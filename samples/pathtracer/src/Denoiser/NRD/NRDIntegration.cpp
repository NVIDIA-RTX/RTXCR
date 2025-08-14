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

static_assert(NRD_VERSION_MAJOR >= 4 && NRD_VERSION_MINOR >= 7, "Unsupported NRD version!");

#include <nvrhi/utils.h>
#include <donut/core/math/math.h>
#include <donut/engine/View.h>
#include <donut/engine/ShaderFactory.h>
#include <sstream>
#include <donut/core/log.h>

#include "../../ResourceManager.h"
#include "../../../shared/shared.h"

static nvrhi::Format GetNvrhiFormat(nrd::Format format)
{
    switch (format)
    {
    case nrd::Format::R8_UNORM:             return nvrhi::Format::R8_UNORM;
    case nrd::Format::R8_SNORM:             return nvrhi::Format::R8_SNORM;
    case nrd::Format::R8_UINT:              return nvrhi::Format::R8_UINT;
    case nrd::Format::R8_SINT:              return nvrhi::Format::R8_SINT;
    case nrd::Format::RG8_UNORM:            return nvrhi::Format::RG8_UNORM;
    case nrd::Format::RG8_SNORM:            return nvrhi::Format::RG8_SNORM;
    case nrd::Format::RG8_UINT:             return nvrhi::Format::RG8_UINT;
    case nrd::Format::RG8_SINT:             return nvrhi::Format::RG8_SINT;
    case nrd::Format::RGBA8_UNORM:          return nvrhi::Format::RGBA8_UNORM;
    case nrd::Format::RGBA8_SNORM:          return nvrhi::Format::RGBA8_SNORM;
    case nrd::Format::RGBA8_UINT:           return nvrhi::Format::RGBA8_UINT;
    case nrd::Format::RGBA8_SINT:           return nvrhi::Format::RGBA8_SINT;
    case nrd::Format::RGBA8_SRGB:           return nvrhi::Format::SRGBA8_UNORM;
    case nrd::Format::R16_UNORM:            return nvrhi::Format::R16_UNORM;
    case nrd::Format::R16_SNORM:            return nvrhi::Format::R16_SNORM;
    case nrd::Format::R16_UINT:             return nvrhi::Format::R16_UINT;
    case nrd::Format::R16_SINT:             return nvrhi::Format::R16_SINT;
    case nrd::Format::R16_SFLOAT:           return nvrhi::Format::R16_FLOAT;
    case nrd::Format::RG16_UNORM:           return nvrhi::Format::RG16_UNORM;
    case nrd::Format::RG16_SNORM:           return nvrhi::Format::RG16_SNORM;
    case nrd::Format::RG16_UINT:            return nvrhi::Format::RG16_UINT;
    case nrd::Format::RG16_SINT:            return nvrhi::Format::RG16_SINT;
    case nrd::Format::RG16_SFLOAT:          return nvrhi::Format::RG16_FLOAT;
    case nrd::Format::RGBA16_UNORM:         return nvrhi::Format::RGBA16_UNORM;
    case nrd::Format::RGBA16_SNORM:         return nvrhi::Format::RGBA16_SNORM;
    case nrd::Format::RGBA16_UINT:          return nvrhi::Format::RGBA16_UINT;
    case nrd::Format::RGBA16_SINT:          return nvrhi::Format::RGBA16_SINT;
    case nrd::Format::RGBA16_SFLOAT:        return nvrhi::Format::RGBA16_FLOAT;
    case nrd::Format::R32_UINT:             return nvrhi::Format::R32_UINT;
    case nrd::Format::R32_SINT:             return nvrhi::Format::R32_SINT;
    case nrd::Format::R32_SFLOAT:           return nvrhi::Format::R32_FLOAT;
    case nrd::Format::RG32_UINT:            return nvrhi::Format::RG32_UINT;
    case nrd::Format::RG32_SINT:            return nvrhi::Format::RG32_SINT;
    case nrd::Format::RG32_SFLOAT:          return nvrhi::Format::RG32_FLOAT;
    case nrd::Format::RGB32_UINT:           return nvrhi::Format::RGB32_UINT;
    case nrd::Format::RGB32_SINT:           return nvrhi::Format::RGB32_SINT;
    case nrd::Format::RGB32_SFLOAT:         return nvrhi::Format::RGB32_FLOAT;
    case nrd::Format::RGBA32_UINT:          return nvrhi::Format::RGBA32_UINT;
    case nrd::Format::RGBA32_SINT:          return nvrhi::Format::RGBA32_SINT;
    case nrd::Format::RGBA32_SFLOAT:        return nvrhi::Format::RGBA32_FLOAT;
    case nrd::Format::R10_G10_B10_A2_UNORM: return nvrhi::Format::R10G10B10A2_UNORM;
    case nrd::Format::R10_G10_B10_A2_UINT:  return nvrhi::Format::UNKNOWN; // not representable and not used
    case nrd::Format::R11_G11_B10_UFLOAT:   return nvrhi::Format::R11G11B10_FLOAT;
    case nrd::Format::R9_G9_B9_E5_UFLOAT:   return nvrhi::Format::UNKNOWN; // not representable and not used
    default:                                return nvrhi::Format::UNKNOWN;
    }
}

NrdIntegration::NrdIntegration(nvrhi::IDevice* device, const ResourceManager& resourceManager, nrd::Denoiser denoiser)
    : m_device(device)
    , m_resourceManager(resourceManager)
    , m_initialized(false)
    , m_instance(nullptr)
    , m_denoiser(denoiser)
    , m_bindingCache(device)
    , m_identifier(0)
{
}

NrdIntegration::~NrdIntegration()
{
    if (m_initialized)
        nrd::DestroyInstance(*m_instance);
}

bool NrdIntegration::Initialize(const uint32_t width, const uint32_t height, donut::engine::ShaderFactory& shaderFactory)
{
    const nrd::LibraryDesc& libraryDesc = nrd::GetLibraryDesc();

    const nrd::DenoiserDesc denoiserDescs[] =
    {
        { m_identifier, m_denoiser }
    };

    nrd::InstanceCreationDesc instanceCreationDesc = {};
    instanceCreationDesc.denoisers = denoiserDescs;
    instanceCreationDesc.denoisersNum = _countof(denoiserDescs);

    nrd::Result result = nrd::CreateInstance(instanceCreationDesc, m_instance);
    if (result != nrd::Result::SUCCESS)
        return false;

    const nrd::InstanceDesc& instanceDesc = nrd::GetInstanceDesc(*m_instance);

    const nvrhi::BufferDesc constantBufferDesc = nvrhi::utils::CreateVolatileConstantBufferDesc(
        instanceDesc.constantBufferMaxDataSize,
        "NrdConstantBuffer",
        instanceDesc.descriptorPoolDesc.setsMaxNum * 4);

    m_constantBuffer = m_device->createBuffer(constantBufferDesc);

    for (uint32_t samplerIndex = 0; samplerIndex < instanceDesc.samplersNum; samplerIndex++)
    {
        const nrd::Sampler& samplerMode = instanceDesc.samplers[samplerIndex];

        nvrhi::SamplerAddressMode addressMode = nvrhi::SamplerAddressMode::Wrap;
        bool filter = false;

        switch (samplerMode)
        {
        case nrd::Sampler::NEAREST_CLAMP:
            addressMode = nvrhi::SamplerAddressMode::Clamp;
            filter = false;
            break;
        case nrd::Sampler::LINEAR_CLAMP:
            addressMode = nvrhi::SamplerAddressMode::Clamp;
            filter = true;
            break;
        default:
            assert(!"Unknown NRD sampler mode");
            break;
        }

        auto samplerDesc = nvrhi::SamplerDesc()
            .setAllAddressModes(addressMode)
            .setAllFilters(filter);

        const nvrhi::SamplerHandle sampler = m_device->createSampler(samplerDesc);

        if (!sampler)
        {
            assert(!"Cannot create an NRD sampler");
            return false;
        }

        m_samplers.push_back(sampler);
    }

    for (uint32_t pipelineIndex = 0; pipelineIndex < instanceDesc.pipelinesNum; pipelineIndex++)
    {
        const nrd::PipelineDesc& nrdPipelineDesc = instanceDesc.pipelines[pipelineIndex];

        std::string fileName = std::string("nrd/RayTracingDenoiser/Shaders/Source/") + nrdPipelineDesc.shaderFileName;
        std::vector<donut::engine::ShaderMacro> macros = { {"NRD_COMPILER_DXC", "1"}, {"NRD_NORMAL_ENCODING", "2"}, {"NRD_ROUGHNESS_ENCODING", "1"} };

        NrdPipeline pipeline;
        pipeline.shader = shaderFactory.CreateShader(fileName.c_str(), "main", &macros, nvrhi::ShaderType::Compute);

        if (!pipeline.shader)
        {
            assert(!"Cannot create an NRD shader");
            return false;
        }

        nvrhi::BindingLayoutDesc layoutDesc;
        layoutDesc.visibility = nvrhi::ShaderType::Compute;

        nvrhi::BindingLayoutItem constantBufferItem = nvrhi::BindingLayoutItem::VolatileConstantBuffer(instanceDesc.constantBufferRegisterIndex);
        layoutDesc.bindings.push_back(constantBufferItem);

        assert(instanceDesc.samplersSpaceIndex == 0);
        for (uint32_t samplerIndex = 0; samplerIndex < instanceDesc.samplersNum; samplerIndex++)
        {
            nvrhi::BindingLayoutItem samplerItem = nvrhi::BindingLayoutItem::Sampler(instanceDesc.samplersBaseRegisterIndex + samplerIndex);
            layoutDesc.bindings.push_back(samplerItem);
        }

        for (uint32_t descriptorRangeIndex = 0; descriptorRangeIndex < nrdPipelineDesc.resourceRangesNum; descriptorRangeIndex++)
        {
            const nrd::ResourceRangeDesc& nrdDescriptorRange = nrdPipelineDesc.resourceRanges[descriptorRangeIndex];

            nvrhi::BindingLayoutItem resourceItem = {};
            switch (nrdDescriptorRange.descriptorType)
            {
            case nrd::DescriptorType::TEXTURE:
                resourceItem = nvrhi::BindingLayoutItem::Texture_SRV(0);
                break;
            case nrd::DescriptorType::STORAGE_TEXTURE:
                resourceItem = nvrhi::BindingLayoutItem::Texture_UAV(0);
                break;
            default:
                assert(!"Unknown NRD descriptor type");
                break;
            }

            for (uint32_t descriptorOffset = 0; descriptorOffset < nrdDescriptorRange.descriptorsNum; descriptorOffset++)
            {
                resourceItem.slot = nrdDescriptorRange.baseRegisterIndex + descriptorOffset;
                layoutDesc.bindings.push_back(resourceItem);
            }
        }
        //Shaders have been compiled with different offsets than standard
            layoutDesc.bindingOffsets
            .setConstantBufferOffset(300)
            .setSamplerOffset(100)
            .setShaderResourceOffset(200)
            .setUnorderedAccessViewOffset(400);

        pipeline.bindingLayout = m_device->createBindingLayout(layoutDesc);

        if (!pipeline.bindingLayout)
        {
            assert(!"Cannot create an NRD binding layout");
            return false;
        }

        nvrhi::ComputePipelineDesc pipelineDesc;
        pipelineDesc.bindingLayouts = { pipeline.bindingLayout };
        pipelineDesc.CS = pipeline.shader;
        pipeline.pipeline = m_device->createComputePipeline(pipelineDesc);

        if (!pipeline.pipeline)
        {
            assert(!"Cannot create an NRD pipeline");
            return false;
        }

        m_pipelines.push_back(pipeline);
    }

    m_initialized = RecreateDenoiserTextures(width, height);

    return true;
}

void NrdIntegration::CleanDenoiserTextures()
{
    m_permanentTextures.clear();
    m_transientTextures.clear();
}

bool NrdIntegration::RecreateDenoiserTextures(const uint32_t width, const uint32_t height)
{
    const nrd::InstanceDesc& instanceDesc = nrd::GetInstanceDesc(*m_instance);
    const uint32_t poolSize = instanceDesc.permanentPoolSize + instanceDesc.transientPoolSize;

    for (uint32_t i = 0; i < poolSize; i++)
    {
        const bool isPermanent = (i < instanceDesc.permanentPoolSize);

        const nrd::TextureDesc& nrdTextureDesc = isPermanent
            ? instanceDesc.permanentPool[i]
            : instanceDesc.transientPool[i - instanceDesc.permanentPoolSize];

        const nvrhi::Format format = GetNvrhiFormat(nrdTextureDesc.format);
        if (format == nvrhi::Format::UNKNOWN)
        {
            assert(!"Unknown or unsupported NRD format");
            return false;
        }

        std::stringstream ss;
        ss << "NRD " << (isPermanent ? "Permanent" : "Transient") << "Texture [" << (isPermanent ? i : i - instanceDesc.permanentPoolSize) << "]";

        nvrhi::TextureDesc textureDesc;
        textureDesc.width = width;
        textureDesc.height = height;
        textureDesc.format = format;
        textureDesc.mipLevels = 1;
        textureDesc.dimension = nvrhi::TextureDimension::Texture2D;
        textureDesc.initialState = nvrhi::ResourceStates::ShaderResource;
        textureDesc.keepInitialState = true;
        textureDesc.isUAV = true;
        textureDesc.debugName = ss.str();

        const nvrhi::TextureHandle texture = m_device->createTexture(textureDesc);

        if (!texture)
        {
            donut::log::fatal("Cannot create an NRD texture");
            return false;
        }

        if (isPermanent)
        {
            m_permanentTextures.push_back(texture);
        }
        else
        {
            m_transientTextures.push_back(texture);
        }
    }

    return true;
}

bool NrdIntegration::IsAvailable() const
{
    return m_initialized;
}

static inline void MatrixToNrd(float* dest, const dm::float4x4& m)
{
    dm::float4x4 tm = dm::transpose(m);
    memcpy(dest, &m, sizeof(m));
}

void NrdIntegration::DispatchDenoiserPasses(
    nvrhi::ICommandList* commandList,
    int pass,
    const donut::engine::PlanarView& view,
    const donut::engine::PlanarView& viewPrev,
    uint32_t frameIndex,
    nrd::CommonSettings& nrdCommonSettings,
    const void* methodSettings,
    bool reset)
{
    if (methodSettings)
    {
        nrd::SetDenoiserSettings(*m_instance, m_identifier, methodSettings);
    }

    const bool isMotionVectorInWorldSpace = false;
    const dm::float2 pixelOffset = view.GetPixelOffset();
    const dm::float2 prevPixelOffset = viewPrev.GetPixelOffset();

    MatrixToNrd(nrdCommonSettings.viewToClipMatrix, view.GetProjectionMatrix(false));
    MatrixToNrd(nrdCommonSettings.viewToClipMatrixPrev, viewPrev.GetProjectionMatrix(false));
    MatrixToNrd(nrdCommonSettings.worldToViewMatrix, dm::affineToHomogeneous(view.GetViewMatrix()));
    MatrixToNrd(nrdCommonSettings.worldToViewMatrixPrev, dm::affineToHomogeneous(viewPrev.GetViewMatrix()));
    nrdCommonSettings.motionVectorScale[0] = isMotionVectorInWorldSpace ? 1.0f : 1.0f / view.GetViewExtent().width();
    nrdCommonSettings.motionVectorScale[1] = isMotionVectorInWorldSpace ? 1.0f : 1.0f / view.GetViewExtent().height();
    nrdCommonSettings.motionVectorScale[2] = 1.0f;
    nrdCommonSettings.cameraJitter[0] = pixelOffset.x;
    nrdCommonSettings.cameraJitter[1] = pixelOffset.y;
    nrdCommonSettings.cameraJitterPrev[0] = prevPixelOffset.x;
    nrdCommonSettings.cameraJitterPrev[1] = prevPixelOffset.y;
    nrdCommonSettings.resourceSize[0] = view.GetViewExtent().width();
    nrdCommonSettings.resourceSize[1] = view.GetViewExtent().height();
    nrdCommonSettings.resourceSizePrev[0] = view.GetViewExtent().width();
    nrdCommonSettings.resourceSizePrev[1] = view.GetViewExtent().height();
    nrdCommonSettings.rectSize[0] = view.GetViewExtent().width();
    nrdCommonSettings.rectSize[1] = view.GetViewExtent().height();
    nrdCommonSettings.rectSizePrev[0] = view.GetViewExtent().width();
    nrdCommonSettings.rectSizePrev[1] = view.GetViewExtent().height();
    nrdCommonSettings.timeDeltaBetweenFrames = 0.0f;
    nrdCommonSettings.denoisingRange = TRACING_FAR_DENOISING_DISTANCE;
    nrdCommonSettings.disocclusionThreshold = 0.01f; ///< Coming from the UI
    nrdCommonSettings.disocclusionThresholdAlternate = 0.05f; ///< Coming from the UI
    nrdCommonSettings.splitScreen = 0.0f;
    nrdCommonSettings.rectOrigin[0] = 0;
    nrdCommonSettings.rectOrigin[1] = 0;
    nrdCommonSettings.frameIndex = frameIndex;
    nrdCommonSettings.accumulationMode = reset ? nrd::AccumulationMode::RESTART : nrd::AccumulationMode::CONTINUE;
    nrdCommonSettings.isMotionVectorInWorldSpace = isMotionVectorInWorldSpace;
    nrdCommonSettings.isHistoryConfidenceAvailable = false;
    nrdCommonSettings.isDisocclusionThresholdMixAvailable = false; ///< Coming from the UI
    nrdCommonSettings.isBaseColorMetalnessAvailable = false;

    nrd::SetCommonSettings(*m_instance, nrdCommonSettings);

    const nrd::DispatchDesc* dispatchDescs = nullptr;
    uint32_t dispatchDescNum = 0;
    nrd::GetComputeDispatches(*m_instance, &m_identifier, 1, dispatchDescs, dispatchDescNum);

    const nrd::InstanceDesc& instanceDesc = nrd::GetInstanceDesc(*m_instance);

    const ResourceManager::PathTracerResources::GBufferResources& gbufferResources = m_resourceManager.GetGBufferResources();
    const ResourceManager::DenoiserResources& denoiserResources = m_resourceManager.GetDenoiserResources();

    for (uint32_t dispatchIndex = 0; dispatchIndex < dispatchDescNum; dispatchIndex++)
    {
        const nrd::DispatchDesc& dispatchDesc = dispatchDescs[dispatchIndex];

        if (dispatchDesc.name)
            commandList->beginMarker(dispatchDesc.name);

        assert(m_constantBuffer);
        commandList->writeBuffer(m_constantBuffer, dispatchDesc.constantBufferData, dispatchDesc.constantBufferDataSize);

        nvrhi::BindingSetDesc setDesc;
        setDesc.bindings.push_back(nvrhi::BindingSetItem::ConstantBuffer(instanceDesc.constantBufferRegisterIndex, m_constantBuffer));

        for (uint32_t samplerIndex = 0; samplerIndex < instanceDesc.samplersNum; samplerIndex++)
        {
            assert(m_samplers[samplerIndex]);
            setDesc.bindings.push_back(nvrhi::BindingSetItem::Sampler(instanceDesc.samplersBaseRegisterIndex + samplerIndex, m_samplers[samplerIndex]));
        }

        const nrd::PipelineDesc& nrdPipelineDesc = instanceDesc.pipelines[dispatchDesc.pipelineIndex];
        uint32_t resourceIndex = 0;

        for (uint32_t descriptorRangeIndex = 0; descriptorRangeIndex < nrdPipelineDesc.resourceRangesNum; descriptorRangeIndex++)
        {
            const nrd::ResourceRangeDesc& nrdDescriptorRange = nrdPipelineDesc.resourceRanges[descriptorRangeIndex];

            for (uint32_t descriptorOffset = 0; descriptorOffset < nrdDescriptorRange.descriptorsNum; descriptorOffset++)
            {
                assert(resourceIndex < dispatchDesc.resourcesNum);
                const nrd::ResourceDesc& resource = dispatchDesc.resources[resourceIndex];

                assert(resource.descriptorType == nrdDescriptorRange.descriptorType);

                nvrhi::TextureHandle texture;
                switch (resource.type)
                {
                case nrd::ResourceType::IN_MV:
                    texture = gbufferResources.motionVectorTexture;
                    break;
                case nrd::ResourceType::IN_NORMAL_ROUGHNESS:
                    texture = gbufferResources.shadingNormalRoughnessTexture;
                    break;
                case nrd::ResourceType::IN_VIEWZ:
                    texture = gbufferResources.viewZTexture;
                    break;
                case nrd::ResourceType::IN_SPEC_RADIANCE_HITDIST:
                    texture = denoiserResources.noisySpecularRadianceHitT;
                    break;
                case nrd::ResourceType::IN_DIFF_RADIANCE_HITDIST:
                    texture = denoiserResources.noisyDiffuseRadianceHitT;
                    break;
                case nrd::ResourceType::OUT_SPEC_RADIANCE_HITDIST:
                    texture = denoiserResources.denoisedSpecularRadianceHitT;
                    break;
                case nrd::ResourceType::OUT_DIFF_RADIANCE_HITDIST:
                    texture = denoiserResources.denoisedDiffuseRadianceHitT;
                    break;
                case nrd::ResourceType::TRANSIENT_POOL:
                    texture = m_transientTextures[resource.indexInPool];
                    break;
                case nrd::ResourceType::PERMANENT_POOL:
                    texture = m_permanentTextures[resource.indexInPool];
                    break;
                case nrd::ResourceType::OUT_VALIDATION:
                    texture = denoiserResources.validationTexture;
                    break;
                default:
                    assert(!"Unavailable resource type");
                    break;
                }

                assert(texture);

                nvrhi::TextureSubresourceSet subresources = nvrhi::AllSubresources;
                subresources.baseMipLevel = 0;
                subresources.numMipLevels = 1;

                nvrhi::BindingSetItem setItem = nvrhi::BindingSetItem::None();
                setItem.resourceHandle = texture;
                setItem.slot = nrdDescriptorRange.baseRegisterIndex + descriptorOffset;
                setItem.subresources = subresources;
                setItem.type = (nrdDescriptorRange.descriptorType == nrd::DescriptorType::TEXTURE)
                    ? nvrhi::ResourceType::Texture_SRV
                    : nvrhi::ResourceType::Texture_UAV;

                setDesc.bindings.push_back(setItem);

                resourceIndex++;
            }
        }

        assert(resourceIndex == dispatchDesc.resourcesNum);

        const NrdPipeline& pipeline = m_pipelines[dispatchDesc.pipelineIndex];

        nvrhi::BindingSetHandle bindingSet = m_bindingCache.GetOrCreateBindingSet(setDesc, pipeline.bindingLayout);

        nvrhi::ComputeState state;
        state.bindings = { bindingSet };
        state.pipeline = pipeline.pipeline;
        commandList->setComputeState(state);

        commandList->dispatch(dispatchDesc.gridWidth, dispatchDesc.gridHeight);

        if (dispatchDesc.name)
        {
            commandList->endMarker();
        }
    }
}