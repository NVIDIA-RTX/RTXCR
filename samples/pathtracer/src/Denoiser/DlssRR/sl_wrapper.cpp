/*
 * Copyright (c) 2023, NVIDIA CORPORATION. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "sl_wrapper.h"

#include <sl_security.h>

#if USE_DX12
#include <DXGI.h>
#include <d3d12.h>
#include <nvrhi/d3d12.h>
#endif
#if USE_VK
#include <vulkan/vulkan.h>
#include <nvrhi/vulkan.h>
// Needed to access internal state of VK resources
#include "../../../donut/nvrhi/src/vulkan/vulkan-backend.h"
#endif

#include <vector>

using namespace donut;

static const char* GetSLErrorString(sl::Result res)
{
    switch (res)
    {
        case sl::Result::eOk: return "Ok";
        case sl::Result::eErrorIO: return "ErrorIO";
        case sl::Result::eErrorDriverOutOfDate: return "ErrorDriverOutOfDate";
        case sl::Result::eErrorOSOutOfDate: return "ErrorOSOutOfDate";
        case sl::Result::eErrorOSDisabledHWS: return "ErrorOSDisabledHWS";
        case sl::Result::eErrorDeviceNotCreated: return "ErrorDeviceNotCreated";
        case sl::Result::eErrorNoSupportedAdapterFound: return "ErrorNoSupportedAdapterFound";
        case sl::Result::eErrorAdapterNotSupported: return "ErrorAdapterNotSupported";
        case sl::Result::eErrorNoPlugins: return "ErrorNoPlugins";
        case sl::Result::eErrorVulkanAPI: return "ErrorVulkanAPI";
        case sl::Result::eErrorDXGIAPI: return "ErrorDXGIAPI";
        case sl::Result::eErrorD3DAPI: return "ErrorD3DAPI";
        case sl::Result::eErrorNRDAPI: return "ErrorNRDAPI";
        case sl::Result::eErrorNVAPI: return "ErrorNVAPI";
        case sl::Result::eErrorReflexAPI: return "ErrorReflexAPI";
        case sl::Result::eErrorNGXFailed: return "ErrorNGXFailed";
        case sl::Result::eErrorJSONParsing: return "ErrorJSONParsing";
        case sl::Result::eErrorMissingProxy: return "ErrorMissingProxy";
        case sl::Result::eErrorMissingResourceState: return "ErrorMissingResourceState";
        case sl::Result::eErrorInvalidIntegration: return "ErrorInvalidIntegration";
        case sl::Result::eErrorMissingInputParameter: return "ErrorMissingInputParameter";
        case sl::Result::eErrorNotInitialized: return "ErrorNotInitialized";
        case sl::Result::eErrorComputeFailed: return "ErrorComputeFailed";
        case sl::Result::eErrorInitNotCalled: return "ErrorInitNotCalled";
        case sl::Result::eErrorExceptionHandler: return "ErrorExceptionHandler";
        case sl::Result::eErrorInvalidParameter: return "ErrorInvalidParameter";
        case sl::Result::eErrorMissingConstants: return "ErrorMissingConstants";
        case sl::Result::eErrorDuplicatedConstants: return "ErrorDuplicatedConstants";
        case sl::Result::eErrorMissingOrInvalidAPI: return "ErrorMissingOrInvalidAPI";
        case sl::Result::eErrorCommonConstantsMissing: return "ErrorCommonConstantsMissing";
        case sl::Result::eErrorUnsupportedInterface: return "ErrorUnsupportedInterface";
        case sl::Result::eErrorFeatureMissing: return "ErrorFeatureMissing";
        case sl::Result::eErrorFeatureNotSupported: return "ErrorFeatureNotSupported";
        case sl::Result::eErrorFeatureMissingHooks: return "ErrorFeatureMissingHooks";
        case sl::Result::eErrorFeatureFailedToLoad: return "ErrorFeatureFailedToLoad";
        case sl::Result::eErrorFeatureWrongPriority: return "ErrorFeatureWrongPriority";
        case sl::Result::eErrorFeatureMissingDependency: return "ErrorFeatureMissingDependency";
        case sl::Result::eErrorFeatureManagerInvalidState: return "ErrorFeatureManagerInvalidState";
        case sl::Result::eErrorInvalidState: return "ErrorInvalidState";
        case sl::Result::eWarnOutOfVRAM: return "WarnOutOfVRAM";
        default: return "Unknown";
    }
}

static std::wstring GetSlInterposerDllLocation()
{
    wchar_t path[MAX_PATH];

    if (GetModuleFileNameW(nullptr, path, sizeof(path)) == 0)
        return std::wstring();

    std::filesystem::path dllPath = std::filesystem::path(path).parent_path() / "sl.interposer.dll";
    return dllPath.wstring();
}

static D3D12_RESOURCE_STATES NvRHIStateToD3D12(nvrhi::ResourceStates stateBits)
{
    if (stateBits == nvrhi::ResourceStates::Common)
        return D3D12_RESOURCE_STATE_COMMON;

    D3D12_RESOURCE_STATES result = D3D12_RESOURCE_STATE_COMMON; // also 0

    if ((stateBits & nvrhi::ResourceStates::ConstantBuffer) != 0) result |= D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    if ((stateBits & nvrhi::ResourceStates::VertexBuffer) != 0) result |= D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    if ((stateBits & nvrhi::ResourceStates::IndexBuffer) != 0) result |= D3D12_RESOURCE_STATE_INDEX_BUFFER;
    if ((stateBits & nvrhi::ResourceStates::IndirectArgument) != 0) result |= D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
    if ((stateBits & nvrhi::ResourceStates::ShaderResource) != 0) result |= D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    if ((stateBits & nvrhi::ResourceStates::UnorderedAccess) != 0) result |= D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    if ((stateBits & nvrhi::ResourceStates::RenderTarget) != 0) result |= D3D12_RESOURCE_STATE_RENDER_TARGET;
    if ((stateBits & nvrhi::ResourceStates::DepthWrite) != 0) result |= D3D12_RESOURCE_STATE_DEPTH_WRITE;
    if ((stateBits & nvrhi::ResourceStates::DepthRead) != 0) result |= D3D12_RESOURCE_STATE_DEPTH_READ;
    if ((stateBits & nvrhi::ResourceStates::StreamOut) != 0) result |= D3D12_RESOURCE_STATE_STREAM_OUT;
    if ((stateBits & nvrhi::ResourceStates::CopyDest) != 0) result |= D3D12_RESOURCE_STATE_COPY_DEST;
    if ((stateBits & nvrhi::ResourceStates::CopySource) != 0) result |= D3D12_RESOURCE_STATE_COPY_SOURCE;
    if ((stateBits & nvrhi::ResourceStates::ResolveDest) != 0) result |= D3D12_RESOURCE_STATE_RESOLVE_DEST;
    if ((stateBits & nvrhi::ResourceStates::ResolveSource) != 0) result |= D3D12_RESOURCE_STATE_RESOLVE_SOURCE;
    if ((stateBits & nvrhi::ResourceStates::Present) != 0) result |= D3D12_RESOURCE_STATE_PRESENT;
    if ((stateBits & nvrhi::ResourceStates::AccelStructRead) != 0) result |= D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;
    if ((stateBits & nvrhi::ResourceStates::AccelStructWrite) != 0) result |= D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;
    if ((stateBits & nvrhi::ResourceStates::AccelStructBuildInput) != 0) result |= D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    if ((stateBits & nvrhi::ResourceStates::AccelStructBuildBlas) != 0) result |= D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;
    if ((stateBits & nvrhi::ResourceStates::ShadingRateSurface) != 0) result |= D3D12_RESOURCE_STATE_SHADING_RATE_SOURCE;

    return result;
}

bool SLWrapper::s_SlInitialized = false;

bool SLWrapper::InitializeStreamline(nvrhi::GraphicsAPI api, const std::vector<sl::Feature>& featuresToLoad)
{
    if (s_SlInitialized)
    {
        log::warning("Streamline already initialized");
        return true;
    }

    sl::Preferences slPreferences{};
    slPreferences.applicationId = FEATURE_DEMO_APP_ID;
    slPreferences.featuresToLoad = featuresToLoad.data();
    slPreferences.numFeaturesToLoad = static_cast<uint32_t>(featuresToLoad.size());
    switch (api)
    {
        case nvrhi::GraphicsAPI::D3D11:  slPreferences.renderAPI = sl::RenderAPI::eD3D11; break;
        case nvrhi::GraphicsAPI::D3D12:  slPreferences.renderAPI = sl::RenderAPI::eD3D12; break;
        case nvrhi::GraphicsAPI::VULKAN: slPreferences.renderAPI = sl::RenderAPI::eVulkan; break;
    }
    slPreferences.flags |= sl::PreferenceFlags::eUseManualHooking;

    std::wstring interposerLibPath = GetSlInterposerDllLocation();
    bool skipSignatureCheck = true;
    if (!skipSignatureCheck && !sl::security::verifyEmbeddedSignature(interposerLibPath.c_str()))
    {
        log::fatal("Streamline signature verification failed");
        return false;
    }

    if (LoadLibraryW(interposerLibPath.c_str()) == nullptr)
    {
        log::fatal("Failed to load Streamline interposer DLL");
        return false;
    }

    sl::Result slRes = slInit(slPreferences, sl::kSDKVersion);
    if (slRes != sl::Result::eOk)
    {
        log::fatal("Failed to initialize Streamline: %s", GetSLErrorString(slRes));
        return false;
    }

    s_SlInitialized = true;

    return true;
}

void SLWrapper::ShutdownStreamline()
{
    if (!s_SlInitialized)
    {
        log::error("Attempting to shutdown streamline when it is not initialized");
    }

    sl::Result slRes = slShutdown();
    if (slRes != sl::Result::eOk)
    {
        log::error("Failed to shutdown Streamline: ", GetSLErrorString(slRes));
    }

    s_SlInitialized = false;
}

#if USE_DX12
bool SLWrapper::GetSupportedDXGIAdapter(const std::vector<sl::Feature>& featuresToLoad, IDXGIAdapter** outAdapter)
{
    if (!s_SlInitialized)
    {
        log::error("Streamline not initialized");
        return false;
    }

    sl::Result slRes = sl::Result::eErrorNoSupportedAdapterFound;

    IDXGIFactory* factory;
    if (SUCCEEDED(CreateDXGIFactory(__uuidof(IDXGIFactory), (void**)&factory)))
    {
        IDXGIAdapter* adapter;
        for (uint32_t i = 0; SUCCEEDED(factory->EnumAdapters(i, &adapter)); i++)
        {
            DXGI_ADAPTER_DESC desc{};
            if (SUCCEEDED(adapter->GetDesc(&desc)))
            {
                sl::AdapterInfo adapterInfo{};
                adapterInfo.deviceLUID = (uint8_t*)&desc.AdapterLuid;
                adapterInfo.deviceLUIDSizeInBytes = sizeof(LUID);

                for (sl::Feature feature : featuresToLoad)
                {
                    slRes = slIsFeatureSupported(feature, adapterInfo);
                    if (slRes != sl::Result::eOk)
                    {
                        break;
                    }
                }

                // All features supported
                if (slRes == sl::Result::eOk)
                {
                    *outAdapter = adapter;
                    break;
                }
            }
            adapter->Release();
        }

        factory->Release();
    }

    if (slRes != sl::Result::eOk)
    {
        log::fatal("Could not find an adapter which supports all required features: %s", GetSLErrorString(slRes));
        return false;
    }

    return true;
}
#endif

#if USE_VK
bool SLWrapper::IsSupportedVulkanDevice(const std::vector<sl::Feature>& featuresToLoad, VkPhysicalDevice* vkPhysicalDevice)
{
    if (!s_SlInitialized)
    {
        log::error("Streamline not initialized");
        return false;
    }

    sl::AdapterInfo adapterInfo{};
    adapterInfo.vkPhysicalDevice = reinterpret_cast<void*>(vkPhysicalDevice);

    sl::Result slRes = sl::Result::eOk;
    for (sl::Feature feature : featuresToLoad)
    {
        slRes = slIsFeatureSupported(feature, adapterInfo);
        if (slRes != sl::Result::eOk)
        {
            log::error("Feature not supported on this device: %s", GetSLErrorString(slRes));
            return false;
        }
    }

    return true;
}
#endif

sl::Resource SLWrapper::NvRHITextureToSL(nvrhi::TextureHandle texture, nvrhi::ResourceStates stateBits)
{
    sl::Resource slResource;
#if USE_DX12
    if (m_GraphicsAPI == nvrhi::GraphicsAPI::D3D12)
    {
        slResource = sl::Resource(sl::ResourceType::eTex2d, texture->getNativeObject(nvrhi::ObjectTypes::D3D12_Resource), NvRHIStateToD3D12(stateBits));
    }
#endif
#if USE_VK
    if (m_GraphicsAPI == nvrhi::GraphicsAPI::VULKAN)
    {
        const nvrhi::TextureDesc& desc = texture->getDesc();
        const vk::ImageCreateInfo& vkDesc = dynamic_cast<nvrhi::vulkan::Texture*>(texture.Get())->imageInfo;
        slResource = sl::Resource(
            sl::ResourceType::eTex2d,
            texture->getNativeObject(nvrhi::ObjectTypes::VK_Image),
            texture->getNativeObject(nvrhi::ObjectTypes::VK_DeviceMemory),
            texture->getNativeView(nvrhi::ObjectTypes::VK_ImageView, desc.format, nvrhi::AllSubresources),
            static_cast<uint32_t>(vkDesc.initialLayout)
        );

        slResource.width = desc.width;
        slResource.height = desc.height;
        slResource.nativeFormat = static_cast<uint32_t>(nvrhi::vulkan::convertFormat(desc.format));
        slResource.mipLevels = desc.mipLevels;
        slResource.arrayLayers = vkDesc.arrayLayers;
        slResource.flags = static_cast<uint32_t>(vkDesc.flags);
        slResource.usage = static_cast<uint32_t>(vkDesc.usage);
    }
#endif

    return slResource;
}

void* SLWrapper::NvRHICommandListToNative(nvrhi::CommandListHandle commandList)
{
#if USE_DX12
    if (m_GraphicsAPI == nvrhi::GraphicsAPI::D3D12)
    {
        return commandList->getNativeObject(nvrhi::ObjectTypes::D3D12_GraphicsCommandList);
    }
#endif
#if USE_VK
    if (m_GraphicsAPI == nvrhi::GraphicsAPI::VULKAN)
    {
        return commandList->getNativeObject(nvrhi::ObjectTypes::VK_CommandBuffer);
    }
#endif
    return nullptr;
}

bool SLWrapper::AdvanceFrame()
{
    if (!s_SlInitialized)
    {
        log::error("Streamline not initialized");
        return false;
    }

    uint32_t frameIndex = m_FrameToken ? ((uint32_t) *m_FrameToken) + 1 : 0;
    sl::Result slRes = slGetNewFrameToken(m_FrameToken, &frameIndex);
    if (slRes != sl::Result::eOk)
    {
        log::error("Could not get new frame token: %s", GetSLErrorString(slRes));
        return false;
    }

    return true;
}

bool SLWrapper::SetConstants(sl::Constants& consts)
{
    if (!s_SlInitialized)
    {
        log::error("Streamline not initialized");
        return false;
    }

    sl::Result slRes = slSetConstants(consts, *m_FrameToken, m_ViewportHandle);
    if (slRes != sl::Result::eOk)
    {
        log::error("Could not set SL constants: %s", GetSLErrorString(slRes));
        return false;
    }

    return true;
}

bool SLWrapper::GetDLSSSettings(const sl::DLSSOptions& options, sl::DLSSOptimalSettings& outSettings)
{
    if (!s_SlInitialized)
    {
        log::error("Streamline not initialized");
        return false;
    }

    sl::Result slRes = slDLSSGetOptimalSettings(options, outSettings);
    if (slRes != sl::Result::eOk)
    {
        log::error("Could not get optimal settings for DLSS: %s", GetSLErrorString(slRes));
        return false;
    }

    return true;
}

bool SLWrapper::GetDLSSRRSettings(const sl::DLSSDOptions& options, sl::DLSSDOptimalSettings& outSettings)
{
    if (!s_SlInitialized)
    {
        log::error("Streamline not initialized");
        return false;
    }

    sl::Result slRes = slDLSSDGetOptimalSettings(options, outSettings);
    if (slRes != sl::Result::eOk)
    {
        log::error("Could not get optimal settings for DLSS-RR: %s", GetSLErrorString(slRes));
        return false;
    }

    return true;
}

bool SLWrapper::TagDLSSBuffers(
    nvrhi::CommandListHandle commandList,
    dm::uint2 renderSize,
    dm::uint2 displaySize,
    nvrhi::TextureHandle inputColor,
    nvrhi::TextureHandle motionVectors,
    nvrhi::TextureHandle depth,
    bool isLinearDepth,
    nvrhi::TextureHandle exposure,
    nvrhi::TextureHandle outputColor
)
{
    if (!s_SlInitialized)
    {
        log::error("Streamline not initialized");
        return false;
    }

    sl::Result slRes;

    // Streamline manages state transitions automatically, so this is not
    // necessary, but it is still useful to place resources in a known state
    commandList->setTextureState(inputColor, nvrhi::AllSubresources, nvrhi::ResourceStates::ShaderResource);
    commandList->setTextureState(motionVectors, nvrhi::AllSubresources, nvrhi::ResourceStates::ShaderResource);
    commandList->setTextureState(depth, nvrhi::AllSubresources, nvrhi::ResourceStates::ShaderResource);
    commandList->setTextureState(outputColor, nvrhi::AllSubresources, nvrhi::ResourceStates::RenderTarget);

    sl::Resource slColorResource = NvRHITextureToSL(inputColor, nvrhi::ResourceStates::ShaderResource);
    sl::Resource slMvecResource = NvRHITextureToSL(motionVectors, nvrhi::ResourceStates::ShaderResource);
    sl::Resource slDepthResource = NvRHITextureToSL(depth, nvrhi::ResourceStates::ShaderResource);
    sl::Resource slOutputResource = NvRHITextureToSL(outputColor, nvrhi::ResourceStates::RenderTarget);

    sl::Extent inputRes = {0, 0, renderSize.x, renderSize.y};
    sl::Extent outputRes = {0, 0, displaySize.x, displaySize.y};
    
    sl::ResourceTag slColorTag(&slColorResource, sl::kBufferTypeScalingInputColor, sl::ResourceLifecycle::eValidUntilPresent, &inputRes);
    sl::ResourceTag slMvecTag(&slMvecResource, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eValidUntilPresent, &inputRes);
    sl::ResourceTag slDepthTag(&slDepthResource, isLinearDepth ? sl::kBufferTypeLinearDepth : sl::kBufferTypeDepth, sl::ResourceLifecycle::eValidUntilPresent, &inputRes);
    sl::ResourceTag slOutputTag(&slOutputResource, sl::kBufferTypeScalingOutputColor, sl::ResourceLifecycle::eValidUntilPresent, &outputRes);

    std::vector<sl::ResourceTag> dlssResourceTags = { slColorTag, slMvecTag, slDepthTag, slOutputTag };

    // Exposure is optional but recommended, autoexposure will be used if not provided
    if (exposure)
    {
        commandList->setTextureState(exposure, nvrhi::AllSubresources, nvrhi::ResourceStates::ShaderResource);
        sl::Resource slExposureResource = NvRHITextureToSL(exposure, nvrhi::ResourceStates::ShaderResource);

        sl::Extent exposureRes = {0, 0, 1, 1};
        sl::ResourceTag slExposureTag(&slExposureResource, sl::kBufferTypeExposure, sl::ResourceLifecycle::eValidUntilPresent, &exposureRes);
        dlssResourceTags.push_back(slExposureTag);
    }

    slRes = slSetTag(m_ViewportHandle, dlssResourceTags.data(), static_cast<uint32_t>(dlssResourceTags.size()), NvRHICommandListToNative(commandList));
    if (slRes != sl::Result::eOk)
    {
        log::error("Could not tag resources for DLSS: %s", GetSLErrorString(slRes));
        return false;
    }

    return true;
}

bool SLWrapper::TagDLSSRRBuffers(
    nvrhi::CommandListHandle commandList,
    dm::uint2 renderSize,
    dm::uint2 displaySize,
    nvrhi::TextureHandle inputColor,
    nvrhi::TextureHandle motionVectors,
    nvrhi::TextureHandle linearDepth,
    nvrhi::TextureHandle diffuseAlbedo,
    nvrhi::TextureHandle specAlbedo,
    nvrhi::TextureHandle normalRoughness,
    nvrhi::TextureHandle specHitDist,
    nvrhi::TextureHandle outputColor
)
{
    if (!s_SlInitialized)
    {
        log::error("Streamline not initialized");
        return false;
    }

    if (!TagDLSSBuffers(commandList, renderSize, displaySize, inputColor, motionVectors, linearDepth, true, nullptr, outputColor))
    {
        return false;
    }

    sl::Result slRes;

    // Streamline manages state transitions automatically, so this is not
    // necessary, but it is still useful to place resources in a known state
    commandList->setTextureState(diffuseAlbedo, nvrhi::AllSubresources, nvrhi::ResourceStates::ShaderResource);
    commandList->setTextureState(specAlbedo, nvrhi::AllSubresources, nvrhi::ResourceStates::ShaderResource);
    commandList->setTextureState(normalRoughness, nvrhi::AllSubresources, nvrhi::ResourceStates::ShaderResource);

    sl::Resource slDiffuseAlbedoResource = NvRHITextureToSL(diffuseAlbedo, nvrhi::ResourceStates::ShaderResource);
    sl::Resource slSpecAlbedoResource = NvRHITextureToSL(specAlbedo, nvrhi::ResourceStates::ShaderResource);
    sl::Resource slNormalRoughnessResource = NvRHITextureToSL(normalRoughness, nvrhi::ResourceStates::ShaderResource);

    sl::Extent inputRes = {0, 0, renderSize.x, renderSize.y};
    sl::Extent outputRes = {0, 0, displaySize.x, displaySize.y};
    
    sl::ResourceTag slDiffuseAlbedoTag(&slDiffuseAlbedoResource, sl::kBufferTypeAlbedo, sl::ResourceLifecycle::eValidUntilPresent, &inputRes);
    sl::ResourceTag slSpecAlbedoTag(&slSpecAlbedoResource, sl::kBufferTypeSpecularAlbedo, sl::ResourceLifecycle::eValidUntilPresent, &inputRes);
    sl::ResourceTag slNormalRoughnessTag(&slNormalRoughnessResource, sl::kBufferTypeNormalRoughness, sl::ResourceLifecycle::eValidUntilPresent, &inputRes);

    std::vector<sl::ResourceTag> dlssrrResourceTags = { slDiffuseAlbedoTag, slSpecAlbedoTag, slNormalRoughnessTag };

    // Specular hit distance is optional
    if (specHitDist)
    {
        commandList->setTextureState(specHitDist, nvrhi::AllSubresources, nvrhi::ResourceStates::ShaderResource);
        sl::Resource slSpecHitDistResource = NvRHITextureToSL(specHitDist, nvrhi::ResourceStates::ShaderResource);

        sl::ResourceTag slSpecHitDistTag(&slSpecHitDistResource, sl::kBufferTypeSpecularHitDistance, sl::ResourceLifecycle::eValidUntilPresent, &inputRes);
        dlssrrResourceTags.push_back(slSpecHitDistTag);
    }

    slRes = slSetTag(m_ViewportHandle, dlssrrResourceTags.data(), static_cast<uint32_t>(dlssrrResourceTags.size()), NvRHICommandListToNative(commandList));
    if (slRes != sl::Result::eOk)
    {
        log::error("Could not tag resources for DLSS-RR: %s", GetSLErrorString(slRes));
        return false;
    }

    return true;
}

bool SLWrapper::SetDLSSOptions(const sl::DLSSOptions& options)
{
    if (!s_SlInitialized)
    {
        log::error("Streamline not initialized");
        return false;
    }

    sl::Result slRes = slDLSSSetOptions(m_ViewportHandle, options);
    if (slRes != sl::Result::eOk)
    {
        log::error("Could not get optimal settings for DLSS: %s", GetSLErrorString(slRes));
        return false;
    }

    return true;
}

bool SLWrapper::SetDLSSRROptions(const sl::DLSSDOptions& options)
{
    if (!s_SlInitialized)
    {
        log::error("Streamline not initialized");
        return false;
    }

    sl::Result slRes = slDLSSDSetOptions(m_ViewportHandle, options);
    if (slRes != sl::Result::eOk)
    {
        log::error("Could not get optimal settings for DLSS-RR: %s", GetSLErrorString(slRes));
        return false;
    }

    return true;
}

bool SLWrapper::EvaluateDLSS(nvrhi::CommandListHandle commandList)
{
    if (!s_SlInitialized)
    {
        log::error("Streamline not initialized");
        return false;
    }

    std::vector<const sl::BaseStructure*> inputs = {&m_ViewportHandle};
    sl::Result slRes = slEvaluateFeature(sl::kFeatureDLSS, *m_FrameToken, inputs.data(), static_cast<uint32_t>(inputs.size()), NvRHICommandListToNative(commandList));
    if (slRes != sl::Result::eOk)
    {
        log::error("Failed to evaluate DLSS: %s", GetSLErrorString(slRes));
        return false;
    }

    return true;
}

bool SLWrapper::EvaluateDLSSRR(nvrhi::CommandListHandle commandList)
{
    if (!s_SlInitialized)
    {
        log::error("Streamline not initialized");
        return false;
    }

    std::vector<const sl::BaseStructure*> inputs = {&m_ViewportHandle};
    sl::Result slRes = slEvaluateFeature(sl::kFeatureDLSS_RR, *m_FrameToken, inputs.data(), static_cast<uint32_t>(inputs.size()), NvRHICommandListToNative(commandList));
    if (slRes != sl::Result::eOk)
    {
        log::error("Failed to evaluate DLSS: %s", GetSLErrorString(slRes));
        return false;
    }

    return true;
}
