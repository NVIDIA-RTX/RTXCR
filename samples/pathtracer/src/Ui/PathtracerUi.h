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

#include <donut/engine/SceneTypes.h>
#include <donut/engine/SceneGraph.h>
#include <donut/app/DeviceManager.h>
#include <donut/app/imgui_renderer.h>
#include <donut/app/imgui_console.h>
#include <donut/render/TemporalAntiAliasingPass.h>

#include "../Curve/CurveTessellation.h"

#include <NRD.h>
#include "../Denoiser/NRD/NrdConfig.h"

#include <../shared/shared.h>
#include <sl.h>
#include <sl_consts.h>

#include <sl_dlss.h>
#include <sl_dlss_d.h>
#include <sl_reflex.h>

enum class DenoiserSelection : uint32_t
{
    None = 0,
    Nrd = 1,
    DlssRr = 2,
    Reference = 3,
};

enum class UpscalerSelection : uint32_t
{
    None = 0,
    TAA = 1,
    DLSS = 2,
};

enum class NrdMode : uint32_t
{
    Reblur = 0,
    Relax = 1,
};

enum class HairAbsorptionModel : uint32_t
{
    Color = 0,
    Physics = 1,
    Physics_Normalized = 2,
};

enum class SssScatteringColorPreset : uint32_t
{
    Custom = 0,
    Marble = 1,
    Skin_1 = 2,
    Skin_2 = 3,
    Skin_3 = 4,
    Skin_4 = 5,
    Apple = 6,
    Chicken = 7,
    Cream = 8,
    Ketchup = 9,
    Potato = 10,
    Skim_Milk = 11,
    Whole_Milk = 12,
};

enum class ToneMappingOperator : uint32_t
{
    Linear = 0,
    Reinhard = 1,
};

struct UIData
{
    bool                    showUI = true;
    bool                    enableRandom = true;
    float                   cameraSpeed = 50.0f;
    bool                    lockCamera = false;
    bool                    enableTransmission = true;
    bool                    enableBackFaceCull = true;
    int                     bouncesMax = 8;
    bool                    enableAccumulation = false;
    int                     accumulatedFrames = 1;
    int                     accumulatedFramesMax = 128;
    float                   exposureAdjustment = 0.0f;
    bool                    enableSky = true;
    SkyType                 skyType = SkyType::Environment_Map;
    bool                    enableEmissives = true;
    bool                    showEmissiveSurfaces = false;
    bool                    enableLighting = true;
    bool                    enableDirectLighting = true;
    bool                    enableIndirectLighting = true;
    bool                    enableTransparentShadows = true;
    bool                    enableSoftShadows = true;
    float                   throughputThreshold = 0.01f;
    bool                    enableRussianRoulette = true;
    dm::float3              skyColor = dm::float3(42.0f, 52.0f, 57.0f) / 255.0f;
    float                   environmentLightIntensity = 0.33f;
    int                     samplesPerPixel = 1;
    int                     targetLight = -1;
    bool                    enableTonemapping = true;

    JitterMode              jitterMode = JitterMode::Halton_DLSS;
    const char* const       jitterModeStrings = "None\0Halton\0Halton_DLSS\0";

    bool                    toneMappingClamp = true;
    ToneMappingOperator     toneMappingOperator = ToneMappingOperator::Reinhard;
    const char* const       toneMappingOperatorStrings = "Linear\0Reinhard\0";

    // Denoiser
    bool                    enableDenoiser = false;
    DenoiserSelection       denoiserSelection = DenoiserSelection::DlssRr;
    const char*             denoiserSelectionStrings = "None\0NRD\0DLSS-RR\0Reference\0";
    // NRD
    NrdMode                 nrdDenoiserMode = NrdMode::Relax;
    const char*             nrdModeStrings = "Reblur\0Relax\0";
    bool                    forceResetDenoiser = false;
    nrd::CommonSettings     nrdCommonSettings = {};
    nrd::ReblurSettings     reblurSettings = NrdConfig::GetDefaultREBLURSettings();
    nrd::RelaxSettings      relaxSettings = NrdConfig::GetDefaultRELAXSettings();
    // DLSS-RR
    sl::DLSSMode            dlssrrQualityMode = sl::DLSSMode::eMaxQuality;
    enum class DLSSMode : uint32_t
    {
        eOff,
        eMaxPerformance,
        eBalanced,
        eMaxQuality,
        eUltraPerformance,
        eUltraQuality,
        eDLAA,
        eCount,
    };
    // DLFG
    bool                    enableDlfg = true;
    int                     dlfgNumFramesToGenerate = 2; // 2x DLSS-FG by default
    int                     dlfgNumFramesActuallyPresented = 1;
    int                     dlfgMaxNumFramesToGenerate = 3;
    // Reflex
    sl::ReflexMode          reflexMode = sl::ReflexMode::eLowLatency;
    const char*             reflexSelectionStrings = "Off\0LowLatency\0LowLatencyWithBoost\0";
    // Upscaler
    UpscalerSelection       upscalerSelection = UpscalerSelection::DLSS;
    const char*             upscalerSelectionStrings = "None\0TAA\0DLSS\0";
    sl::DLSSMode            dlsssrQualityMode = sl::DLSSMode::eDLAA;

    // Hair
    bool                    enableHair = true;
    bool                    enableHairMaterialOverride = false;
    TessellationType        hairTessellationType = TessellationType::LinearSweptSphere;
    const char* const       hairTessellationTypeStrings = "PolyTube\0"
                                                          "DOTS\0"
                                                          "LSS\0";
    HairTechSelection       hairTechSelection = HairTechSelection::Farfield;
    const char* const       hairModeStrings = "Chiang BCSDF\0Farfield BCSDF\0";
    HairAbsorptionModel     hairAbsorptionModel = HairAbsorptionModel::Physics;
    const char* const       hairAbsorptionModelStrings = "Color\0Physics\0Physics Normalized\0";
    bool                    analyticalFresnel = false;
    // Chiang Model
    dm::float3              hairBaseColor = dm::float3(0.227f, 0.130f, 0.035f);
    bool                    anisotropicRoughness = true;
    float                   longitudinalRoughness = 0.4f;
    float                   azimuthalRoughness = 0.6f;
    // OV Model
    float                   melanin = 0.805f;
    float                   melaninRedness = 0.05f;
    float                   hairRoughness = 0.25f;
    float                   diffuseReflectionWeight = 0.0f;
    dm::float3              diffuseRefelctionTint = dm::float3(1.0f, 1.0f, 1.0f);
    // Common Hair Settings
    float                   ior = 1.55f;
    float                   cuticleAngleInDegrees = 3.0f;
    // Hair Tests
    int                     whiteFurnaceSampleCount = 1000;
    // Hair Geometry
    float                   hairRadiusScale = 0.618f;

    // SSS
    bool                    enableSss = true;
    bool                    enableSssIndirect = true;
    bool                    enableSssMaterialOverride = false;
    bool                    useMaterialSpecularAlbedoAsSssTransmission = false;
    bool                    useMaterialDiffuseAlbedoAsSssTransmission = true;
    SssScatteringColorPreset sssPreset = SssScatteringColorPreset::Custom;
    const char* const       sssPresetStrings = "Custom\0"
                                               "Marble\0"
                                               "Skin01\0"
                                               "Skin02\0"
                                               "Skin03\0"
                                               "Skin04\0"
                                               "Apple\0"
                                               "Chicken\0"
                                               "Cream\0"
                                               "Ketchup\0"
                                               "Potato\0"
                                               "Skim Milk\0"
                                               "Whole Milk\0";
    dm::float3              sssTransmissionColor = dm::float3(1.0f, 1.0f, 1.0f);
    dm::float3              sssScatteringColor = dm::float3(0.8f, 0.4f, 0.2f);
    float                   sssScale = 40.0f;
    float                   maxSampleRadius = 1.0f;
    int                     sssSampleCount = 1;
    // SSS Transmission
    bool                    enableSssTransmission = true;
    float                   sssAnisotropy = 0.0f;
    int                     sssTransmissionBsdfSampleCount = 1;
    int                     sssTransmissionPerBsdfScatteringSampleCount = 1;
    bool                    enableSingleScatteringDiffusionProfileCorrection = false;
    // SSS Specular Reflection
    bool                    enableSssMicrofacet = true;
    float                   sssWeight = 1.0f;
    float                   sssSpecularWeight = 1.0f;
    bool                    enableSssRoughnessOverride = false;
    float                   sssRoughnessOverride = 0.4f;
    // SSS Debug
    bool                    enableSssDebug = false;
    bool                    enableDiffusionProfile = true;
    int                     sssDebugCoordinate[2] = { 960, 540 };
    bool                    forceLambertianBRDF = false;

    // Animation
    bool                    showAnimationUI = false;
    bool                    enableAnimations = false;
    float                   animationFps = 30.0f;
    bool                    enableAnimationSmoothing = true;
    float                   animationSmoothingFactor = 16.0f;
    bool                    enableAnimationDebugging = false;
    int                     animationKeyFrameIndexOverride = 0;
    float                   animationKeyFrameWeightOverride = 0.0f;

    bool                    recompileShader = false;

    bool                    captureScreenshot = false;
    static constexpr size_t kBufSize = 64;
    const char*             defaultScreenShotName = "Screenshot.png";
    char                    screenshotName[kBufSize] = "Screenshot.png";

    std::shared_ptr<donut::engine::Material> selectedMaterial = nullptr;
    std::shared_ptr<donut::engine::SceneCamera> activeSceneCamera = nullptr;

    RtxcrDebugOutputType debugOutput = RtxcrDebugOutputType::None;
    const char* debugOutputTypeStrings = "None\0"
                                         "Diffuse Reflectance\0"
                                         "Specular Reflectance\0"
                                         "Roughness\0"
                                         "Worldspace Normals\0"
                                         "Shading Normals\0"
                                         "Worldspace Tangents\0"
                                         "Worldspace Position\0"
                                         "Curve Radius\0"
                                         "View Depth\0"
                                         "Device Depth\0"
                                         "Barycentrics\0"
                                         "Diffuse HitT\0"
                                         "Specular HitT\0"
                                         "InstanceID\0"
                                         "Emissives\0"
                                         "Bounce Heatmap\0"
                                         "Motion Vector\0"
                                         "Path Tracer Output (Noised)\0"
                                         "NaN\0"
                                         "WhiteFurnace\0"
                                         "IsMorphTarget\0";
    float debugScale = 1.0f;
    float debugMinMax[2] = { 0, TRACING_FAR_DENOISING_DISTANCE };
};

class PathtracerUI : public donut::app::ImGui_Renderer
{
public:
    PathtracerUI(donut::app::DeviceManager* deviceManager, class SampleRenderer& app, UIData& ui);
    virtual ~PathtracerUI() = default;
protected:
    virtual void buildUI(void) override;
private:
    class SampleRenderer& m_app;

    UIData& m_ui;

    std::shared_ptr<donut::app::RegisteredFont> m_fontOpenSans = nullptr;
    std::shared_ptr<donut::app::RegisteredFont> m_fontDroidMono = nullptr;

    std::shared_ptr<donut::engine::Light> m_selectedLight;
    int m_selectedLightIndex = 0;

    bool m_showRefreshSceneRemindText = false;

    DenoiserSelection m_prevDenoiserSelection = DenoiserSelection::DlssRr;
    bool m_prevNrdDlfgEnabled = true;
    bool m_prevDlssrrDlfgEnabled = true;

    uint64_t m_adapterMemoryInGigaBytes = 0;

    nvrhi::CommandListHandle m_commandList;
};
