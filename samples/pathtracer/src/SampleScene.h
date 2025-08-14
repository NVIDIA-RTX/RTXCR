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

#include <donut/app/Camera.h>
#include <donut/core/vfs/VFS.h>
#include <donut/engine/Scene.h>
#include "Curve/CurveTessellation.h"

struct UIData;

class SampleScene
{
public:
    SampleScene(const uint32_t initialFrameIndex,
                const float cameraSpeed,
                const uint32_t cameraIndex,
                const bool enableAsyncSceneLoading,
                const char* const defaultSceneOverride,
                UIData& ui);

    ~SampleScene() = default;

    bool Load(nvrhi::IDevice* device,
              std::shared_ptr<donut::engine::ShaderFactory> shaderFactory,
              std::shared_ptr<donut::vfs::IFileSystem> fs,
              std::shared_ptr<donut::engine::TextureCache> textureCache,
              std::shared_ptr<donut::engine::DescriptorTableManager> descriptorTable,
              std::shared_ptr<donut::engine::SceneTypeFactory> sceneTypeFactory,
              const std::filesystem::path& sceneFileName);

    void FinishLoading(nvrhi::IDevice* device, donut::engine::DescriptorTableManager* descriptorTable, const uint32_t frameIndex);

    void Unload();

    bool Animate(
        nvrhi::IDevice* device,
        donut::engine::DescriptorTableManager* descriptorTable,
        const float fElapsedTimeSeconds,
        const bool isSceneLoaded,
        const uint32_t frameIndex,
        const bool lockCamera,
        bool* isRebuildAsAfterAnimation);

    inline void SetCurrentSceneName(const std::string& sceneName)
    {
        if (m_currentScene != sceneName)
        {
            m_currentScene = sceneName;
        }
    }

    inline void SetPreferredSceneName(const std::string& sceneName)
    {
        SetCurrentSceneName(donut::app::FindPreferredScene(m_sceneFilesAvailable, sceneName));
    }

    inline void SetCurrentEnvMapName(const std::string& envMapName)
    {
        if (m_currentEnvMap != envMapName)
        {
            m_currentEnvMap = envMapName;
        }
    }

    inline void RefreshSceneGraph(const uint32_t frameIndex)
    {
        m_scene->RefreshSceneGraph(frameIndex);
    }

    inline void SetAsyncSceneLoading(const bool enableAsyncSceneLoading)
    {
        m_enableAsyncSceneLoading = enableAsyncSceneLoading;
    }

    inline void SetCameraSpeed(const float cameraSpeed)
    {
        m_camera.SetMoveSpeed(cameraSpeed);
    }

    inline std::shared_ptr<donut::engine::Scene> GetNativeScene() const { return m_scene; }
    inline const std::string GetCurrentSceneName() const { return m_currentScene.string(); }
    inline std::vector<std::string> const& GetAvailableScenes() const
    {
        return m_sceneFilesAvailable;
    }
    inline const std::string GetCurrentEnvMapName() const { return m_currentEnvMap.string(); }
    inline std::vector<std::string> const& GetAvailableEnvMaps() const
    {
        return m_envMapFilesAvailable;
    }

    inline donut::app::FirstPersonCamera& GetCamera() { return m_camera; }
    inline const donut::app::FirstPersonCamera& GetCamera() const { return m_camera; }
    inline std::shared_ptr<donut::engine::DirectionalLight> GetSunlight() const { return m_sunLight; }
    inline std::shared_ptr<CurveTessellation> GetCurveTessellation() const { return m_curveTessellation; }

    inline const bool IsAsyncSceneLoadingEnabled() const { return m_enableAsyncSceneLoading; }

    inline TessellationType GetCurveTessellationType() const { return m_currentTessellationType; }

private:
    void importSceneFiles(const std::string& mediaFolder);

    std::shared_ptr<donut::engine::Scene> m_scene;

    std::filesystem::path m_currentScene;
    std::vector<std::string> m_sceneFilesAvailable;

    donut::app::FirstPersonCamera m_camera;
    uint32_t m_cameraIndex;

    std::shared_ptr<donut::engine::DirectionalLight> m_sunLight;
    std::shared_ptr<donut::engine::PointLight> m_headLight;

    std::filesystem::path m_currentEnvMap;
    std::vector<std::string> m_envMapFilesAvailable;

    TessellationType m_currentTessellationType;
    std::shared_ptr<CurveTessellation> m_curveTessellation;

    bool m_enableAsyncSceneLoading;
    float m_wallclockTime;

    UIData& m_ui;
};
