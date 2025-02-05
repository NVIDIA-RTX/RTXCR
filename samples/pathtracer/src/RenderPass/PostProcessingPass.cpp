/*
 * Copyright (c) 2024-2025, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#include "PostProcessingPass.h"

PostProcessingPass::PostProcessingPass(nvrhi::IDevice* const device, std::shared_ptr<donut::engine::ShaderFactory> shaderFactory)
    : m_device(device)
    , m_shaderFactory(shaderFactory)
{

}

bool PostProcessingPass::CreatePostProcessingPipelines()
{
    bool result = createTonemappingPipeline();

    return result;
}

void PostProcessingPass::RecompilePostProcessingShaders()
{
    m_tonemappingShader = m_shaderFactory->CreateShader("app/tonemapping.hlsl", "main_ps", nullptr, nvrhi::ShaderType::Pixel);

    // Clear old PSO, the new PSO will be recreated in the addTonemappingPass
    m_tonemappingPso = nullptr;
}

void PostProcessingPass::Dispatch(nvrhi::CommandListHandle commandList,
                                  const ResourceManager::PathTracerResources& renderTargets,
                                  std::shared_ptr<donut::engine::CommonRenderPasses> commonPass,
                                  nvrhi::IFramebuffer* framebuffer,
                                  const donut::engine::PlanarView& view)
{
    // TODO: Post Processing Passes
    
    addTonemappingPass(commandList, renderTargets, commonPass, framebuffer, view);
}

bool PostProcessingPass::createTonemappingPipeline()
{
    nvrhi::BindingLayoutDesc bindingLayoutDesc;
    bindingLayoutDesc.visibility = nvrhi::ShaderType::Pixel;
    bindingLayoutDesc.bindings = {
        nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
        nvrhi::BindingLayoutItem::Texture_UAV(0),
        nvrhi::BindingLayoutItem::Texture_UAV(1),
    };

    m_tonemappingBindingLayout = m_device->createBindingLayout(bindingLayoutDesc);

    m_tonemappingShader = m_shaderFactory->CreateShader("app/tonemapping.hlsl", "main_ps", nullptr, nvrhi::ShaderType::Pixel);

    return true;
}

void PostProcessingPass::addTonemappingPass(nvrhi::CommandListHandle commandList,
                                            const ResourceManager::PathTracerResources& renderTargets,
                                            std::shared_ptr<donut::engine::CommonRenderPasses> commonPass,
                                            nvrhi::IFramebuffer* framebuffer,
                                            const donut::engine::PlanarView& view)
{
    if (!m_tonemappingPso)
    {
        nvrhi::GraphicsPipelineDesc pipelineDesc;
        pipelineDesc.primType = nvrhi::PrimitiveType::TriangleStrip;
        pipelineDesc.VS = commonPass->m_FullscreenVS;
        pipelineDesc.PS = m_tonemappingShader;
        pipelineDesc.bindingLayouts = { m_tonemappingBindingLayout };

        pipelineDesc.renderState.rasterState.setCullNone();
        pipelineDesc.renderState.depthStencilState.depthTestEnable = false;
        pipelineDesc.renderState.depthStencilState.stencilEnable = false;

        m_tonemappingPso = m_device->createGraphicsPipeline(pipelineDesc, framebuffer);
    }

    nvrhi::BindingSetDesc bindingSetDesc;
    bindingSetDesc.bindings = {
        nvrhi::BindingSetItem::ConstantBuffer(0, renderTargets.globalArgs),
        nvrhi::BindingSetItem::Texture_UAV(0, renderTargets.postProcessingTexture),
        nvrhi::BindingSetItem::Texture_UAV(1, renderTargets.accumulationTexture)
    };

    m_tonemappingBindingSet = m_device->createBindingSet(bindingSetDesc, m_tonemappingBindingLayout);

    nvrhi::GraphicsState state;
    state.pipeline = m_tonemappingPso;
    state.framebuffer = framebuffer;
    state.bindings = { m_tonemappingBindingSet };
    state.viewport = view.GetViewportState();

    commandList->setGraphicsState(state);

    nvrhi::DrawArguments args;
    args.instanceCount = 1;
    args.vertexCount = 4;
    commandList->draw(args);
}
