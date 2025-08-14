/*
 * Copyright (c) 2024-2025, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#include <donut/app/ApplicationBase.h>

#include "Ui/PathtracerUi.h"
#include "SampleScene.h"

using namespace donut;
using namespace donut::math;
using namespace donut::app;
using namespace donut::engine;

static constexpr const char* const s_DefaultSceneFile = "assets/claire.scene.json";
static constexpr const char* const s_DefaultSceneFileExt = ".scene.json";
static constexpr const char* const s_DefaultEnvMapFile = "assets/EnvironmentMaps/venice_sunset_1k.exr";
static const std::vector<std::string> s_DefaultEnvMapExt = { ".dds", ".exr", ".png" };
static constexpr dm::double3 s_DefaultSunDirection(-0.791f, -0.259f, -0.554f);

SampleScene::SampleScene(const uint32_t initialFrameIndex,
                         const float cameraSpeed,
                         const uint32_t cameraIndex,
                         const bool enableAsyncSceneLoading,
                         const char* const defaultSceneOverride,
                         UIData& ui)
 : m_enableAsyncSceneLoading (enableAsyncSceneLoading)
 , m_currentScene(app::GetDirectoryWithExecutable().parent_path())
 , m_currentEnvMap(app::GetDirectoryWithExecutable().parent_path())
 , m_cameraIndex(cameraIndex)
 , m_wallclockTime(0.0f)
 , m_ui(ui)
 , m_currentTessellationType(ui.hairTessellationType)
{
    const char* const defaultScene = !defaultSceneOverride ? s_DefaultSceneFile : defaultSceneOverride;
    m_currentScene /= defaultScene;

    m_currentEnvMap /= s_DefaultEnvMapFile;

    importSceneFiles("assets");

    m_camera.SetMoveSpeed(ui.cameraSpeed);
}

bool SampleScene::Load(nvrhi::IDevice* device,
                       std::shared_ptr<ShaderFactory> shaderFactory,
                       std::shared_ptr<donut::vfs::IFileSystem> fs,
                       std::shared_ptr<TextureCache> textureCache,
                       std::shared_ptr<DescriptorTableManager> descriptorTable,
                       std::shared_ptr<SceneTypeFactory> sceneTypeFactory,
                       const std::filesystem::path& sceneFileName)
{
    engine::Scene* scene = new engine::Scene(device, *shaderFactory, fs, textureCache, descriptorTable, nullptr);

    if (scene->Load(sceneFileName))
    {
        m_scene = std::unique_ptr<engine::Scene>(scene);
        m_curveTessellation = std::make_shared<CurveTessellation>(m_scene->GetSceneGraph()->GetMeshInstances(), m_ui);
        return true;
    }

    return false;
}

void SampleScene::FinishLoading(nvrhi::IDevice* device, donut::engine::DescriptorTableManager* descriptorTable, const uint32_t frameIndex)
{
    // Tessellate curve line segments into Polytubes/DOTS/LSS and cache them on CPU
    m_curveTessellation->convertToTrianglePolyTubes(m_scene->GetSceneGraph()->GetMeshInstances());
    m_curveTessellation->convertToDisjointOrthogonalTriangleStrips(m_scene->GetSceneGraph()->GetMeshInstances());
    m_curveTessellation->convertToLinearSweptSpheres(m_scene->GetSceneGraph()->GetMeshInstances());

    // Ensure that the currently chosen tessellation type is ready to be uploaded to the GPU
    m_curveTessellation->replacingSceneMesh(device, descriptorTable, m_currentTessellationType, m_scene->GetSceneGraph()->GetMeshInstances());

    m_scene->FinishedLoading(frameIndex);

    for (auto light : m_scene->GetSceneGraph()->GetLights())
    {
        if (light->GetLightType() == LightType_Directional)
        {
            m_sunLight = std::static_pointer_cast<DirectionalLight>(light);
            break;
        }
    }

    if (!m_sunLight)
    {
        m_sunLight = std::make_shared<DirectionalLight>();
        m_sunLight->angularSize = 0.8f;
        m_sunLight->irradiance = 20.f;
        auto node = std::make_shared<SceneGraphNode>();
        node->SetLeaf(m_sunLight);
        m_sunLight->SetDirection(s_DefaultSunDirection);
        m_sunLight->SetName("Sun");
        m_scene->GetSceneGraph()->Attach(m_scene->GetSceneGraph()->GetRootNode(), node);
    }

    auto cameras = m_scene->GetSceneGraph()->GetCameras();
    if (!cameras.empty())
    {
        // Override camera
        if (m_cameraIndex != -1 && m_cameraIndex < cameras.size())
        {
            m_ui.activeSceneCamera = cameras[m_cameraIndex];
            m_cameraIndex = -1;
        }
        else
        {
            std::string cameraName = "DefaultCamera";
            auto it = std::find_if(cameras.begin(), cameras.end(), [cameraName](std::shared_ptr<donut::engine::SceneCamera> const& camera) {
                return camera->GetName() == cameraName;
                });
            it != cameras.end() ? m_ui.activeSceneCamera = *it : m_ui.activeSceneCamera = cameras[0];
        }

        // Copy active camera to FirstPersonCamera
        if (m_ui.activeSceneCamera)
        {
            dm::affine3 viewToWorld = m_ui.activeSceneCamera->GetViewToWorldMatrix();
            dm::float3 cameraPos = viewToWorld.m_translation;
            m_camera.LookAt(cameraPos, cameraPos + viewToWorld.m_linear.row2, viewToWorld.m_linear.row1);
        }
    }
    else
    {
        m_ui.activeSceneCamera.reset();
        m_camera.LookAt(float3(0.f, 1.8f, 0.f), float3(1.f, 1.8f, 0.f));
    }
}

void SampleScene::Unload()
{
    m_sunLight = nullptr;
    m_headLight = nullptr;

    m_curveTessellation->clear();
}

bool SampleScene::Animate(
    nvrhi::IDevice* device,
    donut::engine::DescriptorTableManager* descriptorTable,
    const float fElapsedTimeSeconds,
    const bool isSceneLoaded,
    const uint32_t frameIndex,
    const bool lockCamera,
    bool* isRebuildAsAfterAnimation)
{
    static bool enableCam = false;

    m_camera.SetRotateSpeed(enableCam ? 1e-3f : 0.0f);

    if (!lockCamera)
    {
        m_camera.Animate(fElapsedTimeSeconds);
    }
    enableCam = true;

    if (isSceneLoaded && m_ui.enableAnimations)
    {
        m_wallclockTime += fElapsedTimeSeconds;
        float offset = 0;

        for (const auto& animation : m_scene->GetSceneGraph()->GetAnimations())
        {
            float duration = animation->GetDuration();
            float integral;
            float animationTime = std::modf((m_wallclockTime + offset) / duration, &integral) * duration;
            (void)animation->Apply(animationTime);
            offset += 1.0f;
        }
    }

    if (m_currentTessellationType != m_ui.hairTessellationType)
    {
        m_currentTessellationType = m_ui.hairTessellationType;
        m_curveTessellation->replacingSceneMesh(device, descriptorTable, m_currentTessellationType, m_scene->GetSceneGraph()->GetMeshInstances());

        m_scene->FinishedLoading(frameIndex);

        *isRebuildAsAfterAnimation = true;

        return true;
    }

    return m_ui.enableAnimations;
}

void SampleScene::importSceneFiles(const std::string& mediaFolder)
{
    static std::filesystem::path mediaFolderPath;

    const std::filesystem::path candidatePathA = app::GetDirectoryWithExecutable() / mediaFolder;
    const std::filesystem::path candidatePathB = app::GetDirectoryWithExecutable().parent_path() / mediaFolder;
    if (std::filesystem::exists(candidatePathA))
    {
        mediaFolderPath = candidatePathA;
    }
    else
    {
        mediaFolderPath = candidatePathB;
    }

    for (const auto& file : std::filesystem::directory_iterator(mediaFolderPath))
    {
        if (!file.is_regular_file()) continue;
        const std::string fileName = file.path().filename().string();
        const std::string longExt = (fileName.size() <= strlen(s_DefaultSceneFileExt)) ? ("") : (fileName.substr(fileName.length() - strlen(s_DefaultSceneFileExt)));
        if (longExt == s_DefaultSceneFileExt)
        {
            m_sceneFilesAvailable.push_back(file.path().string());
        }
    }

    // Import Environment Maps
    const std::filesystem::path envMapPath = mediaFolderPath / "EnvironmentMaps";
    for (const auto& envMapFile : std::filesystem::directory_iterator(envMapPath))
    {
        const std::string envMapFileName = envMapFile.path().filename().string();

        for (const auto& envMapExt : s_DefaultEnvMapExt)
        {
            const std::string envMapLongExt = (envMapFileName.size() <= strlen(envMapExt.c_str())) ? ("") : (envMapFileName.substr(envMapFileName.length() - strlen(envMapExt.c_str())));
            if (envMapLongExt == envMapExt)
            {
                m_envMapFilesAvailable.push_back(envMapFile.path().string());
            }
        }
    }
}
