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

#include <NRD.h>
#include <nvrhi/nvrhi.h>
#include <donut/engine/BindingCache.h>

class ResourceManager;

namespace donut::engine
{
    class PlanarView;
    class ShaderFactory;
}

class NrdIntegration
{
public:
    NrdIntegration(nvrhi::IDevice* device, const ResourceManager& resourceManager, nrd::Denoiser method);
    ~NrdIntegration();

    bool Initialize(const uint32_t width, const uint32_t height, donut::engine::ShaderFactory& shaderFactory);
    bool IsAvailable() const;

    void DispatchDenoiserPasses(
        nvrhi::ICommandList* commandList,
        int pass,
        const donut::engine::PlanarView& view,
        const donut::engine::PlanarView& viewPrev,
        uint32_t frameIndex,
        nrd::CommonSettings& nrdCommonSettings,
        const void* methodSettings,
        bool reset);

    void CleanDenoiserTextures();
    bool RecreateDenoiserTextures(const uint32_t width, const uint32_t height);

    const nrd::Denoiser GetDenoiser() const { return m_denoiser; }

private:
    nvrhi::DeviceHandle m_device;
    const ResourceManager& m_resourceManager;

    bool m_initialized;
    nrd::Instance* m_instance;
    nrd::Denoiser m_denoiser;
    nrd::Identifier m_identifier;

    struct NrdPipeline
    {
        nvrhi::ShaderHandle shader;
        nvrhi::BindingLayoutHandle bindingLayout;
        nvrhi::ComputePipelineHandle pipeline;
    };

    nvrhi::BufferHandle m_constantBuffer;
    std::vector<NrdPipeline> m_pipelines;
    std::vector<nvrhi::SamplerHandle> m_samplers;
    std::vector<nvrhi::TextureHandle> m_permanentTextures;
    std::vector<nvrhi::TextureHandle> m_transientTextures;
    donut::engine::BindingCache m_bindingCache;
};