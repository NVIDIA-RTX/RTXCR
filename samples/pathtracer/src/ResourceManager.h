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

#include <nvrhi/nvrhi.h>
#include <donut/core/math/math.h>
#include <donut/engine/TextureCache.h>

class SampleScene;

class ResourceManager
{
public:
    ResourceManager(
        nvrhi::IDevice* const device,
        const uint32_t screenWidth,
        const uint32_t screenHeight,
        const uint32_t renderWidth,
        const uint32_t renderHeight);

    ~ResourceManager() = default;

    void CreateBuffers();
    void CreateScreenResolutionTextures();
    void CreateRenderResolutionTextures();
    void CreateMorphTargetBuffers(
        const std::shared_ptr<SampleScene> scene,
        nvrhi::CommandListHandle commandList);
    void CreateEnvironmentMap(donut::engine::TextureCache* textureCache, const std::string& envMapFileName);

    void CleanRenderTextures();
    void CleanMorphTargetTextures();
    void CleanTextures();

    void RecreateScreenResolutionTextures(const uint32_t screenWidth, const uint32_t screenHeight);
    void RecreateRenderResolutionTextures(const uint32_t renderWidth, const uint32_t renderHeight);
    void RecreateMorphTargetBuffers(const std::shared_ptr<SampleScene> scene, nvrhi::CommandListHandle commandList);

    void ClearGBuffer(nvrhi::CommandListHandle commandList);
    void ClearDenoiserResources(nvrhi::CommandListHandle commandList);

    inline bool IsEnvMapUpdated() const { return m_pathTracerResources.isEnvMapUpdated; }
    inline void FinishUpdatingEnvMap() { m_pathTracerResources.isEnvMapUpdated = false; }

    struct PathTracerResources
    {
        nvrhi::BufferHandle  globalArgs;
        nvrhi::BufferHandle  lightConstantsBuffer;
        nvrhi::TextureHandle pathTracerOutputTexture;
        nvrhi::TextureHandle pathTracerOutputTextureDlssOutput;
        nvrhi::TextureHandle postProcessingTexture;
        nvrhi::TextureHandle accumulationTexture;

        std::shared_ptr<donut::engine::LoadedTexture> environmentMapTexture;

        struct GBufferResources
        {
            nvrhi::TextureHandle positionTexture;
            nvrhi::TextureHandle geometryNormalTexture;
            nvrhi::TextureHandle geometryTangentTexture;
            nvrhi::TextureHandle viewZTexture;
            nvrhi::TextureHandle motionVectorTexture;
            nvrhi::TextureHandle screenSpaceMotionVectorTexture;
            nvrhi::TextureHandle emissiveTexture;
            nvrhi::TextureHandle shadingNormalRoughnessTexture;
            nvrhi::TextureHandle albedoTexture;
            nvrhi::TextureHandle specularAlbedoTexture;
            nvrhi::TextureHandle specularHitDistanceTexture;
            nvrhi::TextureHandle deviceZTexture;
        } gBufferResources;

        bool isEnvMapUpdated = false;
    };

    struct DebuggingResources
    {
        nvrhi::StagingTextureHandle dumpTexture;
    };

    struct MorphTargetResources
    {
        nvrhi::BufferHandle morphTargetConstantBuffer;
        nvrhi::BufferHandle morphTargetDataBuffer;
        nvrhi::BufferHandle lineSegmentsBuffer;
        uint32_t vertexSize = 0;
    };

    inline const PathTracerResources& GetPathTracerResources() const { return m_pathTracerResources; }
    inline const PathTracerResources::GBufferResources& GetGBufferResources() const { return m_pathTracerResources.gBufferResources; }
    inline const std::vector<MorphTargetResources>& GetMorphTargetResources() const { return m_morphTargetResources; }
    inline const MorphTargetResources& GetMorphTargetResources(const uint32_t meshIndex) const { return m_morphTargetResources[meshIndex]; }
    inline const DebuggingResources& GetDebuggingResources() const { return m_debuggingResources; }
    inline uint32_t GetResolutionWidth() const { return m_screenWidth; }
    inline uint32_t GetResolutionHeight() const { return m_screenHeight; }
    inline uint32_t GetRenderWidth() const { return m_renderWidth; }
    inline uint32_t GetRenderHeight() const { return m_renderHeight; }
    inline const std::string GetResolutionInfo() const { return std::to_string(m_screenWidth) + " x " + std::to_string(m_screenHeight); }
    inline uint32_t GetMorphTargetCount() const { return m_totalMorphTargetCount; }

private:
    nvrhi::TextureHandle createRenderTargetTexture(const uint32_t width, const uint32_t height, const std::string& name, const nvrhi::Format format);
    nvrhi::BufferHandle createBuffer(const uint32_t byteSize, const uint32_t strideSize, const std::string& name, const bool isUav, const bool isRawBuffer);

    nvrhi::IDevice* const m_device;

    uint32_t m_screenWidth;
    uint32_t m_screenHeight;
    uint32_t m_renderWidth;
    uint32_t m_renderHeight;

    PathTracerResources m_pathTracerResources;
    DebuggingResources m_debuggingResources;
    std::vector<MorphTargetResources> m_morphTargetResources;
    uint32_t m_totalMorphTargetCount;
};
