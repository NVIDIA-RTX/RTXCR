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
}

PathtracerUI::PathtracerUI(DeviceManager* deviceManager, SampleRenderer& app, UIData& ui)
    : ImGui_Renderer(deviceManager)
    , m_app(app)
    , m_ui(ui)
{
    m_commandList = GetDevice()->createCommandList();

    m_fontOpenSans = CreateFontFromFile(*(app.GetRootFS()), "/assets/fonts/OpenSans/OpenSans-Regular.ttf", 17.f);
    m_fontDroidMono = CreateFontFromFile(*(app.GetRootFS()), "/assets/fonts/DroidSans/DroidSans-Mono.ttf", 14.f);

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

    float const fontSize = ImGui::GetFontSize();
    ImGui::SetNextWindowPos(ImVec2(fontSize * 0.2f, fontSize * 0.2f), 0);

    ImGui::Begin("Settings", 0, ImGuiWindowFlags_AlwaysAutoResize);
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
    double frameTime = GetDeviceManager()->GetAverageFrameTimeSeconds();
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
            updateAccum |= ImGui::Checkbox("Transmission", &m_ui.enableTransmission); ImGui::SameLine();
            updateAccum |= ImGui::Combo("Jitter Mode", (int*)&m_ui.jitterMode, m_ui.jitterModeStrings);
#endif

            if (ImGui::Button("Recompile Shader"))
            {
                m_ui.recompileShader = true;
            }

            ImGui::Text("Name:");
            ImGui::SameLine();
            ImGui::InputText(" ", m_ui.screenshotName, m_ui.kBufSize, ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::SameLine();
            if (ImGui::Button("Capture Screenshot"))
            {
                m_ui.captureScreenshot = true;
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
            updateAccum |= ImGui::Combo("Tech", (int*)&m_ui.denoiserSelection, m_ui.denoiserSelectionStrings);

            switch (m_ui.denoiserSelection)
            {
                case DenoiserSelection::None:
                {
                    m_ui.enableDenoiser = false;
                    m_ui.enableAccumulation = false;
                    break;
                }
                case DenoiserSelection::DlssRr:
                {
                    m_ui.enableDenoiser = true;
                    m_ui.enableAccumulation = false;

                    int dlssQualityMode = 0;
                    switch (m_ui.dlssQualityMode)
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
                        case 0: m_ui.dlssQualityMode = sl::DLSSMode::eUltraPerformance; break;
                        case 1: m_ui.dlssQualityMode = sl::DLSSMode::eMaxPerformance; break;
                        case 2: m_ui.dlssQualityMode = sl::DLSSMode::eBalanced; break;
                        case 3: m_ui.dlssQualityMode = sl::DLSSMode::eMaxQuality; break;
                        case 4: m_ui.dlssQualityMode = sl::DLSSMode::eDLAA; break;
                    }

                    break;
                }
                case DenoiserSelection::Reference:
                {
                    m_ui.enableAccumulation = true;
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
                    ImGui::Text("Radius Scale is changed. Please refresh scene.");
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

#ifdef _DEBUG
    // Animation
    if (m_ui.showAnimationUI)
    {
        ImGui::Separator();
        if (ImGui::CollapsingHeader("Animation:", ImGuiTreeNodeFlags_None))
        {
            updateAccum |= ImGui::Checkbox("Animations", &m_ui.enableAnimations);
            if (m_ui.enableAnimations)
            {
                ImGui::SliderFloat("Animation Speed (Seconds Per Frame)", &m_ui.animationFps, 0.1f, 240.0f);

                ImGui::Checkbox("Enable Animation Debugging", &m_ui.enableAnimationDebugging);
                if (m_ui.enableAnimationDebugging)
                {
                    ImGui::SliderInt("Animation Keyframe Index Override", &m_ui.animationKeyFrameIndexOverride, 0, 1000);
                    ImGui::SliderFloat("Animation Keyframe Weight Override", &m_ui.animationKeyFrameWeightOverride, 0.1f, 1.0f);
                }
            }
        }
    }
#endif

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
};
