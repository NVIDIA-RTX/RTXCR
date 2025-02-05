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

#include "GBufferPass.h"

GBufferPass::GBufferPass(nvrhi::IDevice* const device,
    std::shared_ptr<donut::engine::ShaderFactory> shaderFactory,
    const std::shared_ptr<SampleScene> scene,
    const std::shared_ptr<AccelerationStructure> accelerationStructure,
    UIData& ui)
    : m_device(device)
    , m_shaderFactory(shaderFactory)
    , m_scene(scene)
    , m_accelerationStructure(accelerationStructure)
    , m_renderSize(0u)
    , m_ui(ui)
{
}

void GBufferPass::createGBufferPassBindingLayout()
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
        nvrhi::BindingLayoutItem::Sampler(0),
        nvrhi::BindingLayoutItem::TypedBuffer_UAV(RTXCR_NVAPI_SHADER_EXT_SLOT), // for nvidia extensions
    };

    m_bindingLayout = m_device->createBindingLayout(bindingLayoutDesc);

    // Denoiser
    {
        bindingLayoutDesc.registerSpace = 1;
        bindingLayoutDesc.bindings = {
            nvrhi::BindingLayoutItem::Texture_UAV(0),
            nvrhi::BindingLayoutItem::Texture_UAV(1),
            nvrhi::BindingLayoutItem::Texture_UAV(2),
            nvrhi::BindingLayoutItem::Texture_UAV(3),
            nvrhi::BindingLayoutItem::Texture_UAV(4),
            nvrhi::BindingLayoutItem::Texture_UAV(5),
            nvrhi::BindingLayoutItem::Texture_UAV(6),
            nvrhi::BindingLayoutItem::Texture_UAV(7),
        };
        m_denoiserBindingLayout = m_device->createBindingLayout(bindingLayoutDesc);
    }
}

bool GBufferPass::CreateGBufferPassPipeline(const nvrhi::BindingLayoutHandle resourceBindingLayout)
{
    createGBufferPassBindingLayout();
    return RecreateGBufferPassPipeline(resourceBindingLayout);
}

bool GBufferPass::RecreateGBufferPassPipeline(const nvrhi::BindingLayoutHandle resourceBindingLayout)
{
    nvrhi::ShaderLibraryHandle rayGenShaderLibrary = m_shaderFactory->CreateShaderLibrary("app/GBufferPass.rgs.hlsl", &m_pipelineMacros);

    std::vector<donut::engine::ShaderMacro> emptyPipelineMacros;
    nvrhi::ShaderLibraryHandle missShaderLibrary = m_shaderFactory->CreateShaderLibrary("app/PathtracingPass.miss.hlsl", &emptyPipelineMacros);

#if USE_DX12
    std::vector<ShaderMacro> ptChsMacros;
    if (m_device->getGraphicsAPI() == nvrhi::GraphicsAPI::D3D12 && m_device->queryFeatureSupport(nvrhi::Feature::LinearSweptSpheres))
    {
        ptChsMacros = { { ShaderMacro("LSS_GEOMETRY_SUPPORTED", "1") }, { ShaderMacro("API_DX12", "1") } };
    }
    else
    {
        ptChsMacros = { { ShaderMacro("LSS_GEOMETRY_SUPPORTED", "0") }, { ShaderMacro("API_DX12", "0") } };
    }
#else
    ptChsMacros = { { ShaderMacro("LSS_GEOMETRY_SUPPORTED", "0") }, { ShaderMacro("API_DX12", "0") } };
#endif
    nvrhi::ShaderLibraryHandle closestHitShaderLibrary = m_shaderFactory->CreateShaderLibrary("app/PathtracingPass.chs.hlsl", &ptChsMacros);

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
        { "", missShaderLibrary->getShader("Miss", nvrhi::ShaderType::Miss), nullptr }
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
        }
    };

    pipelineDesc.maxPayloadSize = max(sizeof(RayPayload), sizeof(ShadowRayPayload));
    pipelineDesc.hlslExtensionsUAV = int32_t(RTXCR_NVAPI_SHADER_EXT_SLOT);

    m_pipelinePermutation.pipeline = m_device->createRayTracingPipeline(pipelineDesc);
    m_pipelinePermutation.shaderTable = m_pipelinePermutation.pipeline->createShaderTable();

    m_pipelinePermutation.shaderTable->setRayGenerationShader("RayGen");
    m_pipelinePermutation.shaderTable->addHitGroup("HitGroup");
    m_pipelinePermutation.shaderTable->addMissShader("Miss");

    return true;
}

void GBufferPass::Dispatch(
    nvrhi::CommandListHandle commandList,
    const ResourceManager::PathTracerResources& renderTargets,
    const nvrhi::SamplerHandle pathTracingSampler,
    std::shared_ptr<donut::engine::DescriptorTableManager> descriptorTable,
    const dm::uint2 renderSize,
    const bool isEnvMapUpdated)
{
    if (m_accelerationStructure->IsRebuildAS() || m_accelerationStructure->IsUpdateAS() || dm::any(m_renderSize != renderSize) || isEnvMapUpdated)
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
            nvrhi::BindingSetItem::Sampler(0, pathTracingSampler),
            nvrhi::BindingSetItem::TypedBuffer_UAV(RTXCR_NVAPI_SHADER_EXT_SLOT, nullptr), // for nvidia extensions
        };

        m_bindingSet = m_device->createBindingSet(bindingSetDesc, m_bindingLayout);

        m_renderSize = renderSize;
    }

    // Bind Denoiser resources
    {
        nvrhi::BindingSetDesc denoiserBindingSetDesc = {};
        const ResourceManager::PathTracerResources::GBufferResources& gBufferResources = renderTargets.gBufferResources;
        denoiserBindingSetDesc.bindings = {
            nvrhi::BindingSetItem::Texture_UAV(0, gBufferResources.viewZTexture),
            nvrhi::BindingSetItem::Texture_UAV(1, gBufferResources.shadingNormalRoughnessTexture),
            nvrhi::BindingSetItem::Texture_UAV(2, gBufferResources.motionVectorTexture),
            nvrhi::BindingSetItem::Texture_UAV(3, gBufferResources.emissiveTexture),
            nvrhi::BindingSetItem::Texture_UAV(4, gBufferResources.albedoTexture),
            nvrhi::BindingSetItem::Texture_UAV(5, gBufferResources.specularAlbedoTexture),
            nvrhi::BindingSetItem::Texture_UAV(6, gBufferResources.screenSpaceMotionVectorTexture),
            nvrhi::BindingSetItem::Texture_UAV(7, gBufferResources.deviceZTexture),
        };

        m_denoiserBindingSet = m_device->createBindingSet(denoiserBindingSetDesc, m_denoiserBindingLayout);
    }

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
}
