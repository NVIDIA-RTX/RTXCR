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
#include <donut/render/TemporalAntiAliasingPass.h>
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

#include "Denoiser/NRD/NrdConfig.h"

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

SampleRenderer::SampleRenderer(DeviceManager* deviceManager, UIData& ui)
    : ApplicationBase(deviceManager)
    , m_ui(ui)
    , m_resourceManager(GetDevice(),
                        deviceManager->GetBackBuffer(0)->getDesc().width,
                        deviceManager->GetBackBuffer(0)->getDesc().height,
                        deviceManager->GetBackBuffer(0)->getDesc().width,
                        deviceManager->GetBackBuffer(0)->getDesc().height)
    , m_renderSize(0u)
    , m_previousDenoiserSelection(ui.denoiserSelection)
    , m_previousUpscalerSelection(ui.upscalerSelection)
    , m_previousViewsValid(false)
{
    m_commandList = GetDevice()->createCommandList();
}

SampleRenderer::~SampleRenderer() = default;

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

        if (!strcmp(arg, "-enableAnimation"))
        {
            m_ui.enableAnimations = (bool)atoi(argv[n + 1]);
            if (m_ui.enableAnimations)
            {
                m_ui.showAnimationUI = true;
            }
        }

        if (!strcmp(arg, "-animationKeyframeIndex"))
        {
            m_ui.enableAnimationDebugging = true;
            m_ui.animationKeyFrameIndexOverride = atoi(argv[n + 1]);
        }

        if (!strcmp(arg, "-animationKeyframeWeight"))
        {
            m_ui.enableAnimationDebugging = true;
            m_ui.animationKeyFrameWeightOverride = (float)atof(argv[n + 1]);
        }

        if (!strcmp(arg, "-forceLambertianBrdf"))
        {
            m_ui.forceLambertianBRDF = (bool)atoi(argv[n + 1]);
        }

        if (!strcmp(arg, "-denoiser"))
        {
            const int denoiser = atoi(argv[n + 1]);
            if (denoiser <= 2)
            {
                m_ui.denoiserSelection = (DenoiserSelection) denoiser;
            }
        }

        if (!strcmp(arg, "-nrdMode"))
        {
            const int denoiser = atoi(argv[n + 1]);
            if (denoiser <= 1)
            {
                m_ui.nrdDenoiserMode = (NrdMode)denoiser;
            }
        }

        if (!strcmp(arg, "-enableDlss"))
        {
            const bool enableDlss = (bool)atoi(argv[n + 1]);
            if (enableDlss)
            {
                m_ui.upscalerSelection = UpscalerSelection::DLSS;
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

    if (!GetDevice()->queryFeatureSupport(nvrhi::Feature::LinearSweptSpheres) &&
        m_ui.hairTessellationType == TessellationType::LinearSweptSphere)
    {
        m_ui.hairTessellationType = TessellationType::DisjointOrthogonalTriangleStrip;
    }

    // Fallback to NRD and TAA when DLSS is NOT supported
    if (!SLWrapper::IsDLSSSupported())
    {
        if (m_ui.denoiserSelection == DenoiserSelection::DlssRr)
        {
            m_ui.denoiserSelection = DenoiserSelection::Nrd;
        }
        if (m_ui.upscalerSelection == UpscalerSelection::DLSS)
        {
            m_ui.upscalerSelection = UpscalerSelection::TAA;
        }
    }

    m_nativeFileSystem = std::make_shared<vfs::NativeFileSystem>();
    const std::filesystem::path frameworkShaderPath = app::GetDirectoryWithExecutable() / "shaders/framework" / app::GetShaderTypeName(GetDevice()->getGraphicsAPI());
    const std::filesystem::path appShaderPath = app::GetDirectoryWithExecutable() / "shaders/pathtracer" / app::GetShaderTypeName(GetDevice()->getGraphicsAPI());
    const std::filesystem::path mediaDir = app::GetDirectoryWithExecutable().parent_path() / "assets";
    const std::filesystem::path nrdShaderPath = app::GetDirectoryWithExecutable() / "shaders/nrd" / app::GetShaderTypeName(GetDevice()->getGraphicsAPI());

    m_rootFileSystem = std::make_shared<vfs::RootFileSystem>();
    m_rootFileSystem->mount("/shaders/donut", frameworkShaderPath);
    m_rootFileSystem->mount("/shaders/app", appShaderPath);
    m_rootFileSystem->mount("/native", m_nativeFileSystem);
    m_rootFileSystem->mount("/assets", mediaDir);
    m_rootFileSystem->mount("/shaders/nrd", nrdShaderPath);

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

    // Create Denoiser
    m_nrdDenoiser = std::make_unique<NrdDenoiser>(GetDevice(), m_shaderFactory, m_resourceManager, m_ui);

    {
        m_gbufferPass->CreateGBufferPassPipeline(m_bindlessLayout);
        m_pathTracingPass->CreateRayTracingPipeline(m_bindlessLayout);
        m_postProcessingPass->CreatePostProcessingPipelines();
        m_nrdDenoiser->CreateDenoiserPipelines();
    }

    // Create Morph Target Buffers
    // Note: Don't check m_resourceManager.GetMorphTargetCount() here,
    //       because we need to loop the meshes in CreateMorphTargetBuffers to determine how many morph targets we have
    // if (m_resourceManager.GetMorphTargetCount() > 0)
    {
        m_resourceManager.CreateMorphTargetBuffers(m_scene, m_commandList);
    }

    // Reflex
    if (SLWrapper::IsDLSSSupported() && SLWrapper::IsReflexSupported())
    {
        // Set the callbacks for Reflex
        GetDeviceManager()->m_callbacks.beforeFrame   = [&](donut::app::DeviceManager& m, uint32_t f) { SLWrapper::ReflexCallback_Sleep(m, f); };
        GetDeviceManager()->m_callbacks.beforeAnimate = [&](donut::app::DeviceManager& m, uint32_t f) { SLWrapper::ReflexCallback_SimStart(m, f); };
        GetDeviceManager()->m_callbacks.afterAnimate  = [&](donut::app::DeviceManager& m, uint32_t f) { SLWrapper::ReflexCallback_SimEnd(m, f); };
        GetDeviceManager()->m_callbacks.beforeRender  = [&](donut::app::DeviceManager& m, uint32_t f) { SLWrapper::ReflexCallback_RenderStart(m, f); };
        GetDeviceManager()->m_callbacks.afterRender   = [&](donut::app::DeviceManager& m, uint32_t f) { SLWrapper::ReflexCallback_RenderEnd(m, f); };
        GetDeviceManager()->m_callbacks.beforePresent = [&](donut::app::DeviceManager& m, uint32_t f) { SLWrapper::ReflexCallback_PresentStart(m, f); };
        GetDeviceManager()->m_callbacks.afterPresent  = [&](donut::app::DeviceManager& m, uint32_t f) { SLWrapper::ReflexCallback_PresentEnd(m, f); };
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

    if (m_resourceManager.GetMorphTargetCount() > 0)
    {
        m_morphTargetAnimationPass->ResetAnimation();
    }
}

void SampleRenderer::SceneLoaded()
{
    ApplicationBase::SceneLoaded();

    m_scene->FinishLoading(GetDevice(), m_descriptorTable.get(), GetFrameIndex());

    m_pathTracingPass->ResetAccumulation();

    m_accelerationStructure->SetRebuildAS(true);

    // Recreate morph target buffers to fit for the new scene
    if (m_commandList)
    {
        m_resourceManager.RecreateMorphTargetBuffers(m_scene, m_commandList);
    }

    if (m_resourceManager.GetMorphTargetCount() > 0)
    {
        if (!m_morphTargetAnimationPass)
        {
            m_morphTargetAnimationPass = std::make_unique<MorphTargetAnimationPass>(GetDevice(), m_shaderFactory);
        }

        m_morphTargetAnimationPass->CreateMorphTargetAnimationPipeline(m_scene->GetCurveTessellationType());
    }
    else
    {
        m_morphTargetAnimationPass = nullptr;
        m_resourceManager.CleanMorphTargetTextures();
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
    if (m_scene->Animate(GetDevice(), m_descriptorTable.get(), fElapsedTimeSeconds, IsSceneLoaded(), GetFrameIndex(), m_ui.lockCamera, &isRebuildAsAfterAnimation))
    {
        if (m_resourceManager.GetMorphTargetCount() > 0)
        {
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
        m_nrdDenoiser->ResetDenoiser();

        m_previousViewsValid = false;
    }

    const double frameTime = GetDeviceManager()->GetAverageFrameTimeSeconds() / (double)std::max(m_ui.dlfgNumFramesActuallyPresented, 1);
    std::string frameRate;
    if (frameTime > 0.0)
    {
        double const fps = 1.0 / frameTime;
        int const precision = (fps <= 20.0) ? 1 : 0;
        std::ostringstream fpsOss;
        fpsOss << std::fixed << std::setprecision(1) << fps;
        frameRate = std::string(" - ") + fpsOss.str() + " FPS ";
    }
    GetDeviceManager()->SetInformativeWindowTitle(g_WindowTitle, false, frameRate.c_str());
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
        if (m_ui.denoiserSelection == DenoiserSelection::DlssRr || m_ui.upscalerSelection != UpscalerSelection::TAA)
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
            globalConstants.jitterOffset = m_taaPass->GetCurrentPixelOffset();
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
    globalConstants.enableAccumulation = m_ui.enableAccumulation && m_ui.denoiserSelection != DenoiserSelection::DlssRr && m_ui.upscalerSelection == UpscalerSelection::None;
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
    if (globalConstants.enableDenoiser)
    {
        nrd::HitDistanceParameters hitDistanceParameters;
        globalConstants.nrdHitDistanceParams = (float4&)hitDistanceParameters;
    }
    globalConstants.enableDlssRR = (m_ui.denoiserSelection == DenoiserSelection::DlssRr) ? 1 : 0;

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
    {
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
    }

    // Animation
    globalConstants.enableAnimation = m_ui.enableAnimations;

    globalConstants.targetLight = m_ui.targetLight;
    globalConstants.debugOutputMode = m_ui.debugOutput;
    globalConstants.debugScale = m_ui.debugScale;
    globalConstants.debugMin = m_ui.debugMinMax[0];
    globalConstants.debugMax = m_ui.debugMinMax[1];

    globalConstants.enableDenoiserValidationLayer = m_ui.nrdCommonSettings.enableValidation;

    m_commandList->writeBuffer(renderTargets.globalArgs, &globalConstants, sizeof(globalConstants));
}

void SampleRenderer::BackBufferResizing()
{
    m_resourceManager.CleanTextures();

    m_bindingCache->Clear();

    m_accelerationStructure->ClearTLAS();
    m_accelerationStructure->SetRebuildAS(true);

    m_pathTracingPass->ResetAccumulation();

    m_nrdDenoiser->ResetDenoiser();

    m_previousViewsValid = false;
}

void SampleRenderer::Render(nvrhi::IFramebuffer* framebuffer)
{
    const ResourceManager::PathTracerResources& renderTargets = m_resourceManager.GetPathTracerResources();

    m_scene->RefreshSceneGraph(GetFrameIndex());

    const auto& fbinfo = framebuffer->getFramebufferInfo();
    const dm::uint2 displaySize = dm::uint2(fbinfo.width, fbinfo.height);

    auto isDlssRrDirty = [&]() -> bool
    {
        return m_dlssRrOptions.mode != m_ui.dlssrrQualityMode
            || m_dlssRrOptions.outputWidth != displaySize.x
            || m_dlssRrOptions.outputHeight != displaySize.y
            || isDenoiserSelectionDirty();
    };

    auto isDlssSrDirty = [&]() -> bool
    {
        return m_dlssSrOptions.mode != m_ui.dlsssrQualityMode
            || m_dlssSrOptions.outputWidth != displaySize.x
            || m_dlssSrOptions.outputHeight != displaySize.y
            || isDenoiserSelectionDirty();
    };

    auto createDlssConstants = [&](bool isDepthInverted) -> sl::Constants
    {
        float aspectRatio = static_cast<float>(displaySize.x) / static_cast<float>(displaySize.y);

        const auto sceneCamera = (PerspectiveCamera*)m_scene->GetNativeScene()->GetSceneGraph()->GetCameras().at(0).get();
        dm::float4x4 projection = dm::perspProjD3DStyleReverse(sceneCamera->verticalFov, aspectRatio, sceneCamera->zNear);

        const bool isRecreateRenderTargets = displaySize.x != m_resourceManager.GetResolutionWidth() || displaySize.y != m_resourceManager.GetResolutionHeight();
        bool needNewPasses = isRecreateRenderTargets || !renderTargets.pathTracerOutputTexture || m_accelerationStructure->IsRebuildAS();

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

    if (isDlssEnabled())
    {
        if (m_ui.denoiserSelection == DenoiserSelection::DlssRr)
        {
            if (isDlssRrDirty())
            {
                m_dlssRrOptions.mode = m_ui.dlssrrQualityMode;
                m_dlssRrOptions.outputWidth = displaySize.x;
                m_dlssRrOptions.outputHeight = displaySize.y;
                m_dlssRrOptions.colorBuffersHDR = sl::Boolean::eTrue;
                m_dlssRrOptions.normalRoughnessMode = sl::DLSSDNormalRoughnessMode::ePacked;

                sl::DLSSDOptimalSettings dlssRrOptimalSettings;
                SLWrapper::GetDLSSRRSettings(m_dlssRrOptions, dlssRrOptimalSettings);

                m_dlssRrOptions.sharpness = dlssRrOptimalSettings.optimalSharpness;

                m_renderSize = dm::uint2(dlssRrOptimalSettings.optimalRenderWidth, dlssRrOptimalSettings.optimalRenderHeight);
            }

            updateView(m_renderSize.x, m_renderSize.y, true);

            sl::Constants dlssConstants = createDlssConstants(false);
            SLWrapper::SetConstants(dlssConstants);

            // DLSS-RR needs additional camera matrices when specular hit distance is provided
            dm::float4x4 worldToView = dm::affineToHomogeneous(m_scene->GetCamera().GetWorldToViewMatrix());
            m_dlssRrOptions.worldToCameraView = SLWrapper::ToFloat4x4(worldToView);
            m_dlssRrOptions.cameraViewToWorld = SLWrapper::ToFloat4x4(inverse(worldToView));

            SLWrapper::SetDLSSRROptions(m_dlssRrOptions);
        }
        else if (m_ui.upscalerSelection == UpscalerSelection::DLSS)
        {
            if (isDlssSrDirty())
            {
                m_dlssSrOptions.mode = m_ui.dlsssrQualityMode;
                m_dlssSrOptions.outputWidth = displaySize.x;
                m_dlssSrOptions.outputHeight = displaySize.y;
                m_dlssSrOptions.colorBuffersHDR = sl::Boolean::eTrue;
                m_dlssSrOptions.useAutoExposure = sl::Boolean::eTrue;

                sl::DLSSOptimalSettings dlssSrOptimalSettings;
                SLWrapper::GetDLSSSettings(m_dlssSrOptions, dlssSrOptimalSettings);

                m_dlssSrOptions.sharpness = dlssSrOptimalSettings.optimalSharpness;

                m_renderSize = dm::uint2(dlssSrOptimalSettings.optimalRenderWidth, dlssSrOptimalSettings.optimalRenderHeight);
            }

            updateView(m_renderSize.x, m_renderSize.y, true);

            sl::Constants dlssConstants = createDlssConstants(false);
            SLWrapper::SetConstants(dlssConstants);

            SLWrapper::SetDLSSOptions(m_dlssSrOptions);
        }
        else
        {
            m_renderSize = displaySize;
            updateView(m_renderSize.x, m_renderSize.y, true);
        }
    }
    else
    {
        if (m_ui.upscalerSelection == UpscalerSelection::TAA && m_taaPass)
        {
            m_taaPass->SetJitter(m_temporalAntiAliasingJitter);
        }
        m_renderSize = displaySize;
        updateView(m_renderSize.x, m_renderSize.y, true);
    }

    if (SLWrapper::IsDLSSSupported() &&
        m_ui.enableDlfg &&
        m_ui.denoiserSelection != DenoiserSelection::DlssRr &&
        m_ui.upscalerSelection != UpscalerSelection::DLSS)
    {
        sl::Constants dlssConstants = createDlssConstants(false);
        SLWrapper::SetConstants(dlssConstants);
    }

    // REFLEX
    if (SLWrapper::IsDLSSSupported() && SLWrapper::IsReflexSupported())
    {
        auto reflexConst = sl::ReflexOptions{};
        reflexConst.mode = m_ui.reflexMode;
        reflexConst.useMarkersToOptimize = true;
        reflexConst.virtualKey = VK_F13;
        reflexConst.frameLimitUs = 0;
        SLWrapper::SetReflexConsts(reflexConst);
    }

    // DLSS-G/FG
    if (SLWrapper::IsDLSSGSupported())
    {
        bool prevDlssgWanted = false;
        SLWrapper::Get_DLSSG_SwapChainRecreation(prevDlssgWanted);

        m_ui.dlfgNumFramesActuallyPresented = 1;

        if (prevDlssgWanted != m_ui.enableDlfg)
        {
            SLWrapper::Set_DLSSG_SwapChainRecreation(m_ui.enableDlfg);
        }

        int minSize = 0;
        uint64_t estimatedVramUsage = 0;
        sl::DLSSGStatus status = sl::DLSSGStatus::eOk;
        void* pDLSSGInputsProcessingFence{};
        uint64_t lastPresentDLSSGInputsProcessingFenceValue{};
        auto lastDLSSGFenceValue = SLWrapper::GetDLSSGLastFenceValue();
        if (m_ui.enableDlfg)
        {
            SLWrapper::QueryDLSSGState(
                estimatedVramUsage,
                m_ui.dlfgNumFramesActuallyPresented,
                status,
                minSize,
                m_ui.dlfgMaxNumFramesToGenerate,
                pDLSSGInputsProcessingFence,
                lastPresentDLSSGInputsProcessingFenceValue);
        }

        sl::DLSSGOptions dlssgOptions = {};
        if (!m_ui.enableDlfg ||
            static_cast<int>(framebuffer->getFramebufferInfo().width) < minSize ||
            static_cast<int>(framebuffer->getFramebufferInfo().height) < minSize)
        {
            if (m_ui.enableDlfg)
            {
                donut::log::info("Swapchain is too small. DLSSG is disabled.");
            }
            dlssgOptions.mode = sl::DLSSGMode::eOff;
        }
        else
        {
            dlssgOptions.mode = sl::DLSSGMode::eOn;
            // Explicitly manage DLSS-G resources in order to prevent stutter when temporarily disabled.
            dlssgOptions.flags |= sl::DLSSGFlags::eRetainResourcesWhenOff;
        }
        dlssgOptions.numFramesToGenerate = std::min(m_ui.dlfgNumFramesToGenerate - 1, m_ui.dlfgMaxNumFramesToGenerate);
        SLWrapper::SetDLSSGOptions(dlssgOptions);

        if (m_ui.enableDlfg)
        {
            const auto fenceValue = lastPresentDLSSGInputsProcessingFenceValue;
            SLWrapper::QueryDLSSGState(
                estimatedVramUsage,
                m_ui.dlfgNumFramesActuallyPresented,
                status,
                minSize,
                m_ui.dlfgMaxNumFramesToGenerate,
                pDLSSGInputsProcessingFence,
                lastPresentDLSSGInputsProcessingFenceValue);
            assert(fenceValue == lastPresentDLSSGInputsProcessingFenceValue);

            if (pDLSSGInputsProcessingFence != nullptr)
            {
                const bool dlssgEnabledLastFrame = (m_dlssgOptions.mode != sl::DLSSGMode::eOff);
                if (dlssgEnabledLastFrame)
                {
                    if (lastPresentDLSSGInputsProcessingFenceValue == 0 || lastPresentDLSSGInputsProcessingFenceValue > lastDLSSGFenceValue)
                    {
                        // This wait is redundant until SL DLSS FG allows SMSCG but done for now for demonstration purposes.
                        // It needs to be queued before any of the inputs are modified in the subsequent command list submission.
                        SLWrapper::QueueGPUWaitOnSyncObjectSet(GetDevice(), nvrhi::CommandQueue::Graphics, pDLSSGInputsProcessingFence, lastPresentDLSSGInputsProcessingFenceValue);
                    }
                }
                else
                {
                    if (lastPresentDLSSGInputsProcessingFenceValue < lastDLSSGFenceValue)
                    {
                        assert(false);
                        log::warning("Inputs synchronization fence value retrieved from DLSSGState object out of order: \
                         current frame: %ld, last frame: %ld ", lastPresentDLSSGInputsProcessingFenceValue, lastDLSSGFenceValue);
                    }
                    else if (lastPresentDLSSGInputsProcessingFenceValue != 0)
                    {
                        log::info("DLSSG was inactive in the last presenting frame!");
                    }
                }
            }
        }

        m_dlssgOptions = dlssgOptions;
    }

    if (m_renderSize.x != m_resourceManager.GetRenderWidth() ||
        m_renderSize.y != m_resourceManager.GetRenderHeight())
    {
        m_resourceManager.CleanRenderTextures();
    }

    m_commandList->open();

    const bool isRecreateRenderTargets = displaySize.x != m_resourceManager.GetResolutionWidth() ||
                                         displaySize.y != m_resourceManager.GetResolutionHeight() ||
                                         !renderTargets.pathTracerOutputTexture;
    const bool isRecreateRenderResolutionTextures = m_renderSize.x != m_resourceManager.GetRenderWidth() ||
                                                    m_renderSize.y != m_resourceManager.GetRenderHeight();

    if (m_accelerationStructure->IsRebuildAS() || m_accelerationStructure->IsUpdateAS() || m_ui.recompileShader)
    {
        if (m_accelerationStructure->IsRebuildAS() || m_accelerationStructure->IsUpdateAS())
        {
            if (m_accelerationStructure->IsRebuildAS())
            {
                GetDevice()->waitForIdle();
            }

            for (const auto& mesh : m_scene->GetNativeScene()->GetSceneGraph()->GetMeshes())
            {
                m_commandList->beginTrackingBufferState(mesh->buffers->vertexBuffer, nvrhi::ResourceStates::AccelStructBuildInput);
            }
            m_accelerationStructure->CreateAccelerationStructures(m_commandList, GetFrameIndex());

            m_accelerationStructure->BuildTLAS(m_commandList);
        }

        if (m_ui.recompileShader)
        {
            // Compile the shaders
            system("cmake --build ..\\..\\..\\build --target pathtracer_shaders --target nrd_shaders");

            // Clear Shader Cache
            m_shaderFactory->ClearCache();

            // Recompile shaders for PathTracing Passes
            m_gbufferPass->RecreateGBufferPassPipeline(m_bindlessLayout);
            m_pathTracingPass->ResetAccumulation();
            m_pathTracingPass->RecreateRayTracingPipeline(m_bindlessLayout);

            // Recompile Denoiser
            m_nrdDenoiser->RecreateDenoiserPipelines();

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

    if (isRecreateRenderTargets)
    {
        m_resourceManager.RecreateScreenResolutionTextures(displaySize.x, displaySize.y);
        m_resourceManager.RecreateRenderResolutionTextures(m_renderSize.x, m_renderSize.y);
    }
    else if (isRecreateRenderResolutionTextures)
    {
        m_resourceManager.RecreateRenderResolutionTextures(m_renderSize.x, m_renderSize.y);
    }

    // Check if we need to recreate NRD resources or release NRD resources
    if (m_ui.denoiserSelection == DenoiserSelection::Nrd)
    {
        if (isDenoiserSelectionDirty() || isRecreateRenderTargets)
        {
            m_nrdDenoiser->RecreateNrdTextures(m_renderSize);
        }
    }
    else
    {
        if (isDenoiserSelectionDirty() && m_previousDenoiserSelection == DenoiserSelection::Nrd)
        {
            m_nrdDenoiser->CleanDenoiserTextures();
        }
    }

    if (m_ui.upscalerSelection == UpscalerSelection::TAA)
    {
        if (!m_taaPass || isRecreateRenderTargets)
        {
            // Recreate TAA Pass
            const auto& renderTargets = m_resourceManager.GetPathTracerResources();
            const auto& gBufferResources = renderTargets.gBufferResources;

            donut::render::TemporalAntiAliasingPass::CreateParameters taaParams{};
            taaParams.sourceDepth = gBufferResources.deviceZTexture;
            taaParams.motionVectors = gBufferResources.motionVectorTexture;
            taaParams.unresolvedColor = renderTargets.pathTracerOutputTexture;
            taaParams.resolvedColor = renderTargets.pathTracerOutputTextureDlssOutput;
            taaParams.feedback1 = m_resourceManager.GetTaaResources().taaFeedback1;
            taaParams.feedback2 = m_resourceManager.GetTaaResources().taaFeedback2;
            taaParams.useCatmullRomFilter = true;

            m_taaPass = std::make_unique<donut::render::TemporalAntiAliasingPass>(GetDevice(), m_shaderFactory, m_CommonPasses, m_view, taaParams);
            m_taaPass->SetJitter(donut::render::TemporalAntiAliasingJitter::Halton);
        }
    }
    else
    {
        if (isUpscalerSelectionDirty() && m_previousUpscalerSelection == UpscalerSelection::TAA)
        {
            m_taaPass = nullptr;
        }
    }

    {
        m_commandList->clearTextureFloat(renderTargets.pathTracerOutputTexture, nvrhi::AllSubresources, nvrhi::Color(0.0f));
        m_commandList->clearTextureFloat(renderTargets.postProcessingTexture, nvrhi::AllSubresources, nvrhi::Color(0.0f));
        if (m_ui.upscalerSelection != UpscalerSelection::None || m_ui.denoiserSelection == DenoiserSelection::DlssRr)
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
                m_ui.animationKeyFrameWeightOverride,
                m_ui.enableAnimationSmoothing ? m_ui.animationSmoothingFactor : 1.0f);

            ++morphTargetResourcesIndex;
        }
    }

    m_gbufferPass->Dispatch(m_commandList,
                            renderTargets, m_resourceManager.GetDenoiserResources(),
                            m_CommonPasses->m_AnisotropicWrapSampler,
                            m_descriptorTable,
                            m_renderSize,
                            m_resourceManager.IsEnvMapUpdated());

    m_pathTracingPass->Dispatch(m_commandList,
                                renderTargets, m_resourceManager.GetDenoiserResources(),
                                m_CommonPasses->m_AnisotropicWrapSampler,
                                m_descriptorTable,
                                m_renderSize,
                                m_resourceManager.IsEnvMapUpdated());
    m_resourceManager.FinishUpdatingEnvMap();

    // General Tagging
    if (SLWrapper::IsDLSSSupported())
    {
        const auto& gBufferResources = renderTargets.gBufferResources;
        SLWrapper::TagDLSSGeneralBuffers(
            m_commandList,
            m_renderSize,
            displaySize,
            gBufferResources.screenSpaceMotionVectorTexture,
            gBufferResources.viewZTexture);
    }

    const bool enableDebugging = (m_ui.debugOutput != RtxcrDebugOutputType::None &&
                                  m_ui.debugOutput != RtxcrDebugOutputType::WhiteFurnace);
    if (!enableDebugging)
    {
        if (m_ui.enableDenoiser && m_ui.debugOutput != RtxcrDebugOutputType::WhiteFurnace)
        {
            if (m_ui.denoiserSelection == DenoiserSelection::Nrd)
            {
                m_nrdDenoiser->Dispatch(m_commandList, m_renderSize, m_view, m_viewPrevious, GetFrameIndex());
            }
            else if (m_ui.denoiserSelection == DenoiserSelection::DlssRr)
            {
                const auto& gBufferResources = renderTargets.gBufferResources;
                SLWrapper::TagDLSSRRBuffers(
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
                SLWrapper::EvaluateDLSSRR(m_commandList);

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
            if (m_ui.upscalerSelection == UpscalerSelection::DLSS)
            {
                const auto& gBufferResources = renderTargets.gBufferResources;
                SLWrapper::TagDLSSBuffers(m_commandList,
                    m_renderSize,
                    displaySize,
                    renderTargets.pathTracerOutputTexture,
                    gBufferResources.screenSpaceMotionVectorTexture,
                    gBufferResources.deviceZTexture,
                    false,
                    nullptr,
                    renderTargets.pathTracerOutputTextureDlssOutput);

                SLWrapper::EvaluateDLSS(m_commandList);

                m_commandList->close();
                GetDevice()->executeCommandList(m_commandList);

                m_commandList->open();
                const nvrhi::TextureSlice textureSlice = {};
                m_commandList->copyTexture(renderTargets.postProcessingTexture, textureSlice, renderTargets.pathTracerOutputTextureDlssOutput, textureSlice);
                updateConstantBuffers();
            }
            else if (m_ui.upscalerSelection == UpscalerSelection::TAA)
            {
                const auto taaInputView = m_view;
                updateView(displaySize.x, displaySize.y, false);

                m_taaPass->TemporalResolve(
                    m_commandList, m_temporalAntiAliasingParams, m_previousViewsValid, taaInputView, m_previousViewsValid ? m_viewPrevious : m_view);

                m_commandList->close();
                GetDevice()->executeCommandList(m_commandList);

                m_commandList->open();
                const nvrhi::TextureSlice textureSlice = {};
                m_commandList->copyTexture(renderTargets.postProcessingTexture, textureSlice, renderTargets.pathTracerOutputTextureDlssOutput, textureSlice);
                updateConstantBuffers();
            }
            else
            {
                const nvrhi::TextureSlice textureSlice = {};
                m_commandList->copyTexture(renderTargets.postProcessingTexture, textureSlice, renderTargets.pathTracerOutputTexture, textureSlice);
            }
        }

        updateView(displaySize.x, displaySize.y, false);
        m_postProcessingPass->Dispatch(
            m_commandList, renderTargets, m_resourceManager.GetDenoiserResources().validationTexture, m_CommonPasses, framebuffer, m_view);
    }
    else // Debugging
    {
        m_CommonPasses->BlitTexture(m_commandList, framebuffer, renderTargets.pathTracerOutputTexture, m_bindingCache.get());
    }

    m_commandList->close();
    GetDevice()->executeCommandList(m_commandList);

    if (SLWrapper::IsDLSSSupported() &&
        (m_ui.denoiserSelection == DenoiserSelection::DlssRr || m_ui.upscalerSelection == UpscalerSelection::DLSS || m_ui.enableDlfg))
    {
        SLWrapper::AdvanceFrame();
    }

    if (m_ui.upscalerSelection == UpscalerSelection::TAA)
    {
        m_taaPass->AdvanceFrame();
        m_previousViewsValid = true;
    }
    else
    {
        m_previousViewsValid = false;
    }

    // Update Flags
    m_accelerationStructure->SetRebuildAS(false);
    m_accelerationStructure->SetUpdateAS(false);
    m_previousDenoiserSelection = m_ui.denoiserSelection;
    m_previousUpscalerSelection = m_ui.upscalerSelection;

    // Swap Dynamic Vertex Buffer
    if (m_ui.enableAnimations && m_resourceManager.GetMorphTargetCount() > 0)
    {
        m_scene->GetCurveTessellation()->swapDynamicVertexBuffer();
    }

    if (m_ui.captureScreenshot)
    {
        const ResourceManager::DebuggingResources& debuggingResources = m_resourceManager.GetDebuggingResources();
        std::string screenshotFileStr = "../../../bin/screenshots/";
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
