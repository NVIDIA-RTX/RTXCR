/*
 * Copyright (c) 2024-2025, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#pragma once

#include <nvrhi/nvrhi.h>
#include <donut/engine/ShaderFactory.h>
#include <donut/engine/CommonRenderPasses.h>
#include <donut/engine/View.h>

#include "../ResourceManager.h"

class PostProcessingPass
{
public:
    PostProcessingPass(nvrhi::IDevice* const device,
                       std::shared_ptr<donut::engine::ShaderFactory> shaderFactory);

    ~PostProcessingPass() = default;

    bool CreatePostProcessingPipelines();
    void RecompilePostProcessingShaders();

    void Dispatch(
        nvrhi::CommandListHandle commandList,
        const ResourceManager::PathTracerResources& renderTargets,
        const nvrhi::TextureHandle denoiserValidationTexture,
        std::shared_ptr<donut::engine::CommonRenderPasses> commonPass,
        nvrhi::IFramebuffer* framebuffer,
        const donut::engine::PlanarView& view);

private:
    bool createTonemappingPipeline();
    void addTonemappingPass(
        nvrhi::CommandListHandle commandList,
        const ResourceManager::PathTracerResources& renderTargets,
        const nvrhi::TextureHandle denoiserValidationTexture,
        std::shared_ptr<donut::engine::CommonRenderPasses> commonPass,
        nvrhi::IFramebuffer* framebuffer,
        const donut::engine::PlanarView& view);

    nvrhi::IDevice* const m_device;
    std::shared_ptr<donut::engine::ShaderFactory> m_shaderFactory;

    nvrhi::GraphicsPipelineHandle m_tonemappingPso;
    nvrhi::BindingLayoutHandle m_tonemappingBindingLayout;
    nvrhi::BindingSetHandle m_tonemappingBindingSet;
    nvrhi::ShaderHandle m_tonemappingShader;
};
