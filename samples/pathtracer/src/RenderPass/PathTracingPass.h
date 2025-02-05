/*
 * Copyright (c) 2024-2025, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#include <nvrhi/nvrhi.h>
#include <donut/engine/ShaderFactory.h>

class SampleScene;
struct ResourceManager::PathTracerResources;
class AccelerationStructure;
struct UIData;

class PathTracingPass
{
public:
    PathTracingPass(nvrhi::IDevice* const device,
                    std::shared_ptr<donut::engine::ShaderFactory> shaderFactory,
                    const std::shared_ptr<SampleScene> scene,
                    const std::shared_ptr<AccelerationStructure> accelerationStructure,
                    UIData& ui);
    ~PathTracingPass() = default;

    bool CreateRayTracingPipeline(const nvrhi::BindingLayoutHandle resourceBindingLayout);
    bool RecreateRayTracingPipeline(const nvrhi::BindingLayoutHandle resourceBindingLayout);

    void Dispatch(
        nvrhi::CommandListHandle commandList,
        const ResourceManager::PathTracerResources& renderTargets,
        const nvrhi::SamplerHandle pathTracingSampler,
        std::shared_ptr<donut::engine::DescriptorTableManager> descriptorTable,
        const dm::uint2 renderSize,
        const bool isEnvMapUpdated);

    inline void ResetAccumulation()
    {
        m_resetAccumulation = true;
    }

    inline bool IsAccumulationReset() const { return m_resetAccumulation; }
    inline uint32_t GetAccumulationFrameCount() const { return m_accumulatedFrameCount; }

private:
    struct PipelinePermutation
    {
        nvrhi::rt::PipelineHandle pipeline;
        nvrhi::rt::ShaderTableHandle shaderTable;
    };

    void createRayTracingBindingLayout();

    void setupAccumulateCount();

    nvrhi::IDevice* const m_device;
    std::shared_ptr<donut::engine::ShaderFactory> m_shaderFactory;

    const std::shared_ptr<SampleScene> m_scene;
    const std::shared_ptr<AccelerationStructure> m_accelerationStructure;

    std::vector<donut::engine::ShaderMacro> m_pipelineMacros;
    nvrhi::BindingLayoutHandle m_bindingLayout;
    PipelinePermutation m_pipelinePermutation;
    nvrhi::BindingSetHandle m_bindingSet;

    bool m_resetAccumulation;
    uint32_t m_accumulatedFrameCount;
    dm::uint2 m_renderSize;

    UIData& m_ui;

    // Denoiser
    nvrhi::BindingLayoutHandle m_denoiserBindingLayout;
    nvrhi::BindingSetHandle m_denoiserBindingSet;
};
