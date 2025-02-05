/*
 * Copyright (c) 2024-2025, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#include <donut/render/GBufferFillPass.h>
#include <donut/render/ForwardShadingPass.h>
#include <donut/render/DrawStrategy.h>
#include <donut/render/SkyPass.h>
#include <donut/engine/CommonRenderPasses.h>
#include <donut/engine/FramebufferFactory.h>
#include <donut/engine/BindingCache.h>
#include <donut/engine/SceneGraph.h>
#include <donut/app/DeviceManager.h>
#include <donut/core/log.h>
#include <nvrhi/utils.h>
#include <donut/app/imgui_console.h>
#include <donut/app/imgui_renderer.h>

#include "SampleRenderer.h"

using namespace donut;
using namespace donut::math;
using namespace donut::app;
using namespace donut::engine;

#include "../shared/globalCb.h"
#include "../shared/lightingCb.h"

namespace
{
    const uint ReverseBits32(uint x)
    {
        x = (x << 16) | (x >> 16);
        x = ((x & 0x55555555) << 1) | ((x & 0xAAAAAAAA) >> 1);
        x = ((x & 0x33333333) << 2) | ((x & 0xCCCCCCCC) >> 2);
        x = ((x & 0x0F0F0F0F) << 4) | ((x & 0xF0F0F0F0) >> 4);
        x = ((x & 0x00FF00FF) << 8) | ((x & 0xFF00FF00) >> 8);

        return x;
    }

    float RadicalInverse(uint idx, const uint base)
    {
        float val = 0.0f;
        float rcpBase = 1.0f / (float)base;
        float rcpBi = rcpBase;

        while (idx > 0)
        {
            uint d_i = idx % base;
            val += float(d_i) * rcpBi;
            idx = uint(idx * rcpBase);
            rcpBi *= rcpBase;
        }

        return val;
    }

    float2 Halton2D(const uint idx)
    {
        return float2(RadicalInverse(idx + 1, 3), ReverseBits32(idx + 1) * 2.3283064365386963e-10f);
    }

    float2 getCurrentPixelOffset(const int currentFrame) {
        // Halton jitter
        float2 result = float2(0.0f, 0.0f);

        int frameIndex = (currentFrame) % 64;

        const int baseX = 2;
        int Index = frameIndex + 1;
        float invBase = 1.0f / baseX;
        float fraction = invBase;
        while (Index > 0) {
            result.x += (Index % baseX) * fraction;
            Index /= baseX;
            fraction *= invBase;
        }

        const int baseY = 3;
        Index = frameIndex + 1;
        invBase = 1.0f / baseY;
        fraction = invBase;
        while (Index > 0) {
            result.y += (Index % baseY) * fraction;
            Index /= baseY;
            fraction *= invBase;
        }

        result.x -= 0.5f;
        result.y -= 0.5f;
        return result;
    }
}

SampleRenderer::SampleRenderer(DeviceManager* deviceManager, UIData& ui, nvrhi::GraphicsAPI api)
    : ApplicationBase(deviceManager)
    , m_ui(ui)
    , m_resourceManager(GetDevice(),
                        deviceManager->GetBackBuffer(0)->getDesc().width,
                        deviceManager->GetBackBuffer(0)->getDesc().height,
                        deviceManager->GetBackBuffer(0)->getDesc().width,
                        deviceManager->GetBackBuffer(0)->getDesc().height)
    , m_api(api)
    , m_renderSize(0u)
{
}

bool SampleRenderer::Init(int argc, const char* const* argv)
{
    char* sceneName = nullptr;
    uint32_t cameraIndex = -1;
    for (int n = 1; n < argc; n++)
    {
        const char* arg = argv[n];

        if (!strcmp(arg, "-accumulate"))
        {
            m_ui.denoiserSelection = DenoiserSelection::Reference;
        }

        if (!strcmp(arg, "-scene"))
        {
            sceneName = (char*)argv[n + 1];
        }

        if (!strcmp(arg, "-camera"))
        {
            cameraIndex = atoi(argv[n + 1]);
        }

        if (!strcmp(arg, "-screenshot"))
        {
            const char* screenshotName = (char*)argv[n + 1];
            strcpy(m_ui.screenshotName, screenshotName);
        }

        if (!strcmp(arg, "-enableSky"))
        {
            m_ui.enableSky = (bool)atoi(argv[n + 1]);
        }

        if (!strcmp(arg, "-denoiser"))
        {
            const int denoiser = atoi(argv[n + 1]);
            if (denoiser <= 1)
            {
                m_ui.denoiserSelection = (DenoiserSelection) denoiser;
            }
        }

        if (!strcmp(arg, "-hairBsdf"))
        {
            const int hairBsdf = atoi(argv[n + 1]);
            if (hairBsdf <= 1)
            {
                m_ui.hairTechSelection = (HairTechSelection) hairBsdf;
            }
        }

        if (!strcmp(arg, "-hairColorMode"))
        {
            const int hairColorMode = atoi(argv[n + 1]);
            if (hairColorMode <= 2)
            {
                m_ui.hairAbsorptionModel = (HairAbsorptionModel) hairColorMode;
            }
        }

        if (!strcmp(arg, "-enableHairOverride"))
        {
            m_ui.enableHairMaterialOverride = (bool)atoi(argv[n + 1]);
        }

        if (!strcmp(arg, "-hairRadiusScale"))
        {
            m_ui.hairRadiusScale = (float)atof(argv[n + 1]);
        }

        if (!strcmp(arg, "-hairTessellationType"))
        {
            m_ui.hairTessellationType = (TessellationType)atoi(argv[n + 1]);
        }
    }

    if ((GetDevice()->getGraphicsAPI() == nvrhi::GraphicsAPI::VULKAN || !GetDevice()->queryFeatureSupport(nvrhi::Feature::LinearSweptSpheres)) &&
        m_ui.hairTessellationType == TessellationType::LinearSweptSphere)
    {
        m_ui.hairTessellationType = TessellationType::DisjointOrthogonalTriangleStrip;
    }

    m_nativeFileSystem = std::make_shared<vfs::NativeFileSystem>();
    const std::filesystem::path frameworkShaderPath = app::GetDirectoryWithExecutable() / "shaders/framework" / app::GetShaderTypeName(GetDevice()->getGraphicsAPI());
    const std::filesystem::path appShaderPath = app::GetDirectoryWithExecutable() / "shaders/pathtracer" / app::GetShaderTypeName(GetDevice()->getGraphicsAPI());
    const std::filesystem::path mediaDir = app::GetDirectoryWithExecutable().parent_path() / "assets";

    m_rootFileSystem = std::make_shared<vfs::RootFileSystem>();
    m_rootFileSystem->mount("/shaders/donut", frameworkShaderPath);
    m_rootFileSystem->mount("/shaders/app", appShaderPath);
    m_rootFileSystem->mount("/native", m_nativeFileSystem);
    m_rootFileSystem->mount("/assets", mediaDir);

    m_shaderFactory = std::make_shared<engine::ShaderFactory>(GetDevice(), m_rootFileSystem, "/shaders");
    m_CommonPasses = std::make_shared<engine::CommonRenderPasses>(GetDevice(), m_shaderFactory);
    m_bindingCache = std::make_unique<engine::BindingCache>(GetDevice());

    {
        nvrhi::BindlessLayoutDesc bindlessLayoutDesc;
        bindlessLayoutDesc.visibility = nvrhi::ShaderType::All;
        bindlessLayoutDesc.firstSlot = 0;
        bindlessLayoutDesc.maxCapacity = 1024;
        bindlessLayoutDesc.registerSpaces = {
            nvrhi::BindingLayoutItem::RawBuffer_SRV(2),
            nvrhi::BindingLayoutItem::Texture_SRV(3)
        };
        m_bindlessLayout = GetDevice()->createBindlessLayout(bindlessLayoutDesc);
        m_descriptorTable = std::make_shared<engine::DescriptorTableManager>(GetDevice(), m_bindlessLayout);
        m_TextureCache = std::make_shared<engine::TextureCache>(GetDevice(), m_nativeFileSystem, m_descriptorTable);
    }

    m_resourceManager.CreateBuffers();

    // Scene and AS
    {
        m_scene = std::make_shared<SampleScene>(GetFrameIndex(), m_ui.cameraSpeed, cameraIndex, false, sceneName, m_ui);
        SetAsynchronousLoadingEnabled(m_scene->IsAsyncSceneLoadingEnabled());

        m_accelerationStructure = std::make_shared<AccelerationStructure>(GetDevice(), m_scene, m_ui);
    }

    // Render Passes
    {
        m_gbufferPass = std::make_unique<GBufferPass>(GetDevice(), m_shaderFactory, m_scene, m_accelerationStructure, m_ui);
        m_pathTracingPass = std::make_unique<PathTracingPass>(GetDevice(), m_shaderFactory, m_scene, m_accelerationStructure, m_ui);
        m_postProcessingPass = std::make_unique<PostProcessingPass>(GetDevice(), m_shaderFactory);
        m_morphTargetAnimationPass = std::make_unique<MorphTargetAnimationPass>(GetDevice(), m_shaderFactory);
    }

    // Create Environment Map
    {
        m_resourceManager.CreateEnvironmentMap(m_TextureCache.get(), m_scene->GetCurrentEnvMapName());
    }

    // Scene Loading
    {
        BeginLoadingScene(m_nativeFileSystem, m_scene->GetCurrentSceneName());
        m_scene->GetNativeScene()->FinishedLoading(GetFrameIndex());
    }

    {
        m_gbufferPass->CreateGBufferPassPipeline(m_bindlessLayout);
        m_pathTracingPass->CreateRayTracingPipeline(m_bindlessLayout);
        m_postProcessingPass->CreatePostProcessingPipelines();
        m_morphTargetAnimationPass->CreateMorphTargetAnimationPipeline();
    }

    // Create DLSS-RR Denoiser
    m_SL = std::make_unique<SLWrapper>(GetDevice()->getGraphicsAPI());

    m_commandList = GetDevice()->createCommandList();

    // Create Morph Target Buffers
    // Note: Don't check m_resourceManager.GetMorphTargetCount() here,
    //       because we need to loop the meshes in CreateMorphTargetBuffers to determine how many morph targets we have
    // if (m_resourceManager.GetMorphTargetCount() > 0)
    {
        m_resourceManager.CreateMorphTargetBuffers(m_scene, m_commandList);
    }

    return true;
}

bool SampleRenderer::LoadScene(std::shared_ptr<donut::vfs::IFileSystem> fs, const std::filesystem::path& sceneFileName)
{
    return m_scene->Load(GetDevice(), m_shaderFactory, fs, m_TextureCache, m_descriptorTable, nullptr, sceneFileName);
}

void SampleRenderer::SceneUnloading()
{
    GetDevice()->waitForIdle();

    m_scene->Unload();

    m_shaderFactory->ClearCache();
    m_bindingCache->Clear();
    m_ui.selectedMaterial = nullptr;
    m_ui.activeSceneCamera = nullptr;
    m_ui.targetLight = -1;

    m_accelerationStructure->SetRebuildAS(true);

    // Force the buffers to be re-created, as well as the bindings
    BackBufferResizing();

    m_morphTargetAnimationPass->ResetAnimation();
}

void SampleRenderer::SceneLoaded()
{
    ApplicationBase::SceneLoaded();

    m_scene->FinishLoading(GetFrameIndex());

    m_pathTracingPass->ResetAccumulation();

    m_accelerationStructure->SetRebuildAS(true);

    // Recreate morph target buffers to fit for the new scene
    if (m_commandList)
    {
        m_resourceManager.RecreateMorphTargetBuffers(m_scene, m_commandList);
    }
}

void SampleRenderer::SetCurrentSceneNameAndLoading(const std::string& sceneName)
{
    GetScene()->SetCurrentSceneName(sceneName);

    BeginLoadingScene(m_nativeFileSystem, m_scene->GetCurrentSceneName());
}

bool SampleRenderer::SetCurrentEnvironmentMapAndLoading(const std::string& envMapName)
{
    if (m_scene->GetCurrentEnvMapName() != envMapName)
    {
        GetScene()->SetCurrentEnvMapName(envMapName);

        m_resourceManager.CreateEnvironmentMap(m_TextureCache.get(), envMapName);

        if (m_TextureCache)
        {
            m_TextureCache->ProcessRenderingThreadCommands(*m_CommonPasses, 0.f);
            m_TextureCache->LoadingFinished();
        }

        return true;
    }
    return false;
}

void SampleRenderer::Animate(float fElapsedTimeSeconds)
{
    bool isRebuildAsAfterAnimation = false;
    if (m_scene->Animate(fElapsedTimeSeconds, IsSceneLoaded(), GetFrameIndex(), m_ui.lockCamera, &isRebuildAsAfterAnimation))
    {
        if (m_resourceManager.GetMorphTargetCount() > 0)
        {
            m_accelerationStructure->ClearTLAS();

            if (!isRebuildAsAfterAnimation)
            {
                m_accelerationStructure->SetUpdateAS(true);
            }
            else
            {
                m_accelerationStructure->SetRebuildAS(true);

                m_resourceManager.RecreateMorphTargetBuffers(m_scene, m_commandList);

                m_morphTargetAnimationPass->CleanComputePipeline();
            }

            m_morphTargetAnimationPass->Update(fElapsedTimeSeconds);
        }
        else if (isRebuildAsAfterAnimation)
        {
            m_accelerationStructure->SetRebuildAS(true);
        }

        m_pathTracingPass->ResetAccumulation();
    }

    if (m_ui.recompileShader)
    {
        m_pathTracingPass->ResetAccumulation();
    }

    GetDeviceManager()->SetInformativeWindowTitle(g_WindowTitle);
}

void SampleRenderer::updateView(const uint viewportWidth, const uint viewportHeight, const bool updatePreviousView)
{
    nvrhi::Viewport windowViewport(static_cast<float>(viewportWidth), static_cast<float>(viewportHeight));
    if (updatePreviousView)
    {
        m_viewPrevious = m_view;
        m_viewPrevious.UpdateCache();
    }

    const auto sceneCamera = (PerspectiveCamera*)m_scene->GetNativeScene()->GetSceneGraph()->GetCameras().at(0).get();

    m_view.SetViewport(windowViewport);
    m_view.SetMatrices(m_scene->GetCamera().GetWorldToViewMatrix(), perspProjD3DStyleReverse(sceneCamera->verticalFov, windowViewport.width() / windowViewport.height(), sceneCamera->zNear));
    m_view.UpdateCache();
    if (updatePreviousView && GetFrameIndex() == 0)
    {
        m_viewPrevious = m_view;
        m_viewPrevious.UpdateCache();
    }
}

void SampleRenderer::updateConstantBuffers()
{
    // Camera
    m_scene->SetCameraSpeed(m_ui.cameraSpeed);

    LightingConstants constants = {};
    constants.skyColor = m_ui.enableSky ? float4(m_ui.skyColor, 1.0f) : float4(0.0f, 0.0f, 0.0f, 1.0f);

    m_view.UpdateCache();
    m_view.FillPlanarViewConstants(constants.view);

    m_viewPrevious.UpdateCache();
    m_viewPrevious.FillPlanarViewConstants(constants.viewPrev);

    // Add all lights
    m_scene->GetSunlight()->FillLightConstants(constants.sunLight);
    constants.lightCount = 0;
    for (auto light : m_scene->GetNativeScene()->GetSceneGraph()->GetLights())
    {
        if (constants.lightCount < MAX_LIGHTS)
        {
            light->FillLightConstants(constants.lights[constants.lightCount++]);
        }
    }
    const ResourceManager::PathTracerResources& renderTargets = m_resourceManager.GetPathTracerResources();
    m_commandList->writeBuffer(renderTargets.lightConstantsBuffer, &constants, sizeof(constants));

    const bool enableDebugging = m_ui.debugOutput != RtxcrDebugOutputType::None &&
                                 m_ui.debugOutput != RtxcrDebugOutputType::WhiteFurnace;
    const bool enableDenoiser = m_ui.enableDenoiser && !enableDebugging;

    GlobalConstants globalConstants = {};
    if (m_ui.enableRandom)
    {
        if (m_ui.jitterMode == JitterMode::None)
        {
            globalConstants.jitterOffset = float2(0.0f, 0.0f);
        }
        else if (m_ui.jitterMode == JitterMode::Halton)
        {
            globalConstants.jitterOffset = Halton2D(GetFrameIndex());
        }
        else if (m_ui.jitterMode == JitterMode::Halton_DLSS)
        {
            globalConstants.jitterOffset = getCurrentPixelOffset(GetFrameIndex());
        }
        else
        {
            // The random mode needs to be calculated in the shader
            globalConstants.jitterOffset = float2(0.0f, 0.0f);
        }
    }
    else
    {
        // Disable jitter
        globalConstants.jitterOffset = float2(0.0f, 0.0f);
    }
    globalConstants.enableBackFaceCull = m_ui.enableBackFaceCull;
    globalConstants.bouncesMax = m_ui.bouncesMax;
    globalConstants.frameIndex = (m_frameIndex++) * (m_ui.enableRandom ? 1 : 0);
    globalConstants.enableAccumulation = m_ui.enableAccumulation && m_ui.denoiserSelection != DenoiserSelection::DlssRr;
    globalConstants.accumulatedFramesMax = m_pathTracingPass->IsAccumulationReset() ? 1 : m_ui.accumulatedFramesMax;
    globalConstants.recipAccumulatedFrames =
        m_ui.enableAccumulation ? (1.0f / static_cast<float>(m_pathTracingPass->GetAccumulationFrameCount())) : 1.0f;
    globalConstants.environmentLightIntensity = m_ui.environmentLightIntensity;
    globalConstants.enableEmissives = m_ui.enableEmissives;
    globalConstants.enableLighting = m_ui.enableLighting;
    globalConstants.enableDirectLighting = m_ui.enableDirectLighting;
    globalConstants.enableIndirectLighting = m_ui.enableIndirectLighting;
    globalConstants.enableTransmission = m_ui.enableTransmission;
    globalConstants.enableTransparentShadows = m_ui.enableTransparentShadows;
    globalConstants.enableSoftShadows = m_ui.enableSoftShadows;
    globalConstants.throughputThreshold = m_ui.throughputThreshold;
    globalConstants.enableRussianRoulette = m_ui.enableRussianRoulette;
    globalConstants.samplesPerPixel = m_ui.samplesPerPixel;
    globalConstants.exposureScale = donut::math::exp2f(m_ui.exposureAdjustment);

    globalConstants.clamp = (uint)m_ui.toneMappingClamp;
    globalConstants.toneMappingOperator = (uint)m_ui.toneMappingOperator;

    globalConstants.enableDenoiser = enableDenoiser;

    //////////////////////////////////////////////////////////////////////////////////////
    // Hair
    globalConstants.enableHair = m_ui.enableHair;
    globalConstants.enableHairMaterialOverride = m_ui.enableHairMaterialOverride;
    globalConstants.hairMode = m_ui.hairTechSelection;
    globalConstants.hairBaseColor = m_ui.hairBaseColor;
    globalConstants.analyticalFresnel = m_ui.analyticalFresnel;
    globalConstants.longitudinalRoughness = m_ui.longitudinalRoughness;
    globalConstants.azimuthalRoughness = m_ui.anisotropicRoughness ? m_ui.azimuthalRoughness : m_ui.longitudinalRoughness;

    globalConstants.hairIor = m_ui.ior;
    globalConstants.cuticleAngleInDegrees = m_ui.cuticleAngleInDegrees;

    globalConstants.absorptionModel = (uint)m_ui.hairAbsorptionModel;
    globalConstants.melanin = m_ui.melanin;
    globalConstants.melaninRedness = m_ui.melaninRedness;
    globalConstants.hairRoughness = m_ui.hairRoughness;
    globalConstants.diffuseReflectionTint = m_ui.diffuseRefelctionTint;
    globalConstants.diffuseReflectionWeight = m_ui.diffuseReflectionWeight;

    // Hair Test
    globalConstants.whiteFurnaceSampleCount = m_ui.whiteFurnaceSampleCount;
    //////////////////////////////////////////////////////////////////////////////////////

    //////////////////////////////////////////////////////////////////////////////////////
    // Skin
    globalConstants.enableSss = m_ui.enableSss;
    globalConstants.enableSssIndirect = m_ui.enableSssIndirect;
    globalConstants.enableSssMaterialOverride = m_ui.enableSssMaterialOverride;
    globalConstants.sssSampleCount = m_ui.sssSampleCount;
    globalConstants.useMaterialSpecularAlbedoAsSssTransmission = m_ui.useMaterialSpecularAlbedoAsSssTransmission;
    globalConstants.useMaterialDiffuseAlbedoAsSssTransmission = m_ui.useMaterialDiffuseAlbedoAsSssTransmission;
    globalConstants.enableSssTransmission = m_ui.enableSssTransmission;
    // Values are from Henrik Wann Jensen, Stephen R. Marschner, Marc Levoy, and
    // Pat Hanrahan. A Practical Model for Subsurface Light Transport. Proceedings
    // of SIGGRAPH 2001, pages 511-518.
    //
    // TODO: Refactoring SSS color preset
    switch (m_ui.sssPreset)
    {
    case SssScatteringColorPreset::Custom:
        globalConstants.sssTransmissionColor = m_ui.sssTransmissionColor;
        globalConstants.sssScatteringColor = m_ui.sssScatteringColor;
        break;
    case SssScatteringColorPreset::Marble:
        globalConstants.sssTransmissionColor = float3(0.930f, 0.910f, 0.880f);
        globalConstants.sssScatteringColor = float3(8.510f, 5.570f, 3.950f);
        break;
    case SssScatteringColorPreset::Skin_1:
        globalConstants.sssTransmissionColor = float3(0.570f, 0.310f, 0.170f);
        globalConstants.sssScatteringColor = float3(3.670f, 1.370f, 0.680f);
        break;
    case SssScatteringColorPreset::Skin_2:
        globalConstants.sssTransmissionColor = float3(0.750f, 0.570f, 0.470f);
        globalConstants.sssScatteringColor = float3(4.820f, 1.690f, 1.090f);
        break;
    case SssScatteringColorPreset::Skin_3:
        globalConstants.sssTransmissionColor = float3(0.999f, 0.615f, 0.521f);
        globalConstants.sssScatteringColor = float3(1.000f, 0.300f, 0.100f);
        break;
    case SssScatteringColorPreset::Skin_4:
        globalConstants.sssTransmissionColor = float3(0.078f, 0.043f, 0.025f);
        globalConstants.sssScatteringColor = float3(0.723f, 0.264f, 0.127f);
        break;
    case SssScatteringColorPreset::Apple:
        globalConstants.sssTransmissionColor = float3(0.430f, 0.210f, 0.170f);
        globalConstants.sssScatteringColor = float3(11.610f, 3.880f, 1.750f);
        break;
    case SssScatteringColorPreset::Chicken:
        globalConstants.sssTransmissionColor = float3(0.440f, 0.220f, 0.140f);
        globalConstants.sssScatteringColor = float3(9.440f, 3.350f, 1.790f);
        break;
    case SssScatteringColorPreset::Cream:
        globalConstants.sssTransmissionColor = float3(0.990f, 0.940f, 0.830f);
        globalConstants.sssScatteringColor = float3(15.030f, 4.660f, 2.540f);
        break;
    case SssScatteringColorPreset::Ketchup:
        globalConstants.sssTransmissionColor = float3(0.220f, 0.010f, 0.001f);
        globalConstants.sssScatteringColor = float3(4.760f, 0.570f, 0.390f);
        break;
    case SssScatteringColorPreset::Potato:
        globalConstants.sssTransmissionColor = float3(0.860f, 0.740f, 0.290f);
        globalConstants.sssScatteringColor = float3(14.270f, 7.230f, 2.040f);
        break;
    case SssScatteringColorPreset::Skim_Milk:
        globalConstants.sssTransmissionColor = float3(0.890f, 0.890f, 0.800f);
        globalConstants.sssScatteringColor = float3(18.420f, 10.440f, 3.500f);
        break;
    case SssScatteringColorPreset::Whole_Milk:
        globalConstants.sssTransmissionColor = float3(0.950f, 0.930f, 0.850f);
        globalConstants.sssScatteringColor = float3(10.900f, 6.580f, 2.510f);
        break;
    }
    globalConstants.sssScale = std::max(m_ui.sssScale, 1e-7f);
    globalConstants.forceLambertianBRDF = m_ui.forceLambertianBRDF;
    globalConstants.maxSampleRadius = m_ui.maxSampleRadius;
    // SSS Transmission
    {
        globalConstants.sssAnisotropy = clamp(m_ui.sssAnisotropy, -0.999f, 0.999f);
        globalConstants.sssTransmissionBsdfSampleCount = m_ui.sssTransmissionBsdfSampleCount;
        globalConstants.sssTransmissionPerBsdfScatteringSampleCount = m_ui.sssTransmissionPerBsdfScatteringSampleCount;
        globalConstants.enableSingleScatteringDiffusionProfileCorrection = m_ui.enableSingleScatteringDiffusionProfileCorrection;
    }
    globalConstants.enableSssMicrofacet = m_ui.enableSssMicrofacet;
    {
        const float sssWeightSumRcp = 1.0f / (m_ui.sssWeight + m_ui.sssSpecularWeight);
        globalConstants.sssWeight = m_ui.enableSssMicrofacet ? m_ui.sssWeight * sssWeightSumRcp : 1.0f;
        globalConstants.sssSpecularWeight = m_ui.sssSpecularWeight * sssWeightSumRcp;
        globalConstants.enableSssRoughnessOverride = m_ui.enableSssRoughnessOverride;
        globalConstants.sssRoughnessOverride = m_ui.sssRoughnessOverride;
    }
    // SSS Debug
    globalConstants.enableSssDebug = m_ui.enableSssDebug;
    globalConstants.enableDiffusionProfile = m_ui.enableDiffusionProfile;
    globalConstants.sssDebugCoordinate = uint2(m_ui.sssDebugCoordinate[0], m_ui.sssDebugCoordinate[1]);
    //////////////////////////////////////////////////////////////////////////////////////

    // Sky
    donut::render::SkyParameters skyParams = {};
    skyParams.brightness = 1.0f;
    skyParams.horizonColor = constants.skyColor;
    donut::render::SkyPass::FillShaderParameters(*m_scene->GetSunlight(), skyParams, globalConstants.skyParams);
    globalConstants.skyParams.angularSizeOfLight = 0.02f;
    globalConstants.skyParams.glowSize = 0.02f;
    globalConstants.skyParams.skyColor = constants.skyColor;
    if (!m_ui.enableSky)
    {
        globalConstants.skyParams.groundColor = float3(0.0f, 0.0f, 0.0f);
    }
    else if (m_ui.skyType == SkyType::Constant)
    {
        globalConstants.skyParams.groundColor = constants.skyColor;
    }
    else if (m_ui.skyType == SkyType::Environment_Map)
    {
        // Use the angularSizeOfLight in Donut struct to mark env map
        globalConstants.skyParams.angularSizeOfLight = -1.0f;
    }

    globalConstants.targetLight = m_ui.targetLight;
    globalConstants.debugOutputMode = m_ui.debugOutput;
    globalConstants.debugScale = m_ui.debugScale;
    globalConstants.debugMin = m_ui.debugMinMax[0];
    globalConstants.debugMax = m_ui.debugMinMax[1];

    m_commandList->writeBuffer(renderTargets.globalArgs, &globalConstants, sizeof(globalConstants));
}

void SampleRenderer::BackBufferResizing()
{
    m_resourceManager.CleanTextures();

    m_bindingCache->Clear();

    m_accelerationStructure->ClearTLAS();
    m_accelerationStructure->SetRebuildAS(true);

    m_pathTracingPass->ResetAccumulation();
}

void SampleRenderer::Render(nvrhi::IFramebuffer* framebuffer)
{
    const ResourceManager::PathTracerResources& renderTargets = m_resourceManager.GetPathTracerResources();

    m_scene->RefreshSceneGraph(GetFrameIndex());

    const auto& fbinfo = framebuffer->getFramebufferInfo();
    const dm::uint2 displaySize = dm::uint2(fbinfo.width, fbinfo.height);

    auto isDlssRrDirty = [&]() -> bool
    {
        return m_dlssRrOptions.mode != m_ui.dlssQualityMode
            || m_dlssRrOptions.outputWidth != displaySize.x
            || m_dlssRrOptions.outputHeight != displaySize.y;
    };

    auto createDlssConstants = [&](bool isDepthInverted) -> sl::Constants
    {
        float aspectRatio = static_cast<float>(displaySize.x) / static_cast<float>(displaySize.y);

        const auto sceneCamera = (PerspectiveCamera*)m_scene->GetNativeScene()->GetSceneGraph()->GetCameras().at(0).get();
        dm::float4x4 projection = dm::perspProjD3DStyleReverse(sceneCamera->verticalFov, aspectRatio, sceneCamera->zNear);

        const bool isRecreateRenderTargets = displaySize.x != m_resourceManager.GetResolutionWidth() || displaySize.y != m_resourceManager.GetResolutionHeight();
        bool needNewPasses = isRecreateRenderTargets || !renderTargets.pathTracerOutputTexture || m_accelerationStructure->IsRebuildAS() || m_accelerationStructure->IsUpdateAS();

        dm::affine3 viewReprojection = m_view.GetInverseViewMatrix() * m_viewPrevious.GetViewMatrix();
        dm::float4x4 reprojectionMatrix = m_view.GetInverseProjectionMatrix(false) * affineToHomogeneous(viewReprojection) * m_viewPrevious.GetProjectionMatrix(false);

        sl::Constants consts = {};
        consts.cameraAspectRatio = static_cast<float>(displaySize.x) / static_cast<float>(displaySize.y);
        consts.cameraFOV = sceneCamera->verticalFov;
        consts.cameraFar = 200.0f;
        consts.cameraMotionIncluded = sl::Boolean::eTrue;
        consts.cameraNear = sceneCamera->zNear;
        consts.cameraPinholeOffset = { 0.f, 0.f };
        consts.cameraPos = SLWrapper::ToFloat3(m_scene->GetCamera().GetPosition());
        consts.cameraFwd = SLWrapper::ToFloat3(m_scene->GetCamera().GetDir());
        consts.cameraUp = SLWrapper::ToFloat3(m_scene->GetCamera().GetUp());
        consts.cameraRight = SLWrapper::ToFloat3(normalize(cross(m_scene->GetCamera().GetDir(), m_scene->GetCamera().GetUp())));
        consts.cameraViewToClip = SLWrapper::ToFloat4x4(projection);
        consts.clipToCameraView = SLWrapper::ToFloat4x4(inverse(projection));
        consts.clipToPrevClip = SLWrapper::ToFloat4x4(reprojectionMatrix);
        consts.depthInverted = isDepthInverted ? sl::Boolean::eTrue : sl::Boolean::eFalse;
        consts.jitterOffset = SLWrapper::ToFloat2(getCurrentPixelOffset(GetFrameIndex()));
        consts.mvecScale = { 1.0f / m_renderSize.x , 1.0f / m_renderSize.y }; // This are scale factors used to normalize mvec (to -1,1) and donut has mvec in pixel space
        consts.prevClipToClip = SLWrapper::ToFloat4x4(inverse(reprojectionMatrix));
        consts.reset = needNewPasses ? sl::Boolean::eTrue : sl::Boolean::eFalse;
        consts.motionVectors3D = sl::Boolean::eFalse;
        consts.motionVectorsInvalidValue = FLT_MIN;

        return consts;
    };

    if (m_ui.denoiserSelection == DenoiserSelection::DlssRr)
    {
        if (isDlssRrDirty())
        {
            m_dlssRrOptions.mode = m_ui.dlssQualityMode;
            m_dlssRrOptions.outputWidth = displaySize.x;
            m_dlssRrOptions.outputHeight = displaySize.y;
            m_dlssRrOptions.colorBuffersHDR = sl::Boolean::eTrue;
            m_dlssRrOptions.normalRoughnessMode = sl::DLSSDNormalRoughnessMode::ePacked;

            sl::DLSSDOptimalSettings dlssRrOptimalSettings;
            m_SL->GetDLSSRRSettings(m_dlssRrOptions, dlssRrOptimalSettings);

            m_dlssRrOptions.sharpness = dlssRrOptimalSettings.optimalSharpness;

            m_renderSize = dm::uint2(dlssRrOptimalSettings.optimalRenderWidth, dlssRrOptimalSettings.optimalRenderHeight);
        }

        updateView(m_renderSize.x, m_renderSize.y, true);

        sl::Constants dlssConstants = createDlssConstants(false);
        m_SL->SetConstants(dlssConstants);

        // DLSS-RR needs additional camera matrices when specular hit distance is provided
        dm::float4x4 worldToView = dm::affineToHomogeneous(m_scene->GetCamera().GetWorldToViewMatrix());
        m_dlssRrOptions.worldToCameraView = SLWrapper::ToFloat4x4(worldToView);
        m_dlssRrOptions.cameraViewToWorld = SLWrapper::ToFloat4x4(inverse(worldToView));

        m_SL->SetDLSSRROptions(m_dlssRrOptions);
    }
    else
    {
        m_renderSize = displaySize;
        updateView(m_renderSize.x, m_renderSize.y, true);
    }

    if (m_renderSize.x != m_resourceManager.GetRenderWidth() ||
        m_renderSize.y != m_resourceManager.GetRenderHeight())
    {
        m_resourceManager.CleanRenderTextures();
    }

    m_commandList->open();

    const bool isRecreateRenderTargets = displaySize.x != m_resourceManager.GetResolutionWidth() ||
                                         displaySize.y != m_resourceManager.GetResolutionHeight();
    const bool isRecreateRenderResolutionTextures = m_renderSize.x != m_resourceManager.GetRenderWidth() ||
                                                    m_renderSize.y != m_resourceManager.GetRenderHeight();
    if (isRecreateRenderTargets ||
        !renderTargets.pathTracerOutputTexture ||
        m_accelerationStructure->IsRebuildAS() ||
        m_accelerationStructure->IsUpdateAS() ||
        m_ui.recompileShader)
    {
        if (m_accelerationStructure->IsRebuildAS() || m_accelerationStructure->IsUpdateAS() || m_ui.recompileShader)
        {
            GetDevice()->waitForIdle();

            if (m_accelerationStructure->IsRebuildAS() || m_accelerationStructure->IsUpdateAS())
            {
                for (const auto& mesh : m_scene->GetNativeScene()->GetSceneGraph()->GetMeshes())
                {
                    m_commandList->beginTrackingBufferState(mesh->buffers->vertexBuffer, nvrhi::ResourceStates::AccelStructBuildInput);
                }
                m_accelerationStructure->CreateAccelerationStructures(m_commandList, GetFrameIndex());
            }

            if (m_ui.recompileShader)
            {
                // Compile the shaders
                system("cmake --build ..\\..\\..\\build --target pathtracer_shaders");

                // Clear Shader Cache
                m_shaderFactory->ClearCache();

                // Recompile shaders for PathTracing Passes
                m_gbufferPass->RecreateGBufferPassPipeline(m_bindlessLayout);
                m_pathTracingPass->ResetAccumulation();
                m_pathTracingPass->RecreateRayTracingPipeline(m_bindlessLayout);

                // NOTE: Do we need to do anything for DLSS if recompile shaders?

                // Recompile shaders for Postprocessing Passes
                m_postProcessingPass->RecompilePostProcessingShaders();

                // Recompile shaders for Morph Target Passes
                if (m_resourceManager.GetMorphTargetCount() > 0)
                {
                    m_morphTargetAnimationPass->RecompileMorphTargetAnimationShaders();
                }

                // Flip the flag back
                m_ui.recompileShader = false;
            }
        }

        if (isRecreateRenderTargets || !renderTargets.pathTracerOutputTexture)
        {
            m_resourceManager.RecreateScreenResolutionTextures(displaySize.x, displaySize.y);
            m_resourceManager.RecreateRenderResolutionTextures(m_renderSize.x, m_renderSize.y);
        }

        m_accelerationStructure->BuildTLAS(m_commandList);
    }
    else if (isRecreateRenderResolutionTextures)
    {
        m_resourceManager.RecreateRenderResolutionTextures(m_renderSize.x, m_renderSize.y);
    }

    {
        m_commandList->clearTextureFloat(renderTargets.pathTracerOutputTexture, nvrhi::AllSubresources, nvrhi::Color(0.0f));
        m_commandList->clearTextureFloat(renderTargets.postProcessingTexture, nvrhi::AllSubresources, nvrhi::Color(0.0f));
        if ( m_ui.denoiserSelection == DenoiserSelection::DlssRr)
        {
            m_commandList->clearTextureFloat(renderTargets.pathTracerOutputTextureDlssOutput, nvrhi::AllSubresources, nvrhi::Color(0.0f));
        }
        m_resourceManager.ClearDenoiserResources(m_commandList);
    }

    if (m_prevViewMatrix != m_view.GetViewMatrix())
    {
        m_pathTracingPass->ResetAccumulation();
        m_prevViewMatrix = m_view.GetViewMatrix();
    }

    if (m_ui.enableAnimations)
    {
        m_pathTracingPass->ResetAccumulation();
    }

    m_scene->GetNativeScene()->Refresh(m_commandList, GetFrameIndex());

    updateConstantBuffers();

    if (m_ui.enableAnimations && m_resourceManager.GetMorphTargetCount() > 0)
    {
        uint32_t morphTargetResourcesIndex = 0;
        for (const auto& mesh : m_scene->GetNativeScene()->GetSceneGraph()->GetMeshes())
        {
            m_morphTargetAnimationPass->Dispatch(
                mesh,
                m_commandList,
                m_resourceManager.GetMorphTargetResources()[morphTargetResourcesIndex],
                m_scene->GetCurveTessellationType(),
                std::max(1.0f / m_ui.animationFps, 0.001f),
                m_ui.enableAnimationDebugging,
                m_ui.animationKeyFrameIndexOverride,
                m_ui.animationKeyFrameWeightOverride);

            ++morphTargetResourcesIndex;
        }
    }

    m_gbufferPass->Dispatch(m_commandList,
                            renderTargets,
                            m_CommonPasses->m_AnisotropicWrapSampler,
                            m_descriptorTable,
                            m_renderSize,
                            m_resourceManager.IsEnvMapUpdated());

    m_pathTracingPass->Dispatch(m_commandList,
                                renderTargets,
                                m_CommonPasses->m_AnisotropicWrapSampler,
                                m_descriptorTable,
                                m_renderSize,
                                m_resourceManager.IsEnvMapUpdated());
    m_resourceManager.FinishUpdatingEnvMap();

    const bool enableDebugging = (m_ui.debugOutput != RtxcrDebugOutputType::None &&
                                  m_ui.debugOutput != RtxcrDebugOutputType::WhiteFurnace);
    if (!enableDebugging)
    {
        updateView(displaySize.x, displaySize.y, false);
        if (m_ui.enableDenoiser && m_ui.debugOutput != RtxcrDebugOutputType::WhiteFurnace)
        {
            if (m_ui.denoiserSelection == DenoiserSelection::DlssRr)
            {
                const auto& gBufferResources = renderTargets.gBufferResources;
                m_SL->TagDLSSRRBuffers(
                    m_commandList,
                    m_renderSize,
                    displaySize,
                    renderTargets.pathTracerOutputTexture,
                    gBufferResources.screenSpaceMotionVectorTexture,
                    gBufferResources.viewZTexture,
                    gBufferResources.albedoTexture,
                    gBufferResources.specularAlbedoTexture,
                    gBufferResources.shadingNormalRoughnessTexture,
                    GetDevice()->getGraphicsAPI() == nvrhi::GraphicsAPI::VULKAN ? nullptr : gBufferResources.specularHitDistanceTexture,
                    renderTargets.pathTracerOutputTextureDlssOutput
                );
                m_SL->EvaluateDLSSRR(m_commandList);

                m_commandList->close();
                GetDevice()->executeCommandList(m_commandList);

                m_commandList->open();
                nvrhi::TextureSlice textureSlice = {};
                m_commandList->copyTexture(renderTargets.postProcessingTexture, textureSlice, renderTargets.pathTracerOutputTextureDlssOutput, textureSlice);
                updateConstantBuffers();
            }
        }

        // DLSS Upscaling
        if (m_ui.denoiserSelection != DenoiserSelection::DlssRr)
        {
            const nvrhi::TextureSlice textureSlice = {};
            m_commandList->copyTexture(renderTargets.postProcessingTexture, textureSlice, renderTargets.pathTracerOutputTexture, textureSlice);
        }

        m_postProcessingPass->Dispatch(m_commandList, renderTargets, m_CommonPasses, framebuffer, m_view);
    }
    else // Debugging
    {
        m_CommonPasses->BlitTexture(m_commandList, framebuffer, renderTargets.pathTracerOutputTexture, m_bindingCache.get());
    }

    m_commandList->close();
    GetDevice()->executeCommandList(m_commandList);
    GetDevice()->waitForIdle();

    if (m_ui.denoiserSelection == DenoiserSelection::DlssRr)
    {
        m_SL->AdvanceFrame();
    }

    m_accelerationStructure->SetRebuildAS(false);
    m_accelerationStructure->SetUpdateAS(false);

    if (m_ui.captureScreenshot)
    {
        const ResourceManager::DebuggingResources& debuggingResources = m_resourceManager.GetDebuggingResources();
        const std::filesystem::path screenshotPath = app::GetDirectoryWithExecutable() / "screenshots/";
        std::string screenshotFileStr = screenshotPath.string();
        if (!strstr(m_ui.screenshotName, ".png"))
        {
            screenshotFileStr += std::string(m_ui.screenshotName) + ".png";
        }
        else if (strlen(m_ui.screenshotName) == 0 || strcmp(m_ui.screenshotName, ".png") == 0)
        {
            memcpy(m_ui.screenshotName, m_ui.defaultScreenShotName, strlen(m_ui.defaultScreenShotName) + 1);

            screenshotFileStr += std::string(m_ui.defaultScreenShotName);
        }
        else
        {
            screenshotFileStr += std::string(m_ui.screenshotName);
        }

        nvrhi::TextureHandle screenshotTexture = {};
        if (!enableDebugging)
        {
            if (m_ui.denoiserSelection == DenoiserSelection::DlssRr || m_ui.enableAccumulation == false)
            {
                screenshotTexture = renderTargets.postProcessingTexture;
            }
            else
            {
                screenshotTexture = renderTargets.accumulationTexture;
            }
        }
        else
        {
            screenshotTexture = renderTargets.pathTracerOutputTexture;
        }

        SaveTextureToFile(GetDevice(), m_CommonPasses.get(), screenshotTexture, nvrhi::ResourceStates::UnorderedAccess, screenshotFileStr.c_str());

        m_ui.captureScreenshot = false;
    }
}
