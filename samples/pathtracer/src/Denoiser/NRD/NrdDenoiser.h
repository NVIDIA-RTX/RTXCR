/*
* Copyright (c) 2024-2025, NVIDIA CORPORATION.  All rights reserved.
*
* NVIDIA CORPORATION and its licensors retain all intellectual property
* and proprietary rights in and to this software, related documentation
* and any modifications thereto.  Any use, reproduction, disclosure or
* distribution of this software and related documentation without an express
* license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "NrdIntegration.h"
#include "../../Ui/PathtracerUi.h"

class NrdDenoiser
{
public:
    NrdDenoiser(
        nvrhi::IDevice* const device,
        std::shared_ptr<donut::engine::ShaderFactory> shaderFactory,
        const ResourceManager& resourceManager,
        UIData& ui);

    ~NrdDenoiser() = default;

    void Dispatch(
        nvrhi::CommandListHandle commandList,
        const dm::uint2& renderSize,
        const donut::engine::PlanarView& view,
        const donut::engine::PlanarView& viewPrevious,
        const uint32_t frameIndex);

    bool CreateDenoiserPipelines();
    bool RecreateDenoiserPipelines();

    inline void ResetDenoiser() { m_resetDenoiser = true; }

    inline void RecreateNrdTextures(const dm::uint2& renderSize)
    {
        if (m_nrd)
        {
            m_nrd->CleanDenoiserTextures();
            m_nrd->RecreateDenoiserTextures(renderSize.x, renderSize.y);
        }
    }

    inline void CleanDenoiserTextures()
    {
        if (m_nrd)
        {
            m_nrd->CleanDenoiserTextures();
        }
    }

private:
    void setDenoiserMode(const nrd::Denoiser denoiserMode);
    void createDenoiserBindingLayout();

    void packDenoisingDataPass(nvrhi::CommandListHandle commandList,
                               const dm::uint2& renderSize);

    void denoisingPass(nvrhi::CommandListHandle commandList,
                       const donut::engine::PlanarView& view,
                       const donut::engine::PlanarView& viewPrevious,
                       const uint32_t frameIndex);

    void compositionPass(
        nvrhi::CommandListHandle commandList,
        const dm::uint2& renderSize);

    nvrhi::IDevice* const m_device;
    std::shared_ptr<donut::engine::ShaderFactory> m_shaderFactory;

    nrd::Denoiser m_denoiserMode;

    const ResourceManager& m_resourceManager;

    std::unique_ptr<NrdIntegration> m_nrd;

    nvrhi::ComputePipelineHandle m_denoiserReblurPackPso;
    nvrhi::ComputePipelineHandle m_denoiserRelaxPackPso;
    nvrhi::ComputePipelineHandle m_compositionReblurPso;
    nvrhi::ComputePipelineHandle m_compositionRelaxPso;

    nvrhi::ShaderHandle m_denoiserReblurPackCS;
    nvrhi::ShaderHandle m_denoiserRelaxPackCS;
    nvrhi::ShaderHandle m_compositionReblurCS;
    nvrhi::ShaderHandle m_compositionRelaxCS;

    nvrhi::BindingLayoutHandle m_bindingLayout;
    nvrhi::BindingLayoutHandle m_denoiserBindingLayout;

    nvrhi::BindingSetHandle m_bindingSet;
    nvrhi::BindingSetHandle m_denoiserBindingSet;
    nvrhi::BindingSetHandle m_denoiserOutBindingSet;

    bool m_resetDenoiser;

    UIData& m_ui;
};
