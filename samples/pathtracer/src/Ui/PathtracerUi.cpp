/*
 * Copyright (c) 2024-2025, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#include <donut/core/math/math.h>
#include <donut/app/ApplicationBase.h>
#include <donut/app/UserInterfaceUtils.h>
#include <donut/engine/Scene.h>
#include <donut/engine/TextureCache.h>

#include <imgui_internal.h>

#include "../SampleRenderer.h"

using namespace donut::app;
using namespace donut::engine;
using namespace donut::math;

namespace
{
    // Conversion between sRGB to linear color space
    // Required here because of a known bug with ImGui and sRGB framebuffer
    float srgb_to_linear(float value)
    {
        if (value <= 0.04045f)
            return value / 12.92f;
        else
            return powf((value + 0.055f) / 1.055f, 2.4f);
    }

    void color_correction(ImVec4& color)
    {
        color.x = srgb_to_linear(color.x);
        color.y = srgb_to_linear(color.y);
        color.z = srgb_to_linear(color.z);
    }

    inline ImVec2 MakeImVec2(const dm::float2& v)
    {
        return ImVec2{ v.x, v.y };
    }

    inline ImVec2 MakeImVec2(const dm::int2& v)
    {
        return ImVec2{ float(v.x), float(v.y) };
    }

    inline dm::float2 MakeFloat2(const ImVec2& v)
    {
        return dm::float2{ v.x, v.y };
    }


    void SetConstrainedWindowPos(const char* windowName, ImVec2 windowPos, const ImVec2& windowPivot, const ImVec2& screenSize)
    {
        ImGuiCond cond = ImGuiCond_FirstUseEver;
        ImGuiWindow* window = ImGui::FindWindowByName(windowName);

        // Bound the window position to be on screen by a margin
        const float kMinOnscreenLength = 20.0f;
        if (window)
        {
            const dm::float2 kMinOnscreenSize = { kMinOnscreenLength, kMinOnscreenLength };
            dm::float2 currentWindowPos = MakeFloat2(window->Pos);
            dm::float2 currentWindowSize = MakeFloat2(window->Size);
            dm::box2 windowRect{ currentWindowPos, currentWindowPos + currentWindowSize };
            dm::box2 screenLayoutRect{ kMinOnscreenSize, MakeFloat2(screenSize) - kMinOnscreenSize };

            if (!screenLayoutRect.intersects(windowRect))
            {
                cond = ImGuiCond_Always;
                dm::float2 minCornerAdjustment = -min(windowRect.m_maxs - screenLayoutRect.m_mins, dm::float2::zero());
                dm::float2 maxCornerAdjustment = -max(windowRect.m_mins - screenLayoutRect.m_maxs, dm::float2::zero());
                dm::float2 adjustment = minCornerAdjustment + maxCornerAdjustment;
                windowRect = windowRect.translate(adjustment);

                windowPos = MakeImVec2(windowRect.m_mins + MakeFloat2(windowPivot) * currentWindowSize);
            }
        }
        ImGui::SetNextWindowPos(windowPos, cond, windowPivot);
    }
}

PathtracerUI::PathtracerUI(DeviceManager* deviceManager, SampleRenderer& app, UIData& ui)
    : ImGui_Renderer(deviceManager)
    , m_app(app)
    , m_ui(ui)
{
    m_commandList = GetDevice()->createCommandList();

    // Check adapter memory
    {
        std::vector<donut::app::AdapterInfo> adapters;
        GetDeviceManager()->EnumerateAdapters(adapters);
        for (const auto& adapter : adapters)
        {
            m_adapterMemoryInGigaBytes = std::max(m_adapterMemoryInGigaBytes, adapter.dedicatedVideoMemory);
        }
        constexpr uint64_t gigabyte = 1073741824;
        m_adapterMemoryInGigaBytes /= gigabyte;
    }

    float scaleX, scaleY;
    GetDeviceManager()->GetDPIScaleInfo(scaleX, scaleY);

    float fontSize = 25.0f;
    if (scaleX <= 1.0f)
    {
        fontSize = 25.0f;
    }
    else if (scaleX > 1.0f && scaleX <= 1.51f)
    {
        fontSize = 16.0f;
    }
    else if (scaleX <= 2.51f)
    {
        fontSize = 11.0f;
    }
    else
    {
        fontSize = 9.0f;
    }

    m_fontOpenSans = CreateFontFromFile(*(app.GetRootFS()), "/assets/fonts/OpenSans/OpenSans-Regular.ttf", fontSize);
    m_fontDroidMono = CreateFontFromFile(*(app.GetRootFS()), "/assets/fonts/DroidSans/DroidSans-Mono.ttf", 14.0f);

    ImGui_Console::Options opts;
    opts.font = m_fontDroidMono;

    ImGui::GetIO().IniFilename = nullptr;
}

void PathtracerUI::buildUI(void)
{
    if (!m_ui.showUI)
    {
        return;
    }

    const auto& io = ImGui::GetIO();

    int width, height;
    GetDeviceManager()->GetWindowDimensions(width, height);
    float scaleX, scaleY;
    GetDeviceManager()->GetDPIScaleInfo(scaleX, scaleY);

    const float layoutToDisplay = std::min(scaleX, scaleY);
    const float contentScale = layoutToDisplay > 0.f ? (1.0f / layoutToDisplay) : 1.0f;

    // Layout is done at lower resolution than scaled up virtually past the render target m_size
    // any element beyond this range is clipped.
    float widthScale = 0.4f;
    if (scaleX > 2.5f || (width < 1920.0f && width >= 1080.0f))
    {
        widthScale = 0.6f;
    }
    else if (width > 1920.0f && width <= 2560.0f)
    {
        widthScale = 0.5f;
    }
    else if (width > 2560.0f || width < 1080.0f)
    {
        widthScale = 1.0f;
    }
    width = int(width * contentScale) * widthScale;
    height = int(height * contentScale);

    if (m_app.IsSceneLoading())
    {
        BeginFullScreenWindow();
        ImGui::PushFont(m_fontOpenSans->GetScaledFont());

        char messageBuffer[256];
        const auto& stats = Scene::GetLoadingStats();
        snprintf(messageBuffer, std::size(messageBuffer), "Loading scene %s, please wait...\nObjects: %d/%d, Textures: %d/%d",
            m_app.GetScene()->GetCurrentSceneName().c_str(), stats.ObjectsLoaded.load(), stats.ObjectsTotal.load(), m_app.GetTextureCache()->GetNumberOfLoadedTextures(), m_app.GetTextureCache()->GetNumberOfRequestedTextures());

        DrawScreenCenteredText(messageBuffer);

        EndFullScreenWindow();

        return;
    }

    ImGui::PushFont(m_fontOpenSans->GetScaledFont());

    bool updateAccum = false;
    bool updateAccelerationStructure = false;

    auto refreshScene = [this](const std::string& scene) -> void
    {
        m_app.SetCurrentSceneNameAndLoading(scene);
        m_showRefreshSceneRemindText = false;
    };

    const char* kWindowName = "Settings";
    const dm::int2 screenLayoutSize(width, height);
    SetConstrainedWindowPos(kWindowName, ImVec2(10, 10), ImVec2(0.0f, 0.0f), MakeImVec2(screenLayoutSize));
    ImGui::SetNextWindowSize(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSizeConstraints(ImVec2(100.f, 200.f), MakeImVec2(screenLayoutSize));

    ImGui::Begin(kWindowName, 0, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove);
    ImGui::SetWindowPos(ImVec2(1.0f, 1.0f));

    ImGui::StyleColorsDark();
    ImGuiStyle* style = &ImGui::GetStyle();
    ImVec4* colors = style->Colors;
    {
        color_correction(colors[ImGuiCol_Text]);
        color_correction(colors[ImGuiCol_TextDisabled]);
        color_correction(colors[ImGuiCol_WindowBg]);
        color_correction(colors[ImGuiCol_ChildBg]);
        color_correction(colors[ImGuiCol_PopupBg]);
        color_correction(colors[ImGuiCol_Border]);
        color_correction(colors[ImGuiCol_BorderShadow]);
        color_correction(colors[ImGuiCol_FrameBg]);
        color_correction(colors[ImGuiCol_FrameBgHovered]);
        color_correction(colors[ImGuiCol_FrameBgActive]);
        color_correction(colors[ImGuiCol_TitleBg]);
        color_correction(colors[ImGuiCol_TitleBgActive]);
        color_correction(colors[ImGuiCol_TitleBgCollapsed]);
        color_correction(colors[ImGuiCol_MenuBarBg]);
        color_correction(colors[ImGuiCol_ScrollbarBg]);
        color_correction(colors[ImGuiCol_ScrollbarGrab]);
        color_correction(colors[ImGuiCol_ScrollbarGrabHovered]);
        color_correction(colors[ImGuiCol_ScrollbarGrabActive]);
        color_correction(colors[ImGuiCol_CheckMark]);
        color_correction(colors[ImGuiCol_SliderGrab]);
        color_correction(colors[ImGuiCol_SliderGrabActive]);
        color_correction(colors[ImGuiCol_Button]);
        color_correction(colors[ImGuiCol_ButtonHovered]);
        color_correction(colors[ImGuiCol_ButtonActive]);
        color_correction(colors[ImGuiCol_Header]);
        color_correction(colors[ImGuiCol_HeaderHovered]);
        color_correction(colors[ImGuiCol_HeaderActive]);
        color_correction(colors[ImGuiCol_Separator]);
        color_correction(colors[ImGuiCol_SeparatorHovered]);
        color_correction(colors[ImGuiCol_SeparatorActive]);
        color_correction(colors[ImGuiCol_ResizeGrip]);
        color_correction(colors[ImGuiCol_ResizeGripHovered]);
        color_correction(colors[ImGuiCol_ResizeGripActive]);
        color_correction(colors[ImGuiCol_Tab]);
        color_correction(colors[ImGuiCol_TabHovered]);
        color_correction(colors[ImGuiCol_TabActive]);
        color_correction(colors[ImGuiCol_TabUnfocused]);
        color_correction(colors[ImGuiCol_TabUnfocusedActive]);
        color_correction(colors[ImGuiCol_PlotLines]);
        color_correction(colors[ImGuiCol_PlotLinesHovered]);
        color_correction(colors[ImGuiCol_PlotHistogram]);
        color_correction(colors[ImGuiCol_PlotHistogramHovered]);
        color_correction(colors[ImGuiCol_TableHeaderBg]);
        color_correction(colors[ImGuiCol_TableBorderStrong]);   // Prefer using Alpha=1.0 here
        color_correction(colors[ImGuiCol_TableBorderLight]);   // Prefer using Alpha=1.0 here
        color_correction(colors[ImGuiCol_TableRowBg]);
        color_correction(colors[ImGuiCol_TableRowBgAlt]);
        color_correction(colors[ImGuiCol_TextSelectedBg]);
        color_correction(colors[ImGuiCol_DragDropTarget]);
        color_correction(colors[ImGuiCol_NavHighlight]);
        color_correction(colors[ImGuiCol_NavWindowingHighlight]);
        color_correction(colors[ImGuiCol_NavWindowingDimBg]);
        color_correction(colors[ImGuiCol_ModalWindowDimBg]);
    }

    ImGui::Text("%s, %s", GetDeviceManager()->GetRendererString(), m_app.GetResolutionInfo().c_str());
    const double frameTime = GetDeviceManager()->GetAverageFrameTimeSeconds() / (double)std::max(m_ui.dlfgNumFramesActuallyPresented, 1);
    if (frameTime > 0.0)
    {
        ImGui::Text("%.3f ms/frame (%.1f FPS)", frameTime * 1e3, 1.0 / frameTime);
    }

    ImGui::Separator();
    if (ImGui::CollapsingHeader("Generic:", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Indent(12.0f);
        {
#ifdef _DEBUG
            const float3 cameraPosition = m_app.GetCamera()->GetPosition();
            const float3 cameraDirection = m_app.GetCamera()->GetDir();
            ImGui::Text("Camera (%0.2f, %0.2f, %0.2f)", cameraPosition.x, cameraPosition.y, cameraPosition.z);
            ImGui::Text("Camera Direction (%0.2f, %0.2f, %0.2f)", cameraDirection.x, cameraDirection.y, cameraDirection.z);
#endif
            ImGui::SliderFloat("Camera Speed", &m_ui.cameraSpeed, 0.01f, 200.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
            ImGui::Checkbox("Lock Camera", &m_ui.lockCamera);

            const std::string currentSceneFullPath = m_app.GetScene()->GetCurrentSceneName();
            const std::string currentScene = currentSceneFullPath.substr(currentSceneFullPath.find_last_of("/\\") + 1);
            if (ImGui::BeginCombo("Scene", currentScene.c_str()))
            {
                const std::vector<std::string>& scenes = m_app.GetScene()->GetAvailableScenes();
                for (const std::string& scene : scenes)
                {
                    const bool is_selected = scene == currentSceneFullPath;
                    const std::string sceneStr = scene.substr(scene.find_last_of("/\\") + 1);
                    if (ImGui::Selectable(sceneStr.c_str(), is_selected))
                    {
                        refreshScene(scene);

                        m_selectedLight = nullptr;
                        m_selectedLightIndex = 0;
                    }

                    if (is_selected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }

            if (ImGui::Button("Refresh Scene"))
            {
                refreshScene(currentSceneFullPath);

                const auto& lights = m_app.GetScene()->GetNativeScene()->GetSceneGraph()->GetLights();
                m_selectedLight = lights[m_selectedLightIndex];
            }

            updateAccum |= ImGui::Checkbox("Back Face Culling", &m_ui.enableBackFaceCull); ImGui::SameLine();
            updateAccum |= ImGui::Checkbox("Enable Soft Shadows", &m_ui.enableSoftShadows);

#ifdef _DEBUG
            if (ImGui::BeginTable("Transmission_Jitter_Mode_Table", 2)) {
                ImGui::TableNextColumn();
                updateAccum |= ImGui::Checkbox("Transmission", &m_ui.enableTransmission);
                ImGui::TableNextColumn();
                updateAccum |= ImGui::Combo("Jitter Mode", (int*)&m_ui.jitterMode, m_ui.jitterModeStrings);
                ImGui::EndTable();
            }
#endif

            if (ImGui::Button("Recompile Shader"))
            {
                m_ui.recompileShader = true;
            }

            ImGui::Text("Name:");
            ImGui::SameLine();
            ImGui::InputText(" ", m_ui.screenshotName, m_ui.kBufSize, ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::SameLine();
            if (ImGui::Button("Capture"))
            {
                m_ui.captureScreenshot = true;
            }

            if (m_ui.denoiserSelection != DenoiserSelection::Reference)
            {
                ImGui::Separator();
                if (ImGui::CollapsingHeader("DLSS:", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    ImGui::Indent(12.0f);
                    if (SLWrapper::IsDLSSGSupported())
                    {
                        if (ImGui::CollapsingHeader("DLFG:", ImGuiTreeNodeFlags_DefaultOpen))
                        {
                            ImGui::Indent(12.0f);
                            updateAccum |= ImGui::Checkbox("Enable DLSS Frame Generation (DLFG)", &m_ui.enableDlfg);
                            if (m_ui.enableDlfg)
                            {
                                ImGui::Text("Generated Frames");
                                if (m_ui.dlfgMaxNumFramesToGenerate > 1)
                                {
                                    ImGui::SameLine();
                                    ImGui::SliderInt("##MultiframeCount", &m_ui.dlfgNumFramesToGenerate, 2, m_ui.dlfgMaxNumFramesToGenerate + 1, "%dx", ImGuiSliderFlags_AlwaysClamp);
                                }
                            }
                            ImGui::Indent(-12.0f);
                        }
                    }
                    else
                    {
                        m_ui.enableDlfg = false;
                        if (SLWrapper::IsDLSSSupported())
                        {
                            ImGui::Text("DLSS Frame Generation (DLFG) is not supported on current GPU.");
                        }
                    }

                    if (SLWrapper::IsReflexSupported())
                    {
                        if (ImGui::CollapsingHeader("Reflex:", ImGuiTreeNodeFlags_DefaultOpen))
                        {
                            ImGui::Indent(12.0f);
                            if (m_ui.enableDlfg)
                            {
                                sl::ReflexMode reflexModeDlfg = sl::ReflexMode::eOff;
                                // Reflex is required when DLFG is enabled
                                ImGui::Combo("Reflex Mode", (int*)&reflexModeDlfg, "Low Latency\0LowLatency + Boost\0");

                                m_ui.reflexMode = (sl::ReflexMode)((int)reflexModeDlfg + 1);
                            }
                            else
                            {
                                ImGui::Combo("Reflex Mode", (int*)&m_ui.reflexMode, m_ui.reflexSelectionStrings);
                            }
                            ImGui::Indent(-12.0f);
                        }
                    }
                    else
                    {
                        if (SLWrapper::IsDLSSSupported())
                        {
                            ImGui::Text("Reflex is not supported on current GPU.");
                        }
                    }
                    ImGui::Indent(-12.0f);
                }
            }
        }
        ImGui::Indent(-12.0f);
    }

    ImGui::Separator();
    if (ImGui::CollapsingHeader("Path Tracing:", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Indent(12.0f);
        {
#ifdef _DEBUG
            updateAccum |= ImGui::Checkbox("Enable Random", &m_ui.enableRandom);
#endif
            updateAccum |= ImGui::SliderInt("Bounces", &m_ui.bouncesMax, 1, 8);
            updateAccum |= ImGui::SliderFloat("Exposure Adjustment", &m_ui.exposureAdjustment, -8.f, 8.0f);

            // Debug views
            updateAccum |= ImGui::Combo("Debug Output", (int*)&m_ui.debugOutput, m_ui.debugOutputTypeStrings);
            if (m_ui.debugOutput == RtxcrDebugOutputType::WhiteFurnace)
            {
                updateAccum |= ImGui::SliderInt("White Furnace Test Sample Count", &m_ui.whiteFurnaceSampleCount, 1, 100000);
            }
            else if (m_ui.debugOutput == RtxcrDebugOutputType::ViewSpaceZ ||
                     m_ui.debugOutput == RtxcrDebugOutputType::DeviceZ ||
                     m_ui.debugOutput == RtxcrDebugOutputType::DiffuseHitT ||
                     m_ui.debugOutput == RtxcrDebugOutputType::SpecularHitT)
            {
                updateAccum |= ImGui::SliderFloat("Debug Scale", &m_ui.debugScale, 1.0f / TRACING_FAR_DISTANCE, TRACING_FAR_DISTANCE, "%.6f", ImGuiSliderFlags_Logarithmic);
                updateAccum |= ImGui::SliderFloat2("Debug Min/Max", m_ui.debugMinMax, 0.0f, TRACING_FAR_DISTANCE, "%.2f", ImGuiSliderFlags_Logarithmic);
            }

            updateAccum |= updateAccelerationStructure;
        }
        ImGui::Indent(-12.0f);
    }

    ImGui::Separator();
    if (ImGui::CollapsingHeader("Denoiser:", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Indent(12.0f);
        {
            auto addDlssUpscalerOptions = [&]() -> void {
                if (SLWrapper::IsDLSSSupported())
                {
                    updateAccum |= ImGui::Combo("Upscaler", (int*)&m_ui.upscalerSelection, m_ui.upscalerSelectionStrings);

                    if (m_ui.upscalerSelection == UpscalerSelection::DLSS)
                    {
                        int dlssQualityMode = 0;
                        switch (m_ui.dlsssrQualityMode)
                        {
                            case sl::DLSSMode::eUltraPerformance: dlssQualityMode = 0; break;
                            case sl::DLSSMode::eMaxPerformance:   dlssQualityMode = 1; break;
                            case sl::DLSSMode::eBalanced:         dlssQualityMode = 2; break;
                            case sl::DLSSMode::eMaxQuality:       dlssQualityMode = 3; break;
                            case sl::DLSSMode::eDLAA:             dlssQualityMode = 4; break;
                        }
                        ImGui::Combo("DLSS Quality", &dlssQualityMode, "UltraPerformance\0Performance\0Balanced\0Quality\0DLAA\0");
                        switch (dlssQualityMode)
                        {
                            case 0: m_ui.dlsssrQualityMode = sl::DLSSMode::eUltraPerformance; break;
                            case 1: m_ui.dlsssrQualityMode = sl::DLSSMode::eMaxPerformance; break;
                            case 2: m_ui.dlsssrQualityMode = sl::DLSSMode::eBalanced; break;
                            case 3: m_ui.dlsssrQualityMode = sl::DLSSMode::eMaxQuality; break;
                            case 4: m_ui.dlsssrQualityMode = sl::DLSSMode::eDLAA; break;
                        }
                    }
                }
                else
                {
                    updateAccum |= ImGui::Combo("Upscaler", (int*)&m_ui.upscalerSelection, "None\0TAA\0");
                }
            };

            if (SLWrapper::IsDLSSSupported())
            {
                updateAccum |= ImGui::Combo("Tech", (int*)&m_ui.denoiserSelection, m_ui.denoiserSelectionStrings);
            }
            else
            {
                // Fallback GUI when DLSS is NOT supported
                static DenoiserSelection nonNvDenoiserSelection = DenoiserSelection::Nrd;
                updateAccum |= ImGui::Combo("Tech", (int*)&nonNvDenoiserSelection, "None\0NRD\0Reference\0");
                m_ui.denoiserSelection = (nonNvDenoiserSelection == DenoiserSelection::DlssRr) ? DenoiserSelection::Reference : nonNvDenoiserSelection;
            }

            switch (m_ui.denoiserSelection)
            {
                case DenoiserSelection::None:
                {
                    m_ui.enableDenoiser = false;
                    m_ui.enableAccumulation = false;
                    addDlssUpscalerOptions();
                    if (m_prevDenoiserSelection != DenoiserSelection::None)
                    {
                        if (m_ui.upscalerSelection == UpscalerSelection::TAA)
                        {
                            m_ui.upscalerSelection = SLWrapper::IsDLSSSupported() ? UpscalerSelection::DLSS : UpscalerSelection::None;
                        }
                    }
                    break;
                }
                case DenoiserSelection::Nrd:
                {
                    m_ui.enableDenoiser = true;
                    m_ui.enableAccumulation = false;

                    updateAccum |= ImGui::Combo("NRD Mode", (int*)&m_ui.nrdDenoiserMode, m_ui.nrdModeStrings);
                    addDlssUpscalerOptions();
                    if (m_prevDenoiserSelection != DenoiserSelection::Nrd)
                    {
                        if (m_ui.upscalerSelection == UpscalerSelection::None)
                        {
                            m_ui.upscalerSelection = SLWrapper::IsDLSSSupported() ? UpscalerSelection::DLSS : UpscalerSelection::TAA;
                        }
                        m_ui.enableDlfg = m_prevNrdDlfgEnabled;
                    }

                    if (ImGui::Button("Reset Denoiser"))
                    {
                        m_ui.forceResetDenoiser = true;
                    }

                    if (ImGui::CollapsingHeader("Common Settings"))
                    {
                        ImGui::Indent(12.0f);
                        ImGui::SliderFloat("Disocclusion threshold", &m_ui.nrdCommonSettings.disocclusionThreshold, 0.01f, 0.02f, "%.3f", ImGuiSliderFlags_Logarithmic);
                        ImGui::SliderFloat("Disocclusion threshold alternate", &m_ui.nrdCommonSettings.disocclusionThresholdAlternate, 0.02f, 0.2f, "%.3f", ImGuiSliderFlags_Logarithmic);
                        // TODO: Implement Disocclusion threshold mix
                        // ImGui::Checkbox("Disocclusion threshold mix", &m_ui.nrdCommonSettings.isDisocclusionThresholdMixAvailable);
#ifdef _DEBUG
                        ImGui::Checkbox("Validation", &m_ui.nrdCommonSettings.enableValidation);
#endif
                        ImGui::Unindent(12.0f);
                    }

                    static const char* const checkerboardMode[] =
                    {
                        "Off",
                        "Black",
                        "White",
                    };

                    static const char* const hitDistanceReconstructionMode[] =
                    {
                        "Off",
                        "3x3",
                        "5x5",
                    };

                    switch (m_ui.nrdDenoiserMode)
                    {
                        case NrdMode::Reblur:
                        {
                            if (ImGui::CollapsingHeader("Reblur Settings"))
                            {
                                ImGui::Indent(12.0f);

                                ImGui::SliderInt("History length (frames)", (int*)&m_ui.reblurSettings.maxAccumulatedFrameNum, 0, nrd::REBLUR_MAX_HISTORY_FRAME_NUM, "%d");
                                ImGui::SliderInt("Fast history length (frames)", (int*)&m_ui.reblurSettings.maxFastAccumulatedFrameNum, 0, nrd::REBLUR_MAX_HISTORY_FRAME_NUM, "%d");
                                ImGui::SliderInt("History fix (frames)", (int*)&m_ui.reblurSettings.historyFixFrameNum, 0, 3);
                                ImGui::SliderFloat2("Pre-pass blur radius (px)", &m_ui.reblurSettings.diffusePrepassBlurRadius, 0.0f, 100.0f, "%.1f");
                                ImGui::SliderFloat("Min blur radius (px)", &m_ui.reblurSettings.minBlurRadius, 0.0f, 100.0f, "%.1f");
                                ImGui::SliderFloat("Max blur radius (px)", &m_ui.reblurSettings.maxBlurRadius, 0.0f, 100.0f, "%.1f");
                                ImGui::SliderFloat("Lobe angle fraction", &m_ui.reblurSettings.lobeAngleFraction, 0.0f, 1.0f, "%.2f");
                                ImGui::SliderFloat("Roughness fraction", &m_ui.reblurSettings.roughnessFraction, 0.0f, 1.0f, "%.2f");
                                ImGui::SliderFloat("Responsive accumulation roughness", &m_ui.reblurSettings.responsiveAccumulationRoughnessThreshold, 0.0f, 1.0f, "%.2f");
                                ImGui::SliderFloat("Plane distance sensitivity", &m_ui.reblurSettings.planeDistanceSensitivity, 0.0f, 1.0f, "%.3f");
                                ImGui::SliderFloat2("Specular MV modification", m_ui.reblurSettings.specularProbabilityThresholdsForMvModification, 0.0f, 1.0f, "%.1f");
                                if (m_ui.reblurSettings.enableAntiFirefly)
                                {
                                    ImGui::SliderFloat("Fire Fly Suppressor Min Relative Scale (%)", &m_ui.reblurSettings.fireflySuppressorMinRelativeScale, 1.0f, 3.0f, "%.2f");
                                }
                                {
                                    int v = (int)m_ui.reblurSettings.checkerboardMode;
                                    ImGui::Combo("Checkerboard mode", &v, checkerboardMode, 3);
                                    m_ui.reblurSettings.checkerboardMode = (nrd::CheckerboardMode)v;
                                }
                                {
                                    int v = (int)m_ui.reblurSettings.hitDistanceReconstructionMode;
                                    ImGui::Combo("HitT reconstruction mode", &v, hitDistanceReconstructionMode, 3);
                                    m_ui.reblurSettings.hitDistanceReconstructionMode = (nrd::HitDistanceReconstructionMode)v;
                                }

                                if (ImGui::CollapsingHeader("Hit Distance"))
                                {
                                    ImGui::Indent(12.0f);
                                    ImGui::SliderFloat("Constant Value", &m_ui.reblurSettings.hitDistanceParameters.A, 0.0f, 1000.0f, "%.1f");
                                    ImGui::SliderFloat("ViewZ Based Linear Scale", &m_ui.reblurSettings.hitDistanceParameters.B, 0.0001f, 1000.0f, "%.1f");
                                    ImGui::SliderFloat("Roughness Based Scale", &m_ui.reblurSettings.hitDistanceParameters.C, 1.0f, 1000.0f, "%.1f");
                                    ImGui::SliderFloat("Absolute Value", &m_ui.reblurSettings.hitDistanceParameters.D, -1000.0f, 0.0f, "%.1f");
                                    ImGui::Unindent(12.0f);
                                }

                                if (ImGui::CollapsingHeader("Antilag"))
                                {
                                    ImGui::Indent(12.0f);
                                    ImGui::SliderFloat2("Sigma scale", &m_ui.reblurSettings.antilagSettings.luminanceSigmaScale, 1.0f, 3.0f, "%.1f");
                                    ImGui::SliderFloat2("Power", &m_ui.reblurSettings.antilagSettings.luminanceSensitivity, 1.0f, 3.0f, "%.2f");
                                    ImGui::Unindent(12.0f);
                                }

                                ImGui::Checkbox("Anti-firefly", &m_ui.reblurSettings.enableAntiFirefly);
                                ImGui::Checkbox("Performance mode", &m_ui.reblurSettings.enablePerformanceMode);
                                ImGui::Checkbox("Pre-pass only for specular motion estimation", &m_ui.reblurSettings.usePrepassOnlyForSpecularMotionEstimation);

                                ImGui::Unindent(12.0f);
                            }

                            break;
                        }
                        case NrdMode::Relax:
                        {
                            if (ImGui::CollapsingHeader("Relax Settings"))
                            {
                                ImGui::Indent(12.0f);

                                ImGui::SliderFloat2("Pre-pass diffuse blur radius (px)", &m_ui.relaxSettings.diffusePrepassBlurRadius, 0.0f, 100.0f, "%.1f");
                                ImGui::SliderFloat2("Pre-pass specular blur radius (px)", &m_ui.relaxSettings.specularPrepassBlurRadius, 0.0f, 100.0f, "%.1f");
                                ImGui::SliderInt2("Diffuse history length (frames)", (int*)&m_ui.relaxSettings.diffuseMaxAccumulatedFrameNum, 0, nrd::RELAX_MAX_HISTORY_FRAME_NUM, "%d");
                                ImGui::SliderInt2("Specular history length (frames)", (int*)&m_ui.relaxSettings.specularMaxAccumulatedFrameNum, 0, nrd::RELAX_MAX_HISTORY_FRAME_NUM, "%d");
                                ImGui::SliderInt("History fix (frames)", (int*)&m_ui.relaxSettings.historyFixFrameNum, 0, 3);
                                ImGui::SliderFloat2("Diffuse phi luminance", &m_ui.relaxSettings.diffusePhiLuminance, 0.0f, 10.0f, "%.1f");
                                ImGui::SliderFloat2("Specular phi luminance", &m_ui.relaxSettings.specularPhiLuminance, 0.0f, 10.0f, "%.1f");
                                ImGui::SliderFloat2("Lobe angle fraction", &m_ui.relaxSettings.lobeAngleFraction, 0.0f, 1.0f, "%.2f");
                                ImGui::SliderFloat("Roughness fraction", &m_ui.relaxSettings.roughnessFraction, 0.0f, 1.0f, "%.2f");
                                ImGui::SliderFloat("Specular variance boost", &m_ui.relaxSettings.specularVarianceBoost, 0.0f, 8.0f, "%.2f");
                                ImGui::SliderFloat("Specular lobe angle slack", &m_ui.relaxSettings.specularLobeAngleSlack, 0.0f, 89.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
                                ImGui::SliderFloat("History fix normal power", &m_ui.relaxSettings.historyFixEdgeStoppingNormalPower, 0.0f, 128.0f, "%.1f");
                                ImGui::SliderFloat("History lamping sigma scale", &m_ui.relaxSettings.historyClampingColorBoxSigmaScale, 0.0f, 10.0f, "%.1f");
                                ImGui::SliderInt("Spatial variance history (frames)", (int*)&m_ui.relaxSettings.spatialVarianceEstimationHistoryThreshold, 0, 10);
                                ImGui::SliderInt("A-trous iterations", (int*)&m_ui.relaxSettings.atrousIterationNum, 2, 8);
                                ImGui::SliderFloat2("Min luminance weight", &m_ui.relaxSettings.diffuseMinLuminanceWeight, 0.0f, 1.0f, "%.2f");
                                ImGui::SliderFloat("Depth threshold", &m_ui.relaxSettings.depthThreshold, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
                                ImGui::SliderFloat3("Confidence driven relaxation", &m_ui.relaxSettings.confidenceDrivenRelaxationMultiplier, 0.0f, 1.0f, "%.2f");
                                ImGui::SliderFloat3("Relaxation", &m_ui.relaxSettings.luminanceEdgeStoppingRelaxation, 0.0f, 1.0f, "%.2f");

                                {
                                    int v = (int)m_ui.relaxSettings.checkerboardMode;
                                    ImGui::Combo("Checkerboard mode", &v, checkerboardMode, 3);
                                    m_ui.relaxSettings.checkerboardMode = (nrd::CheckerboardMode)v;
                                }

                                {
                                    int v = (int)m_ui.relaxSettings.hitDistanceReconstructionMode;
                                    ImGui::Combo("HitT reconstruction mode", &v, hitDistanceReconstructionMode, 3);
                                    m_ui.relaxSettings.hitDistanceReconstructionMode = (nrd::HitDistanceReconstructionMode)v;
                                }

                                ImGui::Checkbox("Anti-firefly", &m_ui.relaxSettings.enableAntiFirefly);
                                ImGui::Checkbox("Roughness edge stopping", &m_ui.relaxSettings.enableRoughnessEdgeStopping);

                                if (ImGui::CollapsingHeader("Antilag"))
                                {
                                    ImGui::Indent(12.0f);
                                    ImGui::SliderFloat("Acceleration amount", &m_ui.relaxSettings.antilagSettings.accelerationAmount, 0.0f, 1.0f, "%.2f");
                                    ImGui::SliderFloat("Spatial sigma scale", &m_ui.relaxSettings.antilagSettings.spatialSigmaScale, 0.01f, 10.0f, "%.2f");
                                    ImGui::SliderFloat("Temporal sigma scale", &m_ui.relaxSettings.antilagSettings.temporalSigmaScale, 0.01f, 10.0f, "%.2f");
                                    ImGui::SliderFloat("Reset amount", &m_ui.relaxSettings.antilagSettings.resetAmount, 0.0f, 1.0f, "%.2f");
                                    ImGui::Unindent(12.0f);
                                }

                                ImGui::Unindent(12.0f);
                            }

                            break;
                        }
                    }

                    m_prevNrdDlfgEnabled = m_ui.enableDlfg;
                    break;
                }
                case DenoiserSelection::DlssRr:
                {
                    m_ui.enableDenoiser = true;
                    m_ui.enableAccumulation = false;
                    if (m_prevDenoiserSelection != DenoiserSelection::DlssRr)
                    {
                        m_ui.enableDlfg = m_prevDlssrrDlfgEnabled;
                    }
                    m_prevDlssrrDlfgEnabled = m_ui.enableDlfg;

                    int dlssQualityMode = 0;
                    switch (m_ui.dlssrrQualityMode)
                    {
                        case sl::DLSSMode::eUltraPerformance: dlssQualityMode = 0; break;
                        case sl::DLSSMode::eMaxPerformance:   dlssQualityMode = 1; break;
                        case sl::DLSSMode::eBalanced:         dlssQualityMode = 2; break;
                        case sl::DLSSMode::eMaxQuality:       dlssQualityMode = 3; break;
                        case sl::DLSSMode::eDLAA:             dlssQualityMode = 4; break;
                    }
                    ImGui::Combo("DLSS Quality", &dlssQualityMode, "UltraPerformance\0Performance\0Balanced\0Quality\0DLAA\0");
                    switch (dlssQualityMode)
                    {
                        case 0: m_ui.dlssrrQualityMode = sl::DLSSMode::eUltraPerformance; break;
                        case 1: m_ui.dlssrrQualityMode = sl::DLSSMode::eMaxPerformance; break;
                        case 2: m_ui.dlssrrQualityMode = sl::DLSSMode::eBalanced; break;
                        case 3: m_ui.dlssrrQualityMode = sl::DLSSMode::eMaxQuality; break;
                        case 4: m_ui.dlssrrQualityMode = sl::DLSSMode::eDLAA; break;
                    }
                    break;
                }
                case DenoiserSelection::Reference:
                {
                    m_ui.enableDenoiser = false;
                    m_ui.enableAccumulation = true;
                    m_ui.upscalerSelection = UpscalerSelection::None;
                    m_ui.enableDlfg = false;
                    m_ui.reflexMode = sl::ReflexMode::eOff;
                    if (m_ui.enableAnimations == true)
                    {
                        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 80, 80, 255));
                        ImGui::Text("Warning: Reference Mode is auto-disabled when Animation is active.");
                        ImGui::PopStyleColor();
                    }
                    break;
                }
            }
        }
        ImGui::Indent(-12.0f);
    }

    ImGui::Separator();
    if (ImGui::CollapsingHeader("Lighting:", ImGuiTreeNodeFlags_None))
    {
        ImGui::Indent(12.0f);
        updateAccum |= ImGui::Checkbox("Enable Sky", &m_ui.enableSky);
        if (m_ui.enableSky)
        {
            updateAccum |= ImGui::Combo("Sky Type", (int*)&m_ui.skyType, "Constant\0Procedural\0EnvironmentMap\0");
            if (m_ui.skyType != SkyType::Environment_Map)
            {
                updateAccum |= ImGui::ColorEdit4("Sky Color", m_ui.skyColor, ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_Float);
            }
            else
            {
                // Show available environment map files
                const std::string currentEnvMapFullPath = m_app.GetScene()->GetCurrentEnvMapName();
                const std::string currentEnvMap = currentEnvMapFullPath.substr(currentEnvMapFullPath.find_last_of("/\\") + 1);
                if (ImGui::BeginCombo("Environment Map", currentEnvMap.c_str()))
                {
                    const std::vector<std::string>& envMaps = m_app.GetScene()->GetAvailableEnvMaps();
                    for (const std::string& envMap : envMaps)
                    {
                        const bool is_selected = envMap == currentEnvMapFullPath;
                        const std::string envMapStr = envMap.substr(envMap.find_last_of("/\\") + 1);
                        if (ImGui::Selectable(envMapStr.c_str(), is_selected))
                        {
                            updateAccum |= m_app.SetCurrentEnvironmentMapAndLoading(envMap);
                        }

                        if (is_selected)
                        {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }
            }
            updateAccum |= ImGui::SliderFloat("Environment Light Intensity", &m_ui.environmentLightIntensity, 0.0f, 10.0f);
        }
        updateAccum |= ImGui::Checkbox("Enable Emissives", &m_ui.enableEmissives);
        const bool showEmissiveSurfacesChanged = ImGui::Checkbox("Show emissive surfaces", &m_ui.showEmissiveSurfaces);
        updateAccum |= showEmissiveSurfacesChanged;
        updateAccelerationStructure |= showEmissiveSurfacesChanged;
        updateAccum |= ImGui::Checkbox("Enable Lighting", &m_ui.enableLighting);
        if (m_ui.enableLighting)
        {
            updateAccum |= ImGui::Checkbox("Enable Direct Lighting", &m_ui.enableDirectLighting);
            updateAccum |= ImGui::Checkbox("Enable Indirect Lighting", &m_ui.enableIndirectLighting);
        }

        const auto& lights = m_app.GetScene()->GetNativeScene()->GetSceneGraph()->GetLights();

        if (!lights.empty() && ImGui::CollapsingHeader("Lights", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (ImGui::BeginCombo("Select Light", m_selectedLight ? m_selectedLight->GetName().c_str() : "(None)"))
            {
                int lightIndex = 0;
                for (const auto& light : lights)
                {
                    bool selected = m_selectedLight == light;
                    ImGui::Selectable(light->GetName().c_str(), &selected);
                    if (selected)
                    {
                        m_selectedLight = light;
                        m_selectedLightIndex = lightIndex;
                        ImGui::SetItemDefaultFocus();
                    }
                    lightIndex++;

                }
                ImGui::EndCombo();
            }

            if (m_selectedLight)
            {
                bool target = (m_ui.targetLight == m_selectedLightIndex);
                updateAccum |= ImGui::Checkbox("Target this light?", &target);
                if (target)
                {
                    m_ui.targetLight = m_selectedLightIndex;
                }
                else
                {
                    m_ui.targetLight = -1;
                }

                updateAccum |= donut::app::LightEditor(*m_selectedLight);
            }
        }
        ImGui::Indent(-12.0f);
    }

    ImGui::Separator();
    if (ImGui::CollapsingHeader("Hair:", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Indent(12.0f);

        updateAccum |= ImGui::Checkbox("Enable Hair", &m_ui.enableHair);

        if (m_ui.enableHair)
        {
            const char* hairGeometryType =
                (m_ui.hairTessellationType == TessellationType::Polytube) ? "Hair Geometry (Polytube):" :
                ((m_ui.hairTessellationType == TessellationType::DisjointOrthogonalTriangleStrip) ? "Hair Geometry (DOTS):" : "Hair Geometry (LSS):");

            if (ImGui::CollapsingHeader(hairGeometryType, ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::Indent(12.0f);

                m_showRefreshSceneRemindText |= ImGui::SliderFloat("Radius Scale", &m_ui.hairRadiusScale, 0.01f, 5.0f);
                if (m_showRefreshSceneRemindText)
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 80, 80, 255));
                    ImGui::Text("Radius Scale is changed. Please refresh scene.");
                    ImGui::PopStyleColor();
                }

                ImGui::Indent(-12.0f);
            }

            if (ImGui::CollapsingHeader("Hair Rendering:", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::Indent(12.0f);

                updateAccum |= ImGui::Checkbox("Enable Hair Material Override", &m_ui.enableHairMaterialOverride);
                updateAccum |= ImGui::Combo("Mode", (int*)&m_ui.hairTechSelection, m_ui.hairModeStrings);
                updateAccum |= ImGui::Combo("Absorption Model", (int*)&m_ui.hairAbsorptionModel, m_ui.hairAbsorptionModelStrings);

                if (m_ui.hairAbsorptionModel == HairAbsorptionModel::Color)
                {
                    updateAccum |= ImGui::ColorEdit4("Hair Color", m_ui.hairBaseColor, ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_Float);
                }
                else if (m_ui.hairAbsorptionModel == HairAbsorptionModel::Physics ||
                    m_ui.hairAbsorptionModel == HairAbsorptionModel::Physics_Normalized)
                {
                    updateAccum |= ImGui::SliderFloat("Melanin", &m_ui.melanin, 0.0f, 1.0f);
                    updateAccum |= ImGui::SliderFloat("MelaninRedness", &m_ui.melaninRedness, 0.0f, 1.0f);
                }
                else
                {
                    assert(false);
                }

#ifdef _DEBUG
                updateAccum |= ImGui::Checkbox("Analytical Fresnel", &m_ui.analyticalFresnel);
#endif

                if (m_ui.hairTechSelection == HairTechSelection::Chiang)
                {
                    updateAccum |= ImGui::Checkbox("Anisotropic Roughness", &m_ui.anisotropicRoughness);
                    updateAccum |= ImGui::SliderFloat("Roughness", &m_ui.longitudinalRoughness, 0.001f, 1.0f);
                    if (m_ui.anisotropicRoughness)
                    {
                        updateAccum |= ImGui::SliderFloat("Azimuthal Roughness", &m_ui.azimuthalRoughness, 0.001f, 1.0f);
                    }
                }
                else if (m_ui.hairTechSelection == HairTechSelection::Farfield)
                {
                    updateAccum |= ImGui::SliderFloat("Roughness", &m_ui.hairRoughness, 0.001f, 1.0f);
                    if (m_ui.hairAbsorptionModel == HairAbsorptionModel::Color)
                    {
                        updateAccum |= ImGui::SliderFloat("Radial Roughness", &m_ui.azimuthalRoughness, 0.0f, 1.0f);
                    }
                    updateAccum |= ImGui::ColorEdit4("Hair Tint", m_ui.diffuseRefelctionTint, ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_Float);
                    updateAccum |= ImGui::SliderFloat("Diffuse Weight", &m_ui.diffuseReflectionWeight, 0.0f, 1.0f);
                }

                updateAccum |= ImGui::SliderFloat("ior", &m_ui.ior, 1.0f, 3.0f);

                updateAccum |= ImGui::SliderFloat("Surface Offset", &m_ui.cuticleAngleInDegrees, 0.0f, 10.0f);
            }

            ImGui::Indent(-12.0f);
        }

        ImGui::Indent(-12.0f);
    }

    ImGui::Separator();
    if (ImGui::CollapsingHeader("Subsurface Scattering:", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Indent(12.0f);

        updateAccum |= ImGui::Checkbox("Enable SSS", &m_ui.enableSss);
        if (m_ui.enableSss)
        {
            updateAccum |= ImGui::Checkbox("Enable SSS Indirect Light", &m_ui.enableSssIndirect);

            updateAccum |= ImGui::Checkbox("Enable SSS Material Override", &m_ui.enableSssMaterialOverride);

#ifdef _DEBUG
            updateAccum |= ImGui::Checkbox("Use Specular as SSS Color", &m_ui.useMaterialSpecularAlbedoAsSssTransmission);
            updateAccum |= ImGui::Checkbox("Use Diffuse as SSS Color", &m_ui.useMaterialDiffuseAlbedoAsSssTransmission);

            ImGui::SliderInt("SSS DI Sample Count", &m_ui.sssSampleCount, 1, 256);
#endif

            updateAccum |= ImGui::Combo("SSS Preset", (int*)&m_ui.sssPreset, m_ui.sssPresetStrings);
            updateAccum |= ImGui::ColorEdit4("SSS Color", m_ui.sssTransmissionColor, ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_Float);
            updateAccum |= ImGui::ColorEdit4("Radius(mfp)", m_ui.sssScatteringColor, ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_Float);

            updateAccum |= ImGui::SliderFloat("Scale", &m_ui.sssScale, 0.0f, 100.0f);
            updateAccum |= ImGui::SliderFloat("Max Sample Radius", &m_ui.maxSampleRadius, 0.0f, 64.0f);

            updateAccum |= ImGui::Checkbox("Enable SSS Transmission", &m_ui.enableSssTransmission);
            if (m_ui.enableSssTransmission)
            {
                if (ImGui::CollapsingHeader("SSS Transmission:", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    ImGui::Indent(12.0f);

                    updateAccum |= ImGui::SliderFloat("SSS Anisotropy", &m_ui.sssAnisotropy, -1.0f, 1.0f);

                    ImGui::Indent(-12.0f);
                }
            }
#ifdef _DEBUG
            updateAccum |= ImGui::Checkbox("Enable SSS Microfacet", &m_ui.enableSssMicrofacet);
            if (m_ui.enableSssMicrofacet)
            {
                if (ImGui::CollapsingHeader("SSS Reflection:", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    ImGui::Indent(6.0f);
                    updateAccum |= ImGui::SliderFloat("SSS Weight", &m_ui.sssWeight, 0.0f, 1.0f);
                    updateAccum |= ImGui::SliderFloat("SSS Specular Weight", &m_ui.sssSpecularWeight, 0.0f, 1.0f);
                    updateAccum |= ImGui::Checkbox("Enable SSS Roughness Override", &m_ui.enableSssRoughnessOverride);
                    if (m_ui.enableSssRoughnessOverride)
                    {
                        updateAccum |= ImGui::SliderFloat("SSS Specular Alpha Override", &m_ui.sssRoughnessOverride, 0.0f, 1.0f);
                    }
                    ImGui::Indent(-6.0f);
                }
            }

            updateAccum |= ImGui::Checkbox("Enable Diffusion Profile", &m_ui.enableDiffusionProfile);
            updateAccum |= ImGui::Checkbox("Enable SSS Debug", &m_ui.enableSssDebug);
            if (m_ui.enableSssDebug)
            {
                updateAccum |= ImGui::SliderInt2("SSS Debug Coordinate", m_ui.sssDebugCoordinate, 0, 10000);
            }
#endif
        }

        ImGui::Indent(-12.0f);
    }

    // Animation
    if (m_ui.showAnimationUI)
    {
        ImGui::Separator();
        if (ImGui::CollapsingHeader("Animation:", ImGuiTreeNodeFlags_None))
        {
            updateAccum |= ImGui::Checkbox("Animations", &m_ui.enableAnimations);
            if (m_ui.enableAnimations)
            {
                ImGui::SliderFloat("Speed(Seconds/Frame)", &m_ui.animationFps, 0.1f, 240.0f);

                ImGui::Checkbox("Enable Animation Smoothing", &m_ui.enableAnimationSmoothing);
                if(m_ui.enableAnimationSmoothing)
                {
                    ImGui::SliderFloat("Smoothing Factor", &m_ui.animationSmoothingFactor, 1.0f, 256.0f);
                }

#if _DEBUG
                ImGui::Checkbox("Enable Animation Debugging", &m_ui.enableAnimationDebugging);
                if (m_ui.enableAnimationDebugging)
                {
                    if (ImGui::Button("-")) {
                        m_ui.animationKeyFrameIndexOverride = std::max(m_ui.animationKeyFrameIndexOverride - 1, 0);
                    }
                    ImGui::SameLine();
                    ImGui::SliderInt("##slider", &m_ui.animationKeyFrameIndexOverride, 0, 1000);
                    ImGui::SameLine();
                    if (ImGui::Button("+")) {
                        m_ui.animationKeyFrameIndexOverride = std::min(m_ui.animationKeyFrameIndexOverride + 1, 1000);
                    }
                    ImGui::SameLine();
                    ImGui::Text("Animation Keyframe Index Override");
                    ImGui::SliderFloat("Animation Keyframe Weight Override", &m_ui.animationKeyFrameWeightOverride, 0.1f, 1.0f);
                }
#endif
            }
        }
    }

    ImGui::Separator();
    if (ImGui::CollapsingHeader("Tone mapping:", ImGuiTreeNodeFlags_None))
    {
        ImGui::Indent(12.0f);

        updateAccum |= ImGui::Combo("Operator", (int*)&m_ui.toneMappingOperator, m_ui.toneMappingOperatorStrings);
        ImGui::Checkbox("Clamp", &m_ui.toneMappingClamp);

        ImGui::Indent(-12.0f);
    }

    ImGui::End();

    if (updateAccum)
    {
        m_app.ResetAccumulation();
    }

    if (updateAccelerationStructure)
    {
        m_app.RebuildAccelerationStructure();
    }

    ImGui::PopFont();

    m_prevDenoiserSelection = m_ui.denoiserSelection;
};
