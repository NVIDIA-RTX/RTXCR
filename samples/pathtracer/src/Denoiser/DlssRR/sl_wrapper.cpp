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

nvrhi::GraphicsAPI SLWrapper::m_GraphicsAPI = nvrhi::GraphicsAPI::D3D12;

bool SLWrapper::m_dlssAvailable = false;
bool SLWrapper::m_dlssrrAvailable = false;
bool SLWrapper::m_dlssgAvailable = false;
bool SLWrapper::m_reflexAvailable = false;
bool SLWrapper::m_pclAvailable = false;

sl::ViewportHandle SLWrapper::m_ViewportHandle = 0;
sl::FrameToken* SLWrapper::m_FrameToken = nullptr;

sl::DLSSGOptions SLWrapper::m_dlssgConsts{};
sl::DLSSGState SLWrapper::m_dlssgSettings{};
bool SLWrapper::m_dlssgTriggerswapchainRecreation = false;
bool SLWrapper::m_dlssgShoudLoad = false;

bool SLWrapper::m_reflexDriverFlashIndicatorEnable = false;

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

void logFunctionCallback(sl::LogType type, const char* msg)
{
    if (type == sl::LogType::eError)
    {
        // Add a breakpoint here to break on errors
        donut::log::error(msg);
    }
    else if (type == sl::LogType::eWarn)
    {
        // Add a breakpoint here to break on warnings
        donut::log::warning(msg);
    }
#if _DEBUG
    else
    {
        donut::log::info(msg);
    }
#endif
}

bool successCheck(sl::Result result, const char* location)
{
    if (result == sl::Result::eOk)
    {
        return true;
    }

    auto a = GetSLErrorString(result);
    if (a != "Unknown")
    {
        const sl::LogType logType = result == sl::Result::eWarnOutOfVRAM ? sl::LogType::eWarn : sl::LogType::eError;
        logFunctionCallback(logType, ("Error: " + std::string(a) + (location == nullptr ? "" : (" encountered in " + std::string(location)))).c_str());
    }
    else
    {
        logFunctionCallback(sl::LogType::eError, ("Unknown error " + static_cast<int>(result) + (location == nullptr ? "" : (" encountered in " + std::string(location)))).c_str());
    }

    return false;

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
    {
        return D3D12_RESOURCE_STATE_COMMON;
    }

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
    slPreferences.flags |= sl::PreferenceFlags::eUseFrameBasedResourceTagging;

#if _DEBUG
    slPreferences.logMessageCallback = api == nvrhi::GraphicsAPI::D3D12 ? &logFunctionCallback : nullptr;
    slPreferences.logLevel = sl::LogLevel::eDefault;
    slPreferences.pathToLogsAndData = L"../../../bin/";
#else
    slPreferences.logLevel = sl::LogLevel::eOff;
#endif

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

void SLWrapper::setSlFeatureFlags(const sl::Feature feature)
{
    switch (feature)
    {
        case sl::kFeatureDLSS:
        {
            m_dlssAvailable = true;
            break;
        }
        case sl::kFeatureDLSS_RR:
        {
            m_dlssrrAvailable = true;
            break;
        }
        case sl::kFeatureDLSS_G:
        {
            m_dlssgAvailable = true;
            break;
        }
        case sl::kFeatureReflex:
        {
            m_reflexAvailable = true;
            break;
        }
        case sl::kFeaturePCL:
        {
            m_pclAvailable = true;
            break;
        }
    }
}

#if USE_DX12
bool SLWrapper::IsSupportedDirectXDevice(const std::vector<sl::Feature>& featuresToLoad, nvrhi::IDevice* device)
{
    if (!s_SlInitialized)
    {
        log::error("Streamline not initialized");
        return false;
    }

    const LUID luid = ((ID3D12Device*)device->getNativeObject(nvrhi::ObjectTypes::D3D12_Device))->GetAdapterLuid();

    sl::AdapterInfo adapterInfo{};
    adapterInfo.deviceLUID = (uint8_t*)(&luid);
    adapterInfo.deviceLUIDSizeInBytes = sizeof(LUID);

    for (sl::Feature feature : featuresToLoad)
    {
        const sl::Result slRes = slIsFeatureSupported(feature, adapterInfo);
        if (slRes == sl::Result::eOk)
        {
            setSlFeatureFlags(feature);
        }
        else
        {
            log::warning("Feature not supported on this device: %s", GetSLErrorString(slRes));
        }
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

    for (sl::Feature feature : featuresToLoad)
    {
        const sl::Result slRes = slIsFeatureSupported(feature, adapterInfo);
        if (slRes == sl::Result::eOk)
        {
            setSlFeatureFlags(feature);
        }
        else
        {
            log::warning("Feature not supported on this device: %s", GetSLErrorString(slRes));
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
        log::warning("SL not initialized or DLSS not available.");
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
    if (!s_SlInitialized || !m_dlssAvailable || !m_dlssrrAvailable)
    {
        log::warning("SL not initialized or DLSS not available.");
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
    if (!s_SlInitialized || !m_dlssAvailable)
    {
        log::warning("SL not initialized or DLSS not available.");
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
    if (!s_SlInitialized || !m_dlssrrAvailable)
    {
        log::warning("SL not initialized or DLSS-RR not available.");
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

void SLWrapper::FeatureLoad(sl::Feature feature, const bool turn_on)
{
    if (m_GraphicsAPI == nvrhi::GraphicsAPI::D3D12) {
        bool loaded;
        slIsFeatureLoaded(feature, loaded);
        if (loaded && !turn_on) {
            slSetFeatureLoaded(feature, turn_on);
        }
        else if (!loaded && turn_on) {
            slSetFeatureLoaded(feature, turn_on);
        }
    }
}

bool SLWrapper::TagDLSSGeneralBuffers(
    nvrhi::CommandListHandle commandList,
    dm::uint2 renderSize,
    dm::uint2 displaySize,
    nvrhi::TextureHandle motionVectors,
    nvrhi::TextureHandle depth)
{
    if (!s_SlInitialized || !m_dlssAvailable)
    {
        log::warning("SL not initialized or DLSS not available.");
        return false;
    }

    sl::Result slRes = sl::Result::eOk;

    commandList->setTextureState(motionVectors, nvrhi::AllSubresources, nvrhi::ResourceStates::ShaderResource);
    commandList->setTextureState(depth, nvrhi::AllSubresources, nvrhi::ResourceStates::ShaderResource);

    sl::Resource slMvecResource = NvRHITextureToSL(motionVectors, nvrhi::ResourceStates::ShaderResource);
    sl::Resource slDepthResource = NvRHITextureToSL(depth, nvrhi::ResourceStates::ShaderResource);

    sl::Extent inputRes = { 0, 0, renderSize.x, renderSize.y };
    sl::Extent outputRes = { 0, 0, displaySize.x, displaySize.y };

    sl::ResourceTag slMvecTag(&slMvecResource, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eValidUntilPresent, &inputRes);
    sl::ResourceTag slDepthTag(&slDepthResource, sl::kBufferTypeDepth, sl::ResourceLifecycle::eOnlyValidNow, &inputRes);

    sl::ResourceTag dlssgResourceTags[] = { slMvecTag, slDepthTag };

    slRes = slSetTagForFrame(*m_FrameToken, m_ViewportHandle, dlssgResourceTags, _countof(dlssgResourceTags), NvRHICommandListToNative(commandList));
    if (slRes != sl::Result::eOk)
    {
        log::error("Could not tag resources for DLSSG: %s", GetSLErrorString(slRes));
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
    if (!s_SlInitialized || !m_dlssAvailable)
    {
        log::warning("SL not initialized or DLSS not available.");
        return false;
    }

    sl::Result slRes = sl::Result::eOk;

    // Streamline manages state transitions automatically, so this is not
    // necessary, but it is still useful to place resources in a known state
    commandList->setTextureState(inputColor, nvrhi::AllSubresources, nvrhi::ResourceStates::ShaderResource);
    commandList->setTextureState(outputColor, nvrhi::AllSubresources, nvrhi::ResourceStates::RenderTarget);

    sl::Resource slColorResource = NvRHITextureToSL(inputColor, nvrhi::ResourceStates::ShaderResource);
    sl::Resource slOutputResource = NvRHITextureToSL(outputColor, nvrhi::ResourceStates::RenderTarget);

    sl::Extent inputRes = {0, 0, renderSize.x, renderSize.y};
    sl::Extent outputRes = {0, 0, displaySize.x, displaySize.y};
    
    sl::ResourceTag slColorTag(&slColorResource, sl::kBufferTypeScalingInputColor, sl::ResourceLifecycle::eValidUntilPresent, &inputRes);
    sl::ResourceTag slOutputTag(&slOutputResource, sl::kBufferTypeScalingOutputColor, sl::ResourceLifecycle::eValidUntilPresent, &outputRes);

    std::vector<sl::ResourceTag> dlssResourceTags = { slColorTag, slOutputTag };

    // Exposure is optional but recommended, auto-exposure will be used if not provided
    if (exposure)
    {
        commandList->setTextureState(exposure, nvrhi::AllSubresources, nvrhi::ResourceStates::ShaderResource);
        sl::Resource slExposureResource = NvRHITextureToSL(exposure, nvrhi::ResourceStates::ShaderResource);

        sl::Extent exposureRes = {0, 0, 1, 1};
        sl::ResourceTag slExposureTag(&slExposureResource, sl::kBufferTypeExposure, sl::ResourceLifecycle::eValidUntilPresent, &exposureRes);
        dlssResourceTags.push_back(slExposureTag);
    }

    slRes = slSetTagForFrame(*m_FrameToken, m_ViewportHandle, dlssResourceTags.data(), static_cast<uint32_t>(dlssResourceTags.size()), NvRHICommandListToNative(commandList));
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
    if (!s_SlInitialized || !m_dlssrrAvailable)
    {
        log::warning("SL not initialized or DLSS-RR not available.");
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

    slRes = slSetTagForFrame(*m_FrameToken, m_ViewportHandle, dlssrrResourceTags.data(), static_cast<uint32_t>(dlssrrResourceTags.size()), NvRHICommandListToNative(commandList));
    if (slRes != sl::Result::eOk)
    {
        log::error("Could not tag resources for DLSS-RR: %s", GetSLErrorString(slRes));
        return false;
    }

    return true;
}

bool SLWrapper::SetDLSSOptions(const sl::DLSSOptions& options)
{
    if (!s_SlInitialized || !m_dlssAvailable)
    {
        log::warning("SL not initialized or DLSS not available.");
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
    if (!s_SlInitialized || !m_dlssrrAvailable)
    {
        log::warning("SL not initialized or DLSS-RR not available.");
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
    if (!s_SlInitialized || !m_dlssAvailable)
    {
        log::warning("SL not initialized or DLSS not available.");
        return false;
    }

    std::vector<const sl::BaseStructure*> inputs = { &m_ViewportHandle };
    sl::Result slRes = slEvaluateFeature(sl::kFeatureDLSS, *m_FrameToken, inputs.data(), static_cast<uint32_t>(inputs.size()), NvRHICommandListToNative(commandList));
    if (slRes != sl::Result::eOk)
    {
        log::warning("Failed to evaluate DLSS: %s", GetSLErrorString(slRes));
        return false;
    }

    commandList->clearState();

    return true;
}

bool SLWrapper::EvaluateDLSSRR(nvrhi::CommandListHandle commandList)
{
    if (!s_SlInitialized || !m_dlssrrAvailable)
    {
        log::warning("SL not initialized or DLSS-RR not available.");
        return false;
    }

    std::vector<const sl::BaseStructure*> inputs = { &m_ViewportHandle };
    sl::Result slRes = slEvaluateFeature(sl::kFeatureDLSS_RR, *m_FrameToken, inputs.data(), static_cast<uint32_t>(inputs.size()), NvRHICommandListToNative(commandList));
    if (slRes != sl::Result::eOk)
    {
        log::warning("Failed to evaluate DLSS: %s", GetSLErrorString(slRes));
        return false;
    }

    commandList->clearState();

    return true;
}

void SLWrapper::SetDLSSGOptions(const sl::DLSSGOptions& consts) {
    if (!s_SlInitialized || !m_dlssgAvailable) {
        log::warning("SL not initialized or DLSSG not available.");
        return;
    }

    m_dlssgConsts = consts;

    successCheck(slDLSSGSetOptions(m_ViewportHandle, m_dlssgConsts), "slDLSSGSetOptions");
}

void SLWrapper::QueryDLSSGState(uint64_t& estimatedVRamUsage, int& fps_multiplier, sl::DLSSGStatus& status, int& minSize, int& maxFrameCount, void*& pFence, uint64_t& fenceValue) {
    if (!s_SlInitialized || !m_dlssgAvailable)
    {
        log::warning("SL not initialized or DLSSG not available.");
        return;
    }

    sl::DLSSGState dlssgSettings{};
    successCheck(slDLSSGGetState(m_ViewportHandle, m_dlssgSettings, &m_dlssgConsts), "slDLSSGGetState");

    estimatedVRamUsage = m_dlssgSettings.estimatedVRAMUsageInBytes;
    fps_multiplier = m_dlssgSettings.numFramesActuallyPresented;
    status = m_dlssgSettings.status;
    minSize = m_dlssgSettings.minWidthOrHeight;
    maxFrameCount = m_dlssgSettings.numFramesToGenerateMax;
    pFence = m_dlssgSettings.inputsProcessingCompletionFence;
    fenceValue = m_dlssgSettings.lastPresentInputsProcessingCompletionFenceValue;
}

uint64_t SLWrapper::GetDLSSGLastFenceValue() { return m_dlssgSettings.lastPresentInputsProcessingCompletionFenceValue; }

void SLWrapper::QueueGPUWaitOnSyncObjectSet(nvrhi::IDevice* pDevice, nvrhi::CommandQueue cmdQType, void* syncObj, uint64_t syncObjVal)
{
    if (pDevice == nullptr)
    {
        log::fatal("Invalid device!");
    }

    switch (pDevice->getGraphicsAPI())
    {
#if DONUT_WITH_DX12
    case nvrhi::GraphicsAPI::D3D12:
    {
        auto pD3d12Device = static_cast<nvrhi::d3d12::IDevice*>(pDevice);
        // device could be recreated during swapchain recreation
        if (!pD3d12Device)
        {
            log::error("D3D12 Device is Invalid.");
            return;
        }
        auto d3d12Queue = static_cast<ID3D12CommandQueue*>(pD3d12Device->getNativeQueue(nvrhi::ObjectTypes::D3D12_CommandQueue, cmdQType));
        d3d12Queue->Wait(reinterpret_cast<ID3D12Fence*>(syncObj), syncObjVal);
    }
    break;
#endif

#if DONUT_WITH_VULKAN
    case nvrhi::GraphicsAPI::VULKAN:
    {
#if _DEBUG
        const auto validationLayer = static_cast<nvrhi::DeviceHandle>(pDevice);
        const auto pVkDevice = static_cast<nvrhi::vulkan::IDevice*>(validationLayer.Get());
#else
        auto pVkDevice = dynamic_cast<nvrhi::vulkan::IDevice*>(pDevice);
#endif
        if (!pVkDevice)
        {
            log::error("Vulkan Device is Invalid.");
            return;
        }
        pVkDevice->queueWaitForSemaphore(nvrhi::CommandQueue::Graphics, reinterpret_cast<VkSemaphore>(syncObj), syncObjVal);
    }
    break;
#endif

    default:
        break;
    }
}

void SLWrapper::CleanupDLSSG() {
    if (!s_SlInitialized || !m_dlssgAvailable) {
        log::warning("SL not initialized or DLSSG not available.");
        return;
    }

    sl::Result status = slFreeResources(sl::kFeatureDLSS_G, m_ViewportHandle);
    // if we've never ran the feature on this viewport, this call may return 'eErrorInvalidParameter'
    assert(status == sl::Result::eOk || status == sl::Result::eErrorInvalidParameter || status == sl::Result::eErrorFeatureMissing);
}

bool SLWrapper::Get_DLSSG_SwapChainRecreation(bool& isTurnOn)
{
    isTurnOn = m_dlssgShoudLoad;
    auto tmp = m_dlssgTriggerswapchainRecreation;
    return tmp;
}

void SLWrapper::SetReflexConsts(const sl::ReflexOptions& reflexOptions)
{
    if (!s_SlInitialized || !m_reflexAvailable)
    {
        log::warning("SL not initialized or Reflex not available.");
        return;
    }

    successCheck(slReflexSetOptions(reflexOptions), "Reflex_Options");

    return;
}

void SLWrapper::ReflexCallback_Sleep(donut::app::DeviceManager& manager, uint32_t frameID)
{
    if (m_reflexAvailable)
    {
        successCheck(slGetNewFrameToken(m_FrameToken, &frameID), "SL_GetFrameToken");
        successCheck(slReflexSleep(*m_FrameToken), "Reflex_Sleep");
    }
}

void SLWrapper::ReflexCallback_SimStart(donut::app::DeviceManager& manager, uint32_t frameID)
{
    if (m_reflexAvailable)
    {
        sl::FrameToken* temp;
        successCheck(slGetNewFrameToken(temp, &frameID), "SL_GetFrameToken");
        successCheck(slPCLSetMarker(sl::PCLMarker::eSimulationStart, *temp), "PCL_SimStart");
    }
}

void SLWrapper::ReflexCallback_SimEnd(donut::app::DeviceManager& manager, uint32_t frameID)
{
    if (m_pclAvailable)
    {
        sl::FrameToken* temp;
        successCheck(slGetNewFrameToken(temp, &frameID), "SL_GetFrameToken");
        successCheck(slPCLSetMarker(sl::PCLMarker::eSimulationEnd, *temp), "PCL_SimEnd");
    }
}

void SLWrapper::ReflexCallback_RenderStart(donut::app::DeviceManager& manager, uint32_t frameID)
{
    if (m_pclAvailable)
    {
        sl::FrameToken* temp;
        successCheck(slGetNewFrameToken(temp, &frameID), "SL_GetFrameToken");
        successCheck(slPCLSetMarker(sl::PCLMarker::eRenderSubmitStart, *temp), "PCL_SubmitStart");
    }
}

void SLWrapper::ReflexCallback_RenderEnd(donut::app::DeviceManager& manager, uint32_t frameID)
{
    if (m_pclAvailable)
    {
        sl::FrameToken* temp;
        successCheck(slGetNewFrameToken(temp, &frameID), "SL_GetFrameToken");
        successCheck(slPCLSetMarker(sl::PCLMarker::eRenderSubmitEnd, *temp), "PCL_SubmitEnd");
    }
}

void SLWrapper::ReflexCallback_PresentStart(donut::app::DeviceManager& manager, uint32_t frameID)
{
    if (m_pclAvailable)
    {
        sl::FrameToken* temp;
        successCheck(slGetNewFrameToken(temp, &frameID), "SL_GetFrameToken");
        successCheck(slPCLSetMarker(sl::PCLMarker::ePresentStart, *temp), "PCL_PresentStart");
    }
}

void SLWrapper::ReflexCallback_PresentEnd(donut::app::DeviceManager& manager, uint32_t frameID)
{
    if (m_pclAvailable)
    {
        sl::FrameToken* temp;
        successCheck(slGetNewFrameToken(temp, &frameID), "SL_GetFrameToken");
        successCheck(slPCLSetMarker(sl::PCLMarker::ePresentEnd, *temp), "PCL_PresentEnd");
    }
}

void SLWrapper::ReflexTriggerFlash()
{
    successCheck(slPCLSetMarker(sl::PCLMarker::eTriggerFlash, *m_FrameToken), "Reflex_Flash");
}

void SLWrapper::ReflexTriggerPcPing()
{
    if (m_pclAvailable)
    {
        successCheck(slPCLSetMarker(sl::PCLMarker::ePCLatencyPing, *m_FrameToken), "PCL_PCPing");
    }
}

void SLWrapper::QueryReflexStats(bool& reflex_lowLatencyAvailable, bool& reflex_flashAvailable, std::string& stats)
{
    if (m_reflexAvailable)
    {
        sl::ReflexState state;
        successCheck(slReflexGetState(state), "Reflex_State");

        reflex_lowLatencyAvailable = state.lowLatencyAvailable;
        reflex_flashAvailable = state.flashIndicatorDriverControlled;

        auto rep = state.frameReport[63];
        if (state.latencyReportAvailable && rep.gpuRenderEndTime != 0)
        {
            auto frameID = rep.frameID;
            auto totalGameToRenderLatencyUs = rep.gpuRenderEndTime - rep.inputSampleTime;
            auto simDeltaUs = rep.simEndTime - rep.simStartTime;
            auto renderDeltaUs = rep.renderSubmitEndTime - rep.renderSubmitStartTime;
            auto presentDeltaUs = rep.presentEndTime - rep.presentStartTime;
            auto driverDeltaUs = rep.driverEndTime - rep.driverStartTime;
            auto osRenderQueueDeltaUs = rep.osRenderQueueEndTime - rep.osRenderQueueStartTime;
            auto gpuRenderDeltaUs = rep.gpuRenderEndTime - rep.gpuRenderStartTime;

            stats = "frameID: " + std::to_string(frameID);
            stats += "\ntotalGameToRenderLatencyUs: " + std::to_string(totalGameToRenderLatencyUs);
            stats += "\nsimDeltaUs: " + std::to_string(simDeltaUs);
            stats += "\nrenderDeltaUs: " + std::to_string(renderDeltaUs);
            stats += "\npresentDeltaUs: " + std::to_string(presentDeltaUs);
            stats += "\ndriverDeltaUs: " + std::to_string(driverDeltaUs);
            stats += "\nosRenderQueueDeltaUs: " + std::to_string(osRenderQueueDeltaUs);
            stats += "\ngpuRenderDeltaUs: " + std::to_string(gpuRenderDeltaUs);
        }
        else
        {
            stats = "Latency Report Unavailable";
        }
    }
}
