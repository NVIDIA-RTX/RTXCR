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

class SampleScene;

class MorphTargetAnimationPass
{
public:
    MorphTargetAnimationPass(
        nvrhi::IDevice* const device,
        std::shared_ptr<donut::engine::ShaderFactory> shaderFactory);

    ~MorphTargetAnimationPass() = default;

    bool CreateMorphTargetAnimationPipeline();
    void RecompileMorphTargetAnimationShaders();

    void Update(const float fElapsedTimeSeconds);

    void Dispatch(
        const std::shared_ptr<donut::engine::MeshInfo> mesh,
        nvrhi::CommandListHandle commandList,
        const ResourceManager::MorphTargetResources& morphTargetResources,
        const TessellationType tessellationType,
        const float animationTimestampPerFrame,
        const bool enableDebugOverride,
        const uint32_t overrideKeyFrameIndex,
        const float overrideKeyFrameWeight);

    void CleanComputePipeline() { m_pso = nullptr; }

    inline void ResetAnimation()
    {
        m_totalTime = 0.0f;
        m_prevAnimationTimestampPerFrame = 0.0f;
    }

private:
    void createShaders();

    nvrhi::IDevice* const m_device;
    std::shared_ptr<donut::engine::ShaderFactory> m_shaderFactory;

    nvrhi::ComputePipelineHandle m_pso;
    nvrhi::BindingLayoutHandle m_bindingLayout;
    nvrhi::BindingSetHandle m_bindingSet;
    nvrhi::ShaderHandle m_shaders[(uint32_t)TessellationType::Count];

    float m_totalTime;
    float m_prevAnimationTimestampPerFrame;
};
