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

class GBufferPass
{
public:
    GBufferPass(
        nvrhi::IDevice* const device,
        std::shared_ptr<donut::engine::ShaderFactory> shaderFactory,
        const std::shared_ptr<SampleScene> scene,
        const std::shared_ptr<AccelerationStructure> accelerationStructure,
        UIData& ui);
    ~GBufferPass() = default;

    bool CreateGBufferPassPipeline(const nvrhi::BindingLayoutHandle resourceBindingLayout);
    bool RecreateGBufferPassPipeline(const nvrhi::BindingLayoutHandle resourceBindingLayout);

    void Dispatch(
        nvrhi::CommandListHandle commandList,
        const ResourceManager::PathTracerResources& renderTargets,
        const ResourceManager::DenoiserResources& denoiserResources,
        const nvrhi::SamplerHandle pathTracingSampler,
        std::shared_ptr<donut::engine::DescriptorTableManager> descriptorTable,
        const dm::uint2 renderSize,
        const bool isEnvMapUpdated);

private:
    struct PipelinePermutation
    {
        nvrhi::rt::PipelineHandle pipeline;
        nvrhi::rt::ShaderTableHandle shaderTable;
    };

    void createGBufferPassBindingLayout();

    void setupAccumulateCount();

    nvrhi::IDevice* const m_device;
    std::shared_ptr<donut::engine::ShaderFactory> m_shaderFactory;

    const std::shared_ptr<SampleScene> m_scene;
    const std::shared_ptr<AccelerationStructure> m_accelerationStructure;

    std::vector<donut::engine::ShaderMacro> m_pipelineMacros;
    nvrhi::BindingLayoutHandle m_bindingLayout;
    PipelinePermutation m_pipelinePermutation;
    nvrhi::BindingSetHandle m_bindingSet;

    dm::uint2 m_renderSize;

    UIData& m_ui;

    // Denoiser
    nvrhi::BindingLayoutHandle m_denoiserBindingLayout;
    nvrhi::BindingSetHandle m_denoiserBindingSet;
};
