/*
 * Copyright (c) 2024-2025, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#include <nvrhi/utils.h>
#include <donut/app/ApplicationBase.h>
#include <donut/render/SkyPass.h>

#include "../Ui/PathtracerUi.h"
#include "../SampleScene.h"
#include "../ResourceManager.h"
#include "../AccelerationStructure.h"
#include "../ScopeMarker.h"

using namespace donut::math;
#include "../../shaders/payloads.h"

#include "PathTracingPass.h"

PathTracingPass::PathTracingPass(nvrhi::IDevice* const device,
                                 std::shared_ptr<donut::engine::ShaderFactory> shaderFactory,
                                 const std::shared_ptr<SampleScene> scene,
                                 const std::shared_ptr<AccelerationStructure> accelerationStructure,
                                 UIData& ui)
: m_device(device)
, m_shaderFactory(shaderFactory)
, m_scene(scene)
, m_accelerationStructure(accelerationStructure)
, m_accumulatedFrameCount(1)
, m_resetAccumulation(false)
, m_renderSize(0u)
, m_ui(ui)
{
}

void PathTracingPass::createRayTracingBindingLayout()
{
    nvrhi::BindingLayoutDesc bindingLayoutDesc;
    bindingLayoutDesc.visibility = nvrhi::ShaderType::All;
    bindingLayoutDesc.registerSpaceIsDescriptorSet = (m_device->getGraphicsAPI() == nvrhi::GraphicsAPI::VULKAN);
    bindingLayoutDesc.registerSpace = 0;
    bindingLayoutDesc.bindings = {
        nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
        nvrhi::BindingLayoutItem::VolatileConstantBuffer(1),
        nvrhi::BindingLayoutItem::RayTracingAccelStruct(0),
        nvrhi::BindingLayoutItem::StructuredBuffer_SRV(1), // instance
        nvrhi::BindingLayoutItem::StructuredBuffer_SRV(2), // geometry
        nvrhi::BindingLayoutItem::StructuredBuffer_SRV(3), // materials
        nvrhi::BindingLayoutItem::Texture_SRV(4), // Environment Map
        nvrhi::BindingLayoutItem::StructuredBuffer_SRV(5), // Instance Mask for Morph Target
        nvrhi::BindingLayoutItem::Sampler(0),
        nvrhi::BindingLayoutItem::Texture_UAV(0), // path tracer output
        nvrhi::BindingLayoutItem::TypedBuffer_UAV(RTXCR_NVAPI_SHADER_EXT_SLOT), // for nvidia extensions
    };

    m_bindingLayout = m_device->createBindingLayout(bindingLayoutDesc);

    // DLSS/NRD
    {
        bindingLayoutDesc.registerSpace = 1;
        bindingLayoutDesc.bindings = {
            nvrhi::BindingLayoutItem::Texture_SRV(0),
            nvrhi::BindingLayoutItem::Texture_SRV(1),
            nvrhi::BindingLayoutItem::Texture_SRV(2),
            nvrhi::BindingLayoutItem::Texture_SRV(3),
            nvrhi::BindingLayoutItem::Texture_SRV(4),
            nvrhi::BindingLayoutItem::Texture_SRV(5),
            nvrhi::BindingLayoutItem::Texture_SRV(6),
            nvrhi::BindingLayoutItem::Texture_SRV(7),
            nvrhi::BindingLayoutItem::Texture_UAV(0),
            nvrhi::BindingLayoutItem::Texture_UAV(1),
            nvrhi::BindingLayoutItem::Texture_UAV(2),
        };
        m_denoiserBindingLayout = m_device->createBindingLayout(bindingLayoutDesc);
    }
}

bool PathTracingPass::CreateRayTracingPipeline(const nvrhi::BindingLayoutHandle resourceBindingLayout)
{
    createRayTracingBindingLayout();
    return RecreateRayTracingPipeline(resourceBindingLayout);
}

bool PathTracingPass::RecreateRayTracingPipeline(const nvrhi::BindingLayoutHandle resourceBindingLayout)
{
#if USE_DX12
    if (m_device->getGraphicsAPI() == nvrhi::GraphicsAPI::D3D12 && m_device->queryFeatureSupport(nvrhi::Feature::LinearSweptSpheres))
    {
        m_pipelineMacros = { { ShaderMacro("LSS_GEOMETRY_SUPPORTED", "1") }, { ShaderMacro("API_DX12", "1") } };
    }
    else
    {
        m_pipelineMacros = { { ShaderMacro("LSS_GEOMETRY_SUPPORTED", "0") }, { ShaderMacro("API_DX12", "0") } };
    }
#else
    m_pipelineMacros = { { ShaderMacro("LSS_GEOMETRY_SUPPORTED", "0") }, { ShaderMacro("API_DX12", "0") } };
#endif

    nvrhi::ShaderLibraryHandle rayGenShaderLibrary = m_shaderFactory->CreateShaderLibrary("app/PathtracingPass.rgs.hlsl", &m_pipelineMacros);

    std::vector<donut::engine::ShaderMacro> emptyPipelineMacros;
    nvrhi::ShaderLibraryHandle missShaderLibrary = m_shaderFactory->CreateShaderLibrary("app/PathtracingPass.miss.hlsl", &emptyPipelineMacros);
    nvrhi::ShaderLibraryHandle closestHitShaderLibrary = m_shaderFactory->CreateShaderLibrary("app/PathtracingPass.chs.hlsl", &m_pipelineMacros);

    if (!rayGenShaderLibrary || !missShaderLibrary || !closestHitShaderLibrary)
    {
        return false;
    }

    nvrhi::rt::PipelineDesc pipelineDesc = {};
    pipelineDesc.globalBindingLayouts.push_back(m_bindingLayout);
    pipelineDesc.globalBindingLayouts.push_back(m_denoiserBindingLayout);
    pipelineDesc.globalBindingLayouts.push_back(resourceBindingLayout);

    pipelineDesc.shaders =
    {
        { "", rayGenShaderLibrary->getShader("RayGen", nvrhi::ShaderType::RayGeneration), nullptr },
        { "", missShaderLibrary->getShader("Miss", nvrhi::ShaderType::Miss), nullptr },
        { "", missShaderLibrary->getShader("ShadowMiss", nvrhi::ShaderType::Miss), nullptr }
    };

    pipelineDesc.hitGroups =
    {
        {
            "HitGroup",
            closestHitShaderLibrary->getShader("ClosestHit", nvrhi::ShaderType::ClosestHit),
            nullptr, // anyhitShader
            nullptr, // intersectionShader
            nullptr, // bindingLayout
            false    // isProceduralPrimitive
        },
        {
            "HitGroupShadow",
            closestHitShaderLibrary->getShader("ClosestHitShadow", nvrhi::ShaderType::ClosestHit),
            nullptr, // anyhitShader
            nullptr, // intersectionShader
            nullptr, // bindingLayout
            false  // isProceduralPrimitive
        }
    };

    pipelineDesc.maxPayloadSize = max(sizeof(RayPayload), sizeof(ShadowRayPayload));
    if (m_device->queryFeatureSupport(nvrhi::Feature::LinearSweptSpheres))
    {
        // We only enable hlslExtensionsUAV for the devices that support LSS
        pipelineDesc.hlslExtensionsUAV = int32_t(RTXCR_NVAPI_SHADER_EXT_SLOT);
    }

    m_pipelinePermutation.pipeline = m_device->createRayTracingPipeline(pipelineDesc);
    m_pipelinePermutation.shaderTable = m_pipelinePermutation.pipeline->createShaderTable();

    m_pipelinePermutation.shaderTable->setRayGenerationShader("RayGen");
    m_pipelinePermutation.shaderTable->addHitGroup("HitGroup");
    m_pipelinePermutation.shaderTable->addHitGroup("HitGroupShadow");
    m_pipelinePermutation.shaderTable->addMissShader("Miss");
    m_pipelinePermutation.shaderTable->addMissShader("ShadowMiss");

    return true;
}

void PathTracingPass::Dispatch(
    nvrhi::CommandListHandle commandList,
    const ResourceManager::PathTracerResources& renderTargets,
    const ResourceManager::DenoiserResources& denoiserResources,
    const nvrhi::SamplerHandle pathTracingSampler,
    std::shared_ptr<donut::engine::DescriptorTableManager> descriptorTable,
    const dm::uint2 renderSize,
    const bool isEnvMapUpdated)
{
    if (m_accelerationStructure->IsRebuildAS() || dm::any(m_renderSize != renderSize) || isEnvMapUpdated)
    {
        m_device->waitForIdle();

        nvrhi::BindingSetDesc bindingSetDesc = {};
        bindingSetDesc.bindings = {
            nvrhi::BindingSetItem::ConstantBuffer(0, renderTargets.lightConstantsBuffer),
            nvrhi::BindingSetItem::ConstantBuffer(1, renderTargets.globalArgs),
            nvrhi::BindingSetItem::RayTracingAccelStruct(0, m_accelerationStructure->GetTLAS()),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(1, m_scene->GetNativeScene()->GetInstanceBuffer()),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(2, m_scene->GetNativeScene()->GetGeometryBuffer()),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(3, m_scene->GetNativeScene()->GetMaterialBuffer()),
            nvrhi::BindingSetItem::Texture_SRV(4, renderTargets.environmentMapTexture->texture),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(5, renderTargets.instanceMorphTargetMetaDataBuffer),
            nvrhi::BindingSetItem::Sampler(0, pathTracingSampler),
            nvrhi::BindingSetItem::Texture_UAV(0, renderTargets.pathTracerOutputTexture),
            nvrhi::BindingSetItem::TypedBuffer_UAV(RTXCR_NVAPI_SHADER_EXT_SLOT, nullptr), // for nvidia extensions
        };

        m_bindingSet = m_device->createBindingSet(bindingSetDesc, m_bindingLayout);

        m_renderSize = renderSize;
    }

    ++m_accumulatedFrameCount;
    if (IsAccumulationReset())
    {
        m_accumulatedFrameCount = 1;
    }

    // Bind Denoiser resources
    {
        nvrhi::BindingSetDesc denoiserBindingSetDesc = {};
        const ResourceManager::PathTracerResources::GBufferResources& gBufferResources = renderTargets.gBufferResources;
        denoiserBindingSetDesc.bindings = {
            nvrhi::BindingSetItem::Texture_SRV(0, gBufferResources.viewZTexture),
            nvrhi::BindingSetItem::Texture_SRV(1, gBufferResources.shadingNormalRoughnessTexture),
            nvrhi::BindingSetItem::Texture_SRV(2, gBufferResources.motionVectorTexture),
            nvrhi::BindingSetItem::Texture_SRV(3, gBufferResources.emissiveTexture),
            nvrhi::BindingSetItem::Texture_SRV(4, gBufferResources.albedoTexture),
            nvrhi::BindingSetItem::Texture_SRV(5, gBufferResources.specularAlbedoTexture),
            nvrhi::BindingSetItem::Texture_SRV(6, gBufferResources.screenSpaceMotionVectorTexture),
            nvrhi::BindingSetItem::Texture_SRV(7, gBufferResources.deviceZTexture),
            nvrhi::BindingSetItem::Texture_UAV(0, denoiserResources.noisyDiffuseRadianceHitT),
            nvrhi::BindingSetItem::Texture_UAV(1, denoiserResources.noisySpecularRadianceHitT),
            nvrhi::BindingSetItem::Texture_UAV(2, gBufferResources.specularHitDistanceTexture),
        };

        m_denoiserBindingSet = m_device->createBindingSet(denoiserBindingSetDesc, m_denoiserBindingLayout);
    }

    // Transition pathTracerOutput
    commandList->setTextureState(renderTargets.pathTracerOutputTexture.Get(),
                                 nvrhi::TextureSubresourceSet(0, 1, 0, 1),
                                 nvrhi::ResourceStates::UnorderedAccess);
    commandList->commitBarriers();

    commandList->clearState();

    nvrhi::rt::State state;
    state.bindings.push_back(m_bindingSet);
    state.bindings.push_back(m_denoiserBindingSet);
    state.bindings.push_back(descriptorTable->GetDescriptorTable());

    state.shaderTable = m_pipelinePermutation.shaderTable;
    commandList->setRayTracingState(state);

    nvrhi::rt::DispatchRaysArguments args;
    args.width = renderTargets.pathTracerOutputTexture->getDesc().width;
    args.height = renderTargets.pathTracerOutputTexture->getDesc().height;
    commandList->dispatchRays(args);

    m_resetAccumulation = false;
}
