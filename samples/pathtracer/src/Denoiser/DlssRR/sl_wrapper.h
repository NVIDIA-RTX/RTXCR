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

#pragma once

#include <donut/core/log.h>
#include <donut/core/math/math.h>
#include <nvrhi/utils.h>

#if USE_DX12
#include <DXGI.h>
#endif

#include <sl.h>
#include <sl_hooks.h>

#include <sl_dlss.h>
#include <sl_dlss_d.h>
#include <sl_dlss_g.h>
#include <sl_reflex.h>

#include <filesystem>
#include <donut/app/AftermathCrashDump.h>

class SLWrapper
{
public:
    static constexpr int FEATURE_DEMO_APP_ID = 231313132;

private:
    static bool s_SlInitialized;

public:
    static bool InitializeStreamline(nvrhi::GraphicsAPI api, const std::vector<sl::Feature>& featuresToLoad);
    static void ShutdownStreamline();

#if USE_DX12
    static bool IsSupportedDirectXDevice(const std::vector<sl::Feature>& featuresToLoad, nvrhi::IDevice* device);
#endif

#if USE_VK
    static bool IsSupportedVulkanDevice(const std::vector<sl::Feature>& featuresToLoad, VkPhysicalDevice* vkPhysicalDevice);
#endif

    // Helper functions for converting donut vectors/matrices to sl
    template <typename T>
    static inline sl::float2 ToFloat2(T val) { return sl::float2(val.x, val.y); }
    template <typename T>
    static inline sl::float3 ToFloat3(T val) { return sl::float3(val.x, val.y, val.z); }
    template <typename T>
    static inline sl::float4 ToFloat4(T val) { return sl::float4(val.x, val.y, val.z, val.w); }
    template <typename T>
    static inline sl::float4x4 ToFloat4x4(T val) {
        sl::float4x4 res;
        res.setRow(0, ToFloat4(val[0]));
        res.setRow(1, ToFloat4(val[1]));
        res.setRow(2, ToFloat4(val[2]));
        res.setRow(3, ToFloat4(val[3]));
        return res;
    }

private:
    static nvrhi::GraphicsAPI m_GraphicsAPI;

    static sl::ViewportHandle m_ViewportHandle;
    static sl::FrameToken* m_FrameToken;

    static bool m_dlssAvailable;
    static bool m_dlssrrAvailable;

    static bool m_dlssgAvailable;
    static bool m_dlssgTriggerswapchainRecreation;
    static bool m_dlssgShoudLoad;
    static sl::DLSSGOptions m_dlssgConsts;
    static sl::DLSSGState m_dlssgSettings;

    static bool m_reflexAvailable;
    static bool m_reflexDriverFlashIndicatorEnable;

    static bool m_pclAvailable;

    static sl::Resource NvRHITextureToSL(nvrhi::TextureHandle texture, nvrhi::ResourceStates stateBits);
    static void* NvRHICommandListToNative(nvrhi::CommandListHandle commandList);

    static void setSlFeatureFlags(const sl::Feature feature);

public:
    SLWrapper(nvrhi::GraphicsAPI api)
    {
        m_GraphicsAPI = api;

        // If SL has already been initialized, call AdvanceFrame to create a frame token.
        // Otherwise, it will be the caller's responsibility to do so before using the wrapper.
        if (s_SlInitialized)
        {
            AdvanceFrame();
        }
    }

    static bool AdvanceFrame();

    static  bool SetConstants(sl::Constants& consts);

    static bool GetDLSSSettings(const sl::DLSSOptions& options, sl::DLSSOptimalSettings& outSettings);
    static bool GetDLSSRRSettings(const sl::DLSSDOptions& options, sl::DLSSDOptimalSettings& outSettings);

    static void FeatureLoad(sl::Feature feature, const bool turn_on);

    static bool TagDLSSGeneralBuffers(
        nvrhi::CommandListHandle commandList,
        dm::uint2 renderSize,
        dm::uint2 displaySize,
        nvrhi::TextureHandle motionVectors,
        nvrhi::TextureHandle depth);

    static bool TagDLSSBuffers(
        nvrhi::CommandListHandle commandList,
        dm::uint2 renderSize,
        dm::uint2 displaySize,
        nvrhi::TextureHandle inputColor,
        nvrhi::TextureHandle motionVectors,
        nvrhi::TextureHandle depth,
        bool isLinearDepth,
        nvrhi::TextureHandle exposure,
        nvrhi::TextureHandle outputColor
    );

    static bool TagDLSSRRBuffers(
        nvrhi::CommandListHandle commandList,
        dm::uint2 renderSize,
        dm::uint2 displaySize,
        nvrhi::TextureHandle inputColor,
        nvrhi::TextureHandle motionVectors,
        nvrhi::TextureHandle depth,
        nvrhi::TextureHandle diffuseAlbedo,
        nvrhi::TextureHandle specAlbedo,
        nvrhi::TextureHandle normalRoughness,
        nvrhi::TextureHandle specHitDist,
        nvrhi::TextureHandle outputColor
    );

    static bool SetDLSSOptions(const sl::DLSSOptions& options);
    static bool SetDLSSRROptions(const sl::DLSSDOptions& options);
    static void SetDLSSGOptions(const sl::DLSSGOptions& consts);
    static void QueryDLSSGState(uint64_t& estimatedVRamUsage, int& fps_multiplier, sl::DLSSGStatus& status, int& minSize, int& framesMax, void*& pFence, uint64_t& fenceValue);
    static uint64_t GetDLSSGLastFenceValue();
    static void QueueGPUWaitOnSyncObjectSet(nvrhi::IDevice* pDevice, nvrhi::CommandQueue cmdQType, void* syncObj, uint64_t syncObjVal);
    static void CleanupDLSSG();
    static bool Get_DLSSG_SwapChainRecreation(bool& isTurnOn);
    static void Set_DLSSG_SwapChainRecreation(bool on)
    {
        m_dlssgTriggerswapchainRecreation = true;
        m_dlssgShoudLoad = on;
    }
    static void Quiet_DLSSG_SwapChainRecreation() { m_dlssgTriggerswapchainRecreation = false; }

    static void SetReflexConsts(const sl::ReflexOptions& reflexOptions);
    static void QueryReflexStats(bool& reflex_lowLatencyAvailable, bool& reflex_flashAvailable, std::string& stats);
    static void ReflexCallback_Sleep(donut::app::DeviceManager& manager, uint32_t frameID);
    static void ReflexCallback_SimStart(donut::app::DeviceManager& manager, uint32_t frameID);
    static void ReflexCallback_SimEnd(donut::app::DeviceManager& manager, uint32_t frameID);
    static void ReflexCallback_RenderStart(donut::app::DeviceManager& manager, uint32_t frameID);
    static void ReflexCallback_RenderEnd(donut::app::DeviceManager& manager, uint32_t frameID);
    static void ReflexCallback_PresentStart(donut::app::DeviceManager& manager, uint32_t frameID);
    static void ReflexCallback_PresentEnd(donut::app::DeviceManager& manager, uint32_t frameID);
    static void ReflexTriggerFlash();
    static void ReflexTriggerPcPing();
    static void SetReflexFlashIndicator(bool enabled) { m_reflexDriverFlashIndicatorEnable = enabled; }
    static bool GetReflexFlashIndicatorEnable() { return m_reflexDriverFlashIndicatorEnable; }

    static bool EvaluateDLSS(nvrhi::CommandListHandle commandList);
    static bool EvaluateDLSSRR(nvrhi::CommandListHandle commandList);

    static bool IsDLSSSupported() { return m_dlssAvailable; }
    static bool IsDLSSRRSupported() { return m_dlssrrAvailable; }
    static bool IsReflexSupported() { return m_reflexAvailable; }
    static bool IsPclSupported() { return m_pclAvailable; }
    static bool IsDLSSGSupported() { return m_dlssgAvailable && IsReflexSupported() && IsPclSupported(); }
};
