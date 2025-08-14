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
#include <donut/core/math/math.h>
#include <donut/engine/ShaderFactory.h>

#include "../../ResourceManager.h"

#include "NrdConfig.h"
#include "NrdDenoiser.h"

NrdDenoiser::NrdDenoiser(
    nvrhi::IDevice* const device,
    std::shared_ptr<donut::engine::ShaderFactory> shaderFactory,
    const ResourceManager& resourceManager,
    UIData& ui)
    : m_device(device)
    , m_shaderFactory(shaderFactory)
    , m_resourceManager(resourceManager)
    , m_ui(ui)
    , m_resetDenoiser(true)
{
}

bool NrdDenoiser::CreateDenoiserPipelines()
{
    createDenoiserBindingLayout();

    return RecreateDenoiserPipelines();
}

bool NrdDenoiser::RecreateDenoiserPipelines()
{
    // Reblur
    {
        std::vector<donut::engine::ShaderMacro> denoiseReblurMacros =
        {
            donut::engine::ShaderMacro("NRD_NORMAL_ENCODING", "2"),
            donut::engine::ShaderMacro("NRD_ROUGHNESS_ENCODING", "1"),
            donut::engine::ShaderMacro("USE_RELAX", "0"),
        };

        m_denoiserReblurPackCS = m_shaderFactory->CreateShader("app/denoiser.hlsl", "demodulate", &denoiseReblurMacros, nvrhi::ShaderType::Compute);
        nvrhi::ComputePipelineDesc denoiserReblurPipelineDesc = {};
        denoiserReblurPipelineDesc.bindingLayouts = { m_bindingLayout, m_denoiserBindingLayout };
        denoiserReblurPipelineDesc.CS = m_denoiserReblurPackCS;
        m_denoiserReblurPackPso = m_device->createComputePipeline(denoiserReblurPipelineDesc);

        m_compositionReblurCS = m_shaderFactory->CreateShader("app/denoiser.hlsl", "composite", &denoiseReblurMacros, nvrhi::ShaderType::Compute);
        nvrhi::ComputePipelineDesc compositeReblurPipelineDesc = {};
        compositeReblurPipelineDesc.bindingLayouts = { m_bindingLayout, m_denoiserBindingLayout };
        compositeReblurPipelineDesc.CS = m_compositionReblurCS;
        m_compositionReblurPso = m_device->createComputePipeline(compositeReblurPipelineDesc);
    }

    // Relax
    {
        std::vector<donut::engine::ShaderMacro> denoiseRelaxMacros =
        {
            donut::engine::ShaderMacro("NRD_NORMAL_ENCODING", "2"),
            donut::engine::ShaderMacro("NRD_ROUGHNESS_ENCODING", "1"),
            donut::engine::ShaderMacro("USE_RELAX", "1"),
        };

        m_denoiserRelaxPackCS = m_shaderFactory->CreateShader("app/denoiser.hlsl", "demodulate", &denoiseRelaxMacros, nvrhi::ShaderType::Compute);
        nvrhi::ComputePipelineDesc denoiserRelaxPipelineDesc = {};
        denoiserRelaxPipelineDesc.bindingLayouts = { m_bindingLayout, m_denoiserBindingLayout };
        denoiserRelaxPipelineDesc.CS = m_denoiserRelaxPackCS;
        m_denoiserRelaxPackPso = m_device->createComputePipeline(denoiserRelaxPipelineDesc);

        m_compositionRelaxCS = m_shaderFactory->CreateShader("app/denoiser.hlsl", "composite", &denoiseRelaxMacros, nvrhi::ShaderType::Compute);
        nvrhi::ComputePipelineDesc compositeRelaxPipelineDesc = {};
        compositeRelaxPipelineDesc.bindingLayouts = { m_bindingLayout, m_denoiserBindingLayout };
        compositeRelaxPipelineDesc.CS = m_compositionRelaxCS;
        m_compositionRelaxPso = m_device->createComputePipeline(compositeRelaxPipelineDesc);
    }

    m_resetDenoiser = true;

    return (!m_denoiserReblurPackPso) && (!m_denoiserRelaxPackPso) && (!m_compositionReblurPso) && (!m_compositionRelaxPso);
}

void NrdDenoiser::Dispatch(
    nvrhi::CommandListHandle commandList,
    const dm::uint2& renderSize,
    const donut::engine::PlanarView& view,
    const donut::engine::PlanarView& viewPrevious,
    const uint32_t frameIndex)
{
    const nrd::Denoiser denoiserMode = (m_ui.nrdDenoiserMode == NrdMode::Reblur) ?
        nrd::Denoiser::REBLUR_DIFFUSE_SPECULAR :
        nrd::Denoiser::RELAX_DIFFUSE_SPECULAR;

    if (!m_nrd || m_denoiserMode != denoiserMode)
    {
        setDenoiserMode(denoiserMode);

        m_nrd = std::make_unique<NrdIntegration>(m_device, m_resourceManager, m_denoiserMode);
        m_nrd->Initialize(renderSize.x, renderSize.y, *m_shaderFactory);
    }

    if (m_ui.forceResetDenoiser)
    {
        ResetDenoiser();
    }

    const auto& renderTargets = m_resourceManager.GetPathTracerResources();
    const auto& denoiserResources = m_resourceManager.GetDenoiserResources();
    const auto& gBufferResources = renderTargets.gBufferResources;

    {
        nvrhi::BindingSetDesc bindingSetDesc = {};
        bindingSetDesc.bindings =
        {
            nvrhi::BindingSetItem::ConstantBuffer(0, renderTargets.globalArgs),
            nvrhi::BindingSetItem::Texture_UAV(0, renderTargets.pathTracerOutputTexture)
        };

        m_bindingSet = m_device->createBindingSet(bindingSetDesc, m_bindingLayout);
    }
    {
        nvrhi::BindingSetDesc denoiserBindingSetDesc = {};
        denoiserBindingSetDesc.bindings =
        {
            nvrhi::BindingSetItem::Texture_UAV(0, denoiserResources.noisyDiffuseRadianceHitT),
            nvrhi::BindingSetItem::Texture_UAV(1, denoiserResources.noisySpecularRadianceHitT),
            nvrhi::BindingSetItem::Texture_UAV(2, gBufferResources.viewZTexture),
            nvrhi::BindingSetItem::Texture_UAV(3, gBufferResources.shadingNormalRoughnessTexture),
            nvrhi::BindingSetItem::Texture_UAV(4, gBufferResources.motionVectorTexture),
            nvrhi::BindingSetItem::Texture_UAV(5, gBufferResources.emissiveTexture),
            nvrhi::BindingSetItem::Texture_UAV(6, gBufferResources.albedoTexture),
            nvrhi::BindingSetItem::Texture_UAV(7, gBufferResources.specularAlbedoTexture),
        };

        m_denoiserBindingSet = m_device->createBindingSet(denoiserBindingSetDesc, m_denoiserBindingLayout);
    }
    {
        nvrhi::BindingSetDesc denoiserOutBindingSetDesc = {};
        denoiserOutBindingSetDesc.bindings =
        {
            nvrhi::BindingSetItem::Texture_UAV(0, denoiserResources.denoisedDiffuseRadianceHitT),
            nvrhi::BindingSetItem::Texture_UAV(1, denoiserResources.denoisedSpecularRadianceHitT),
            nvrhi::BindingSetItem::Texture_UAV(2, gBufferResources.viewZTexture),
            nvrhi::BindingSetItem::Texture_UAV(3, gBufferResources.shadingNormalRoughnessTexture),
            nvrhi::BindingSetItem::Texture_UAV(4, gBufferResources.motionVectorTexture),
            nvrhi::BindingSetItem::Texture_UAV(5, gBufferResources.emissiveTexture),
            nvrhi::BindingSetItem::Texture_UAV(6, gBufferResources.albedoTexture),
            nvrhi::BindingSetItem::Texture_UAV(7, gBufferResources.specularAlbedoTexture),
        };

        m_denoiserOutBindingSet = m_device->createBindingSet(denoiserOutBindingSetDesc, m_denoiserBindingLayout);
    }

    packDenoisingDataPass(commandList, renderSize);

    denoisingPass(commandList, view, viewPrevious, frameIndex);

    compositionPass(commandList, renderSize);
}

void NrdDenoiser::setDenoiserMode(const nrd::Denoiser denoiserMode)
{
    if (m_denoiserMode != denoiserMode)
    {
        m_denoiserMode = denoiserMode;

        m_nrd = nullptr;

        ResetDenoiser();
    }
}

void NrdDenoiser::createDenoiserBindingLayout()
{
    {
        nvrhi::BindingLayoutDesc bindingLayoutDesc;
        bindingLayoutDesc.visibility = nvrhi::ShaderType::Compute;
        bindingLayoutDesc.registerSpace = 0;
        bindingLayoutDesc.registerSpaceIsDescriptorSet = (m_device->getGraphicsAPI() == nvrhi::GraphicsAPI::VULKAN);
        bindingLayoutDesc.bindings = {
            nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
            nvrhi::BindingLayoutItem::Texture_UAV(0)
        };

        m_bindingLayout = m_device->createBindingLayout(bindingLayoutDesc);
    }

    {
        nvrhi::BindingLayoutDesc denoiserBindingLayoutDesc;
        denoiserBindingLayoutDesc.visibility = nvrhi::ShaderType::Compute;
        denoiserBindingLayoutDesc.registerSpace = 1;
        denoiserBindingLayoutDesc.registerSpaceIsDescriptorSet = (m_device->getGraphicsAPI() == nvrhi::GraphicsAPI::VULKAN);
        denoiserBindingLayoutDesc.bindings = {
            nvrhi::BindingLayoutItem::Texture_UAV(0),
            nvrhi::BindingLayoutItem::Texture_UAV(1),
            nvrhi::BindingLayoutItem::Texture_UAV(2),
            nvrhi::BindingLayoutItem::Texture_UAV(3),
            nvrhi::BindingLayoutItem::Texture_UAV(4),
            nvrhi::BindingLayoutItem::Texture_UAV(5),
            nvrhi::BindingLayoutItem::Texture_UAV(6),
            nvrhi::BindingLayoutItem::Texture_UAV(7),
        };
        m_denoiserBindingLayout = m_device->createBindingLayout(denoiserBindingLayoutDesc);
    }
}

void NrdDenoiser::packDenoisingDataPass(
    nvrhi::CommandListHandle commandList,
    const dm::uint2& renderSize)
{
    nvrhi::ComputeState computeState;
    computeState.bindings = { m_bindingSet, m_denoiserBindingSet };
    if (m_ui.nrdDenoiserMode == NrdMode::Reblur)
    {
        computeState.pipeline = m_denoiserReblurPackPso;
    }
    else
    {
        computeState.pipeline = m_denoiserRelaxPackPso;
    }
    commandList->setComputeState(computeState);

    const uint32_t groupSize = 16;
    const dm::uint2 dispatchSize = { (renderSize.x + groupSize - 1) / groupSize,
                                     (renderSize.y + groupSize - 1) / groupSize };
    commandList->dispatch(dispatchSize.x, dispatchSize.y);
}

void NrdDenoiser::denoisingPass(
    nvrhi::CommandListHandle commandList,
    const donut::engine::PlanarView& view,
    const donut::engine::PlanarView& viewPrevious,
    const uint32_t frameIndex)
{
    const void* method = (m_denoiserMode == nrd::Denoiser::REBLUR_DIFFUSE_SPECULAR) ?
                        static_cast<void*>(&m_ui.reblurSettings) : 
                        static_cast<void*>(&m_ui.relaxSettings);

    m_nrd->DispatchDenoiserPasses(commandList, 0, view, viewPrevious, frameIndex, m_ui.nrdCommonSettings, method, m_resetDenoiser);

    m_resetDenoiser = false;
    m_ui.forceResetDenoiser = false;
}

void NrdDenoiser::compositionPass(
    nvrhi::CommandListHandle commandList,
    const dm::uint2& renderSize)
{
    nvrhi::ComputeState computeState;
    computeState.bindings = { m_bindingSet, m_denoiserOutBindingSet };
    if (m_ui.nrdDenoiserMode == NrdMode::Reblur)
    {
        computeState.pipeline = m_compositionReblurPso;
    }
    else
    {
        computeState.pipeline = m_compositionRelaxPso;
    }
    commandList->setComputeState(computeState);

    const uint32_t groupSize = 16;
    const dm::uint2 dispatchSize = { (renderSize.x + groupSize - 1) / groupSize,
                                     (renderSize.y + groupSize - 1) / groupSize };
    commandList->dispatch(dispatchSize.x, dispatchSize.y);
}
