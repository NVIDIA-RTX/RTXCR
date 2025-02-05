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

#include <donut/app/ApplicationBase.h>
#include <donut/app/Camera.h>
#include <donut/core/vfs/VFS.h>
#include <donut/engine/BindingCache.h>
#include <donut/engine/ShaderFactory.h>
#include <donut/engine/Scene.h>
#include <donut/engine/View.h>

#include "SampleScene.h"
#include "ResourceManager.h"
#include "AccelerationStructure.h"
#include "RenderPass/GBufferPass.h"
#include "RenderPass/PathTracingPass.h"
#include "RenderPass/PostProcessingPass.h"
#include "RenderPass/MorphTargetAnimationPass.h"
#include "Denoiser/DlssRR/sl_wrapper.h"
#include "Ui/PathtracerUi.h"

static const char* g_WindowTitle = "RTXCR Sample";

class SampleRenderer : public donut::app::ApplicationBase
{
public:
	using ApplicationBase::ApplicationBase;

	struct PipelinePermutation
	{
		nvrhi::ShaderLibraryHandle shaderLibrary;
		nvrhi::rt::PipelineHandle pipeline;
		nvrhi::rt::ShaderTableHandle shaderTable;
	};

    SampleRenderer(donut::app::DeviceManager* deviceManager, UIData& ui, nvrhi::GraphicsAPI api);

	virtual bool LoadScene(std::shared_ptr<donut::vfs::IFileSystem> fs,
                           const std::filesystem::path& sceneFileName) override;
	virtual void SceneUnloading() override;
	virtual void SceneLoaded() override;

	void Animate(float fElapsedTimeSeconds) override;
	void BackBufferResizing() override;

	virtual void Render(nvrhi::IFramebuffer* framebuffer) override;

    /*
    * Control Functions
    */
    virtual bool KeyboardUpdate(int key, int scancode, int action, int mods) override
    {
        m_scene->GetCamera().KeyboardUpdate(key, scancode, action, mods);

        if (key == GLFW_KEY_ESCAPE && action == GLFW_RELEASE)
        {
            m_ui.showUI = !m_ui.showUI;
        }

        return true;
    }

    virtual bool MousePosUpdate(double xpos, double ypos) override
    {
        m_scene->GetCamera().MousePosUpdate(xpos, ypos);
        return true;
    }

    virtual bool MouseButtonUpdate(int button, int action, int mods) override
    {
        m_scene->GetCamera().MouseButtonUpdate(button, action, mods);
        return true;
    }

    virtual bool MouseScrollUpdate(double xoffset, double yoffset) override
    {
        m_scene->GetCamera().MouseScrollUpdate(xoffset, yoffset);
        return true;
    }

	/*
	* Helper Functions
    */
    bool Init(int argc, const char* const* argv);

    void SetCurrentSceneNameAndLoading(const std::string& sceneName);

    bool SetCurrentEnvironmentMapAndLoading(const std::string& envMapName);

    inline std::shared_ptr<donut::engine::ShaderFactory> GetShaderFactory()
    {
        return m_shaderFactory;
    }

    inline std::shared_ptr<donut::vfs::IFileSystem> GetRootFS() const
    {
        return m_rootFileSystem;
    }

    inline std::shared_ptr<donut::engine::TextureCache> GetTextureCache()
    {
        return m_TextureCache;
    }

    inline void RebuildAccelerationStructure()
    {
        m_accelerationStructure->SetRebuildAS(true);
    }

    inline void ResetAccumulation()
    {
        m_pathTracingPass->ResetAccumulation();
    }

    inline std::shared_ptr<SampleScene> GetScene() const
    {
        return m_scene;
    }

    inline donut::app::FirstPersonCamera* GetCamera()
    {
        return &m_scene->GetCamera();
    }

	inline std::string GetResolutionInfo()
	{
		return m_resourceManager.GetResolutionInfo();
	}

private:
    void updateView(const donut::math::uint viewportWidth, const donut::math::uint viewportHeight, const bool updatePreviousView);
    void updateConstantBuffers();

    UIData& m_ui;

	std::shared_ptr<donut::vfs::RootFileSystem> m_rootFileSystem;
    std::shared_ptr<donut::vfs::NativeFileSystem> m_nativeFileSystem;

    std::shared_ptr<donut::engine::ShaderFactory> m_shaderFactory;
    std::shared_ptr<donut::engine::DescriptorTableManager> m_descriptorTable;

	nvrhi::CommandListHandle m_commandList;
    nvrhi::BindingLayoutHandle m_bindlessLayout;

    std::unique_ptr<donut::engine::BindingCache> m_bindingCache;

    std::shared_ptr<SampleScene> m_scene;
    ResourceManager m_resourceManager;

    std::shared_ptr<AccelerationStructure> m_accelerationStructure;

    std::unique_ptr<GBufferPass> m_gbufferPass;
    std::unique_ptr<PathTracingPass> m_pathTracingPass;
    std::unique_ptr<PostProcessingPass> m_postProcessingPass;
    std::unique_ptr<MorphTargetAnimationPass> m_morphTargetAnimationPass;

    std::unique_ptr<SLWrapper>  m_SL;
    sl::DLSSDOptions            m_dlssRrOptions;

    dm::uint2                   m_renderSize;
    donut::engine::PlanarView   m_view;
    donut::engine::PlanarView   m_viewPrevious;

	int m_frameIndex = 0;

	dm::affine3 m_prevViewMatrix;

	nvrhi::GraphicsAPI m_api;
};
