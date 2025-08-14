/*
 * Copyright (c) 2024-2025, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#include "../../../donut/nvrhi/src/vulkan/vulkan-backend.h"
#include <nvrhi/nvrhi.h>

#include "SampleRenderer.h"
#include "Denoiser/DlssRR/DeviceManagerOverride/DeviceManagerOverride.h"

void vulkanDeviceFeatureInfoCallback(VkDeviceCreateInfo& info)
{
    static vk::PhysicalDeviceFeatures2 deviceFeatures;
    vk::PhysicalDeviceVulkan12Features* features12 = (vk::PhysicalDeviceVulkan12Features*)info.pNext;

    assert((VkStructureType)features12->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES);
    {
        features12->shaderBufferInt64Atomics = true;
        features12->shaderSharedInt64Atomics = true;
        features12->scalarBlockLayout = true;
    }

    deviceFeatures.features = *info.pEnabledFeatures;
    deviceFeatures.features.shaderInt64 = true;
    deviceFeatures.features.shaderFloat64 = true;
    deviceFeatures.features.fragmentStoresAndAtomics = true;

    info.pEnabledFeatures = nullptr;
    deviceFeatures.pNext = (void*)info.pNext;
    info.pNext = &deviceFeatures;
}

void ParseCommandLine(int argc, const char* const* argv, donut::app::DeviceCreationParameters& deviceParams)
{
    // Default resolution
    deviceParams.backBufferWidth = 1920;
    deviceParams.backBufferHeight = 1080;

    for (int n = 1; n < __argc; n++)
    {
        const char* arg = __argv[n];

        if (!strcmp(arg, "-borderless"))
        {
            deviceParams.startBorderless = true;
        }
        else if (!strcmp(arg, "-fullscreen"))
        {
            deviceParams.startFullscreen = true;
        }
        else if (!strcmp(arg, "-2160p"))
        {
            deviceParams.backBufferWidth = 3840;
            deviceParams.backBufferHeight = 2160;
        }
        else if (!strcmp(arg, "-1440p"))
        {
            deviceParams.backBufferWidth = 2560;
            deviceParams.backBufferHeight = 1440;
        }
        else if (!strcmp(arg, "-1080p"))
        {
            deviceParams.backBufferWidth = 1920;
            deviceParams.backBufferHeight = 1080;
        }
    }
}

#ifdef WIN32
int APIENTRY WinMain(_In_     HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_     LPSTR     lpCmdLine,
                     _In_     int       nCmdShow)
#else
int main(int __argc, const char** __argv)
#endif
{
    const nvrhi::GraphicsAPI api = donut::app::GetGraphicsAPIFromCommandLine(__argc, __argv);

    // We don't support D3D11 in this sample
    if (api == nvrhi::GraphicsAPI::D3D11)
    {
        donut::log::fatal("D3D11 is not supported by RTXCR SDK sample");
        return 1;
    }

    donut::app::DeviceCreationParameters deviceParams = {};
    deviceParams.enableRayTracingExtensions = true;
    deviceParams.enablePerMonitorDPI = true;
    deviceParams.allowModeSwitch = false;
#ifdef _DEBUG
    // TODO: Enable runtime debugging for Vulkan, currently we find some issues when insert VK_LAYER_KHRONOS_validation
    deviceParams.enableDebugRuntime = (api == nvrhi::GraphicsAPI::D3D12);
    deviceParams.enableNvrhiValidationLayer = (api == nvrhi::GraphicsAPI::D3D12);
    deviceParams.enablePerMonitorDPI = true;
    deviceParams.allowModeSwitch = false;
#endif

    ParseCommandLine(__argc, __argv, deviceParams);

    // Initialize Streamline
    const std::vector<sl::Feature> slFeaturesToLoad = {
        sl::kFeatureDLSS,
        sl::kFeatureDLSS_RR,
        sl::kFeatureDLSS_G,
        sl::kFeatureReflex,
        sl::kFeaturePCL,
    };


    {
        if (!SLWrapper::InitializeStreamline(api, slFeaturesToLoad))
        {
            donut::log::warning("Cannot initialize SL");
            return 1;
        }
        SLWrapper m_SL(api);

#if USE_VK
        if (api == nvrhi::GraphicsAPI::VULKAN)
        {
#if USE_VK_STREAMLINE
            deviceParams.vulkanLibraryName = "sl.interposer.dll";
#endif
            deviceParams.deviceCreateInfoCallback = &vulkanDeviceFeatureInfoCallback;
        }
    }
#endif

    // Use override Dx12 DeviceManager to properly setup slSetD3DDevice
    // For Vulkan, just use the native Donut DeviceManager::Create
    donut::app::DeviceManager* deviceManager =
        (api == nvrhi::GraphicsAPI::D3D12) ? CreateD3D12() : donut::app::DeviceManager::Create(api);

    if (!deviceManager->CreateWindowDeviceAndSwapChain(deviceParams, g_WindowTitle))
    {
        donut::log::warning("Cannot initialize a graphics device with the requested parameters");
        return 1;
    }

    if (!deviceManager->GetDevice()->queryFeatureSupport(nvrhi::Feature::RayTracingPipeline))
    {
        donut::log::warning("The graphics device does not support Ray Tracing Pipelines");
        return 1;
    }

#if USE_DX12
    if (api == nvrhi::GraphicsAPI::D3D12)
    {
        if (!SLWrapper::IsSupportedDirectXDevice(slFeaturesToLoad, deviceManager->GetDevice()))
        {
            donut::log::error("SL Not support Dx12 Device");
            return 1;
        }
    }
#endif

#if USE_VK
    if (api == nvrhi::GraphicsAPI::VULKAN &&
        !SLWrapper::IsSupportedVulkanDevice(slFeaturesToLoad, deviceManager->GetDevice()->getNativeObject(nvrhi::ObjectTypes::VK_PhysicalDevice)))
    {
        donut::log::error("SL Not support VK Device");
        return 1;
    }
#endif

    {
        UIData uiData;
        SampleRenderer renderer(deviceManager, uiData);

        if (renderer.Init(__argc, __argv))
        {
            PathtracerUI gui(deviceManager, renderer, uiData);
            gui.Init(renderer.GetShaderFactory());

            deviceManager->AddRenderPassToBack(&renderer);
            deviceManager->AddRenderPassToBack(&gui);

            deviceManager->RunMessageLoop();

            deviceManager->RemoveRenderPass(&gui);
            deviceManager->RemoveRenderPass(&renderer);
        }
    }

    SLWrapper::ShutdownStreamline();
    deviceManager->Shutdown();

    delete deviceManager;

    return 0;
}
