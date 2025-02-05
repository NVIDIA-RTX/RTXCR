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
#include <sl_consts.h>
#include <sl_hooks.h>
#include <sl_version.h>

#include <sl_dlss.h>
#include <sl_dlss_d.h>

#include <filesystem>

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
    static bool GetSupportedDXGIAdapter(const std::vector<sl::Feature>& featuresToLoad, IDXGIAdapter** outAdapter);
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
    nvrhi::GraphicsAPI m_GraphicsAPI;

    sl::ViewportHandle m_ViewportHandle;
    sl::FrameToken* m_FrameToken = nullptr;

    sl::Resource NvRHITextureToSL(nvrhi::TextureHandle texture, nvrhi::ResourceStates stateBits);
    void* NvRHICommandListToNative(nvrhi::CommandListHandle commandList);

public:
    SLWrapper(nvrhi::GraphicsAPI api)
        : m_GraphicsAPI(api)
        , m_ViewportHandle(0)
    {
        // If SL has already been initialized, call AdvanceFrame to create a frame token.
        // Otherwise, it will be the caller's responsibility to do so before using the wrapper.
        if (s_SlInitialized) {
            AdvanceFrame();
        }
    }

    bool AdvanceFrame();

    bool SetConstants(sl::Constants& consts);

    bool GetDLSSSettings(const sl::DLSSOptions& options, sl::DLSSOptimalSettings& outSettings);
    bool GetDLSSRRSettings(const sl::DLSSDOptions& options, sl::DLSSDOptimalSettings& outSettings);

    bool TagDLSSBuffers(
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

    bool TagDLSSRRBuffers(
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

    bool SetDLSSOptions(const sl::DLSSOptions& options);
    bool SetDLSSRROptions(const sl::DLSSDOptions& options);

    bool EvaluateDLSS(nvrhi::CommandListHandle commandList);
    bool EvaluateDLSSRR(nvrhi::CommandListHandle commandList);

};
