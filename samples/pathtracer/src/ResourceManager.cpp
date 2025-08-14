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
#include <donut/engine/CommonRenderPasses.h>
#include <nvrhi/utils.h>

#include "ResourceManager.h"
#include "SampleScene.h"
#include "shared.h"

using namespace donut;
using namespace donut::math;
using namespace donut::engine;

#include "../shared/globalCb.h"
#include "../shared/lightingCb.h"

ResourceManager::ResourceManager(nvrhi::IDevice* const device,
                                 const uint32_t screenWidth,
                                 const uint32_t screenHeight,
                                 const uint32_t renderWidth,
                                 const uint32_t renderHeight)
: m_device(device)
, m_screenWidth(screenWidth)
, m_screenHeight(screenHeight)
, m_renderWidth(renderWidth)
, m_renderHeight(renderHeight)
, m_totalMorphTargetCount(0)
{
}

void ResourceManager::CreateBuffers()
{
    m_pathTracerResources.globalArgs = m_device->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(
        sizeof(GlobalConstants), "GlobalConstants", donut::engine::c_MaxRenderPassConstantBufferVersions));

    m_pathTracerResources.lightConstantsBuffer = m_device->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(
        sizeof(LightingConstants), "LightingConstants", donut::engine::c_MaxRenderPassConstantBufferVersions));
}

void ResourceManager::CreateScreenResolutionTextures()
{
    m_pathTracerResources.postProcessingTexture = createRenderTargetTexture(m_screenWidth, m_screenHeight, "Post Processing Texture", nvrhi::Format::RGBA32_FLOAT);
    m_pathTracerResources.accumulationTexture = createRenderTargetTexture(m_screenWidth, m_screenHeight, "AccumulateTexture", nvrhi::Format::RGBA32_FLOAT);

    // Output Texture
    {
        nvrhi::TextureDesc desc;
        desc.dimension = nvrhi::TextureDimension::Texture2D;
        desc.width = m_screenWidth;
        desc.height = m_screenHeight;
        desc.sampleCount = 1;
        desc.isUAV = true;
        desc.keepInitialState = true;
        desc.format = nvrhi::Format::RGBA32_FLOAT;
        desc.isRenderTarget = true;
        desc.initialState = nvrhi::ResourceStates::RenderTarget;
        desc.debugName = "PathTracerDlssOutput";
        m_pathTracerResources.pathTracerOutputTextureDlssOutput = m_device->createTexture(desc);
    }

    // Screenshot Texture
    {
        nvrhi::TextureDesc dumpTextureDesc;
        dumpTextureDesc.dimension = nvrhi::TextureDimension::Texture2D;
        dumpTextureDesc.width = m_screenWidth;
        dumpTextureDesc.height = m_screenHeight;
        dumpTextureDesc.sampleCount = 1;
        dumpTextureDesc.isUAV = false;
        dumpTextureDesc.keepInitialState = true;
        dumpTextureDesc.format = nvrhi::Format::RGBA32_FLOAT;
        dumpTextureDesc.initialState = nvrhi::ResourceStates::CopyDest;
        dumpTextureDesc.debugName = "Dump Texture";
        m_debuggingResources.dumpTexture = m_device->createStagingTexture(dumpTextureDesc, nvrhi::CpuAccessMode::Read);
    }

    // TAA Textures
    {
        nvrhi::TextureDesc taaTextureDesc;
        taaTextureDesc.dimension = nvrhi::TextureDimension::Texture2D;
        taaTextureDesc.width = m_screenWidth;
        taaTextureDesc.height = m_screenHeight;
        taaTextureDesc.sampleCount = 1;
        taaTextureDesc.isUAV = true;
        taaTextureDesc.keepInitialState = true;
        taaTextureDesc.format = nvrhi::Format::RGBA16_SNORM;
        taaTextureDesc.isRenderTarget = true;
        taaTextureDesc.initialState = nvrhi::ResourceStates::RenderTarget;
        taaTextureDesc.keepInitialState = true;
        taaTextureDesc.useClearValue = true;
        taaTextureDesc.clearValue = nvrhi::Color(0.0f);

        taaTextureDesc.debugName = "TAA Feedback 1 Texture";
        m_taaResources.taaFeedback1 = m_device->createTexture(taaTextureDesc);

        taaTextureDesc.debugName = "TAA Feedback 2 Texture";
        m_taaResources.taaFeedback2 = m_device->createTexture(taaTextureDesc);
    }
}

void ResourceManager::CreateRenderResolutionTextures()
{
    m_pathTracerResources.pathTracerOutputTexture = createRenderTargetTexture(m_renderWidth, m_renderHeight, "PathTracerOutput", nvrhi::Format::RGBA32_FLOAT);

    m_pathTracerResources.gBufferResources.positionTexture = createRenderTargetTexture(m_renderWidth, m_renderHeight, "Position", nvrhi::Format::RGBA16_FLOAT);
    m_pathTracerResources.gBufferResources.geometryNormalTexture = createRenderTargetTexture(m_renderWidth, m_renderHeight, "Geometry Normal", nvrhi::Format::RGBA16_FLOAT);
    m_pathTracerResources.gBufferResources.geometryTangentTexture = createRenderTargetTexture(m_renderWidth, m_renderHeight, "Geometry Tangent", nvrhi::Format::RGBA16_FLOAT);
    m_pathTracerResources.gBufferResources.viewZTexture = createRenderTargetTexture(m_renderWidth, m_renderHeight, "ViewZ", nvrhi::Format::R16_FLOAT);
    m_pathTracerResources.gBufferResources.motionVectorTexture = createRenderTargetTexture(m_renderWidth, m_renderHeight, "Motion Vector", nvrhi::Format::RGBA16_FLOAT);
    m_pathTracerResources.gBufferResources.screenSpaceMotionVectorTexture = createRenderTargetTexture(m_renderWidth, m_renderHeight, "Screen Space Motion Vector", nvrhi::Format::RG16_FLOAT);
    m_pathTracerResources.gBufferResources.emissiveTexture = createRenderTargetTexture(m_renderWidth, m_renderHeight, "Emissive", nvrhi::Format::RGBA16_FLOAT);
    m_pathTracerResources.gBufferResources.shadingNormalRoughnessTexture = createRenderTargetTexture(m_renderWidth, m_renderHeight, "Shading Normal Roughness", nvrhi::Format::RGBA16_FLOAT);
    m_pathTracerResources.gBufferResources.albedoTexture = createRenderTargetTexture(m_renderWidth, m_renderHeight, "Albedo", nvrhi::Format::RGBA8_UNORM);
    m_pathTracerResources.gBufferResources.specularAlbedoTexture = createRenderTargetTexture(m_renderWidth, m_renderHeight, "Specular Albedo", nvrhi::Format::RGBA16_FLOAT);

    m_pathTracerResources.gBufferResources.specularHitDistanceTexture = createRenderTargetTexture(m_renderWidth, m_renderHeight, "Specular HitT", nvrhi::Format::R16_FLOAT);
    m_pathTracerResources.gBufferResources.deviceZTexture = createRenderTargetTexture(m_renderWidth, m_renderHeight, "DeviceZ", nvrhi::Format::R16_FLOAT);

    m_denoiserResources.noisyDiffuseRadianceHitT = createRenderTargetTexture(m_renderWidth, m_renderHeight, "Noisy Diffuse Radiance HitT", nvrhi::Format::RGBA16_FLOAT);
    m_denoiserResources.noisySpecularRadianceHitT = createRenderTargetTexture(m_renderWidth, m_renderHeight, "Noisy Specular Radiance HitT", nvrhi::Format::RGBA16_FLOAT);
    m_denoiserResources.denoisedDiffuseRadianceHitT = createRenderTargetTexture(m_renderWidth, m_renderHeight, "Denoised Diffuse Radiance HitT", nvrhi::Format::RGBA16_FLOAT);
    m_denoiserResources.denoisedSpecularRadianceHitT = createRenderTargetTexture(m_renderWidth, m_renderHeight, "Denoised Diffuse Radiance HitT", nvrhi::Format::RGBA16_FLOAT);
    m_denoiserResources.validationTexture = createRenderTargetTexture(m_renderWidth, m_renderHeight, "Denoiser Validation Texture", nvrhi::Format::RGBA8_UNORM);
}

void ResourceManager::CreateMorphTargetBuffers(
    const std::shared_ptr<SampleScene> scene,
    nvrhi::CommandListHandle commandList)
{
    for (const auto& mesh : scene->GetNativeScene()->GetSceneGraph()->GetMeshes())
    {
        const auto& lineSegments = scene->GetCurveTessellation()->GetCurvesLineSegments(mesh->name);
        if (mesh->isMorphTargetAnimationMesh && lineSegments.size() > 0)
        {
            MorphTargetResources morphTargetResource = {};

            const std::string bufferIndexName = std::to_string(mesh->globalMeshIndex);

            commandList->open();
            {
                const auto& morphTargetFrameData = mesh->buffers->morphTargetData;
                const uint32_t morphTargetFrameDataSize = sizeof(float4) * morphTargetFrameData.size();
                morphTargetResource.morphTargetDataBuffer = createBuffer(morphTargetFrameDataSize, sizeof(float4), "Morph Target Data Buffer " + bufferIndexName, false, true);

                commandList->beginTrackingBufferState(morphTargetResource.morphTargetDataBuffer, nvrhi::ResourceStates::Common);
                commandList->writeBuffer(morphTargetResource.morphTargetDataBuffer, morphTargetFrameData.data(), morphTargetFrameDataSize);
                commandList->commitBarriers();
            }

            const uint lineSegmentsSize = sizeof(LineSegment) * lineSegments.size();
            if (scene->GetCurveTessellationType() == TessellationType::Polytube)
            {
                morphTargetResource.vertexSize = lineSegments.size() * RTXCR_CURVE_POLYTUBE_ORDER * 6;
            }
            else if (scene->GetCurveTessellationType() == TessellationType::DisjointOrthogonalTriangleStrip)
            {
                morphTargetResource.vertexSize = lineSegments.size() * 3 * 4;
            }
            else if (scene->GetCurveTessellationType() == TessellationType::LinearSweptSphere)
            {
                morphTargetResource.vertexSize = mesh->totalVertices;
            }

            {
                std::vector<LineSegment> lineSegmentsBufferData(lineSegments.size());
                for (uint segmentIndex = 0; segmentIndex < lineSegments.size(); ++segmentIndex)
                {
                    const auto& segment = lineSegments[segmentIndex];
                    auto& lineSegmentData = lineSegmentsBufferData[segmentIndex];
                    lineSegmentData.geometryIndex = segment.geometryIndex;
                    lineSegmentData.point0 = float3(segment.vertices[0].position);
                    lineSegmentData.radius0 = segment.vertices[0].radius;
                    lineSegmentData.point1 = float3(segment.vertices[1].position);
                    lineSegmentData.radius1 = segment.vertices[1].radius;
                }
                morphTargetResource.lineSegmentsBuffer = createBuffer(lineSegmentsSize, sizeof(LineSegment), "Mesh Line Segments Buffer " + bufferIndexName, false, false);
                commandList->beginTrackingBufferState(morphTargetResource.lineSegmentsBuffer, nvrhi::ResourceStates::Common);
                commandList->writeBuffer(morphTargetResource.lineSegmentsBuffer, lineSegmentsBufferData.data(), lineSegmentsSize);
                commandList->setPermanentBufferState(morphTargetResource.lineSegmentsBuffer, nvrhi::ResourceStates::ShaderResource);
                commandList->commitBarriers();
            }

            {
                morphTargetResource.morphTargetConstantBuffer = m_device->createBuffer(nvrhi::utils::CreateStaticConstantBufferDesc(
                    sizeof(MorphTargetConstants), "MorphTargetConstants"));

                MorphTargetConstants morphTargetConstants = {};
                morphTargetConstants.vertexCount = morphTargetResource.vertexSize;
                commandList->beginTrackingBufferState(morphTargetResource.morphTargetConstantBuffer, nvrhi::ResourceStates::Common);
                commandList->writeBuffer(morphTargetResource.morphTargetConstantBuffer, &morphTargetConstants, sizeof(MorphTargetConstants));
            }

            commandList->close();
            m_device->executeCommandList(commandList);

            m_morphTargetResources.push_back(morphTargetResource);

            ++m_totalMorphTargetCount;
        }
        else
        {
            m_morphTargetResources.push_back(MorphTargetResources());
        }
    }

    {
        const auto& meshInstances = scene->GetNativeScene()->GetSceneGraph()->GetMeshInstances();
        std::vector<uint32_t> morphTargetMaskData(meshInstances.size(), 0);
        for (uint32_t meshInstanceIndex = 0; meshInstanceIndex < meshInstances.size(); ++meshInstanceIndex)
        {
            const auto& mesh = meshInstances.at(meshInstanceIndex)->GetMesh();
            if (!mesh->buffers->morphTargetData.empty())
            {
                morphTargetMaskData[meshInstanceIndex] = 1;
            }
        }

        m_pathTracerResources.instanceMorphTargetMetaDataBuffer =
            createBuffer(morphTargetMaskData.size() * sizeof(uint32_t), sizeof(uint32_t), "Instance Morph Target Meta Data", false, false);

        commandList->open();
        commandList->beginTrackingBufferState(m_pathTracerResources.instanceMorphTargetMetaDataBuffer, nvrhi::ResourceStates::Common);
        commandList->writeBuffer(m_pathTracerResources.instanceMorphTargetMetaDataBuffer, morphTargetMaskData.data(), morphTargetMaskData.size());
        commandList->setPermanentBufferState(m_pathTracerResources.instanceMorphTargetMetaDataBuffer, nvrhi::ResourceStates::ShaderResource);
        commandList->commitBarriers();
        commandList->close();
        m_device->executeCommandList(commandList);
    }
}

void ResourceManager::CreateEnvironmentMap(donut::engine::TextureCache* textureCache, const std::string& envMapFileName)
{
    const std::filesystem::path environmentMapPath = app::GetDirectoryWithExecutable() / envMapFileName.c_str();

    m_pathTracerResources.environmentMapTexture = textureCache->LoadTextureFromFileDeferred(environmentMapPath, false);
    m_pathTracerResources.isEnvMapUpdated = true;
}

void ResourceManager::CleanRenderTextures()
{
    m_pathTracerResources.pathTracerOutputTexture = nullptr;
    m_pathTracerResources.gBufferResources.positionTexture = nullptr;
    m_pathTracerResources.gBufferResources.geometryNormalTexture = nullptr;
    m_pathTracerResources.gBufferResources.geometryTangentTexture = nullptr;
    m_pathTracerResources.gBufferResources.viewZTexture = nullptr;
    m_pathTracerResources.gBufferResources.motionVectorTexture = nullptr;
    m_pathTracerResources.gBufferResources.screenSpaceMotionVectorTexture = nullptr;
    m_pathTracerResources.gBufferResources.emissiveTexture = nullptr;
    m_pathTracerResources.gBufferResources.shadingNormalRoughnessTexture = nullptr;
    m_pathTracerResources.gBufferResources.albedoTexture = nullptr;
    m_pathTracerResources.gBufferResources.specularAlbedoTexture = nullptr;
    m_pathTracerResources.gBufferResources.specularHitDistanceTexture = nullptr;
    m_pathTracerResources.gBufferResources.deviceZTexture = nullptr;
}

void ResourceManager::CleanMorphTargetTextures()
{
    for (auto& morphTargetResource : m_morphTargetResources)
    {
        morphTargetResource.morphTargetConstantBuffer = nullptr;
        morphTargetResource.morphTargetDataBuffer = nullptr;
        morphTargetResource.lineSegmentsBuffer = nullptr;
        morphTargetResource.vertexSize = 0;
    }

    m_morphTargetResources.clear();

    m_totalMorphTargetCount = 0;
}

void ResourceManager::CleanTextures()
{
    m_pathTracerResources.accumulationTexture = nullptr;
    CleanRenderTextures();
}

void ResourceManager::RecreateScreenResolutionTextures(const uint32_t screenWidth, const uint32_t screenHeight)
{
    m_screenWidth = screenWidth;
    m_screenHeight = screenHeight;

    CreateScreenResolutionTextures();
}

void ResourceManager::RecreateRenderResolutionTextures(const uint32_t renderWidth, const uint32_t renderHeight)
{
    m_renderWidth = renderWidth;
    m_renderHeight = renderHeight;

    CreateRenderResolutionTextures();
}

void ResourceManager::RecreateMorphTargetBuffers(const std::shared_ptr<SampleScene> scene, nvrhi::CommandListHandle commandList)
{
    CleanMorphTargetTextures();

    CreateMorphTargetBuffers(scene, commandList);
}

void ResourceManager::ClearGBuffer(nvrhi::CommandListHandle commandList)
{
    auto& gBufferResources = m_pathTracerResources.gBufferResources;
    commandList->clearTextureFloat(gBufferResources.positionTexture, nvrhi::AllSubresources, nvrhi::Color(0.0f));
    commandList->clearTextureFloat(gBufferResources.geometryNormalTexture, nvrhi::AllSubresources, nvrhi::Color(0.0f));
    commandList->clearTextureFloat(gBufferResources.geometryTangentTexture, nvrhi::AllSubresources, nvrhi::Color(0.0f));
    commandList->clearTextureFloat(gBufferResources.viewZTexture, nvrhi::AllSubresources, nvrhi::Color(0.0f));
    commandList->clearTextureFloat(gBufferResources.emissiveTexture, nvrhi::AllSubresources, nvrhi::Color(0.0f));
    commandList->clearTextureFloat(gBufferResources.albedoTexture, nvrhi::AllSubresources, nvrhi::Color(0.0f));
    commandList->clearTextureFloat(gBufferResources.specularAlbedoTexture, nvrhi::AllSubresources, nvrhi::Color(0.0f));
    commandList->clearTextureFloat(gBufferResources.motionVectorTexture, nvrhi::AllSubresources, nvrhi::Color(0.0f));
    commandList->clearTextureFloat(gBufferResources.screenSpaceMotionVectorTexture, nvrhi::AllSubresources, nvrhi::Color(0.0f));
    commandList->clearTextureFloat(gBufferResources.shadingNormalRoughnessTexture, nvrhi::AllSubresources, nvrhi::Color(0.0f));
    commandList->clearTextureFloat(gBufferResources.specularHitDistanceTexture, nvrhi::AllSubresources, nvrhi::Color(0.0f));
    commandList->clearTextureFloat(gBufferResources.deviceZTexture, nvrhi::AllSubresources, nvrhi::Color(0.0f));
}

void ResourceManager::ClearDenoiserResources(nvrhi::CommandListHandle commandList)
{
    auto& gBufferResources = m_pathTracerResources.gBufferResources;
    commandList->clearTextureFloat(gBufferResources.emissiveTexture, nvrhi::AllSubresources, nvrhi::Color(0.0f));
    commandList->clearTextureFloat(gBufferResources.albedoTexture, nvrhi::AllSubresources, nvrhi::Color(0.0f));
    commandList->clearTextureFloat(gBufferResources.specularAlbedoTexture, nvrhi::AllSubresources, nvrhi::Color(0.0f));
    commandList->clearTextureFloat(gBufferResources.viewZTexture, nvrhi::AllSubresources, nvrhi::Color(0.0f));
    commandList->clearTextureFloat(gBufferResources.motionVectorTexture, nvrhi::AllSubresources, nvrhi::Color(0.0f));
    commandList->clearTextureFloat(gBufferResources.shadingNormalRoughnessTexture, nvrhi::AllSubresources, nvrhi::Color(0.0f));
    commandList->clearTextureFloat(m_denoiserResources.noisyDiffuseRadianceHitT, nvrhi::AllSubresources, nvrhi::Color(0.0f));
    commandList->clearTextureFloat(m_denoiserResources.noisySpecularRadianceHitT, nvrhi::AllSubresources, nvrhi::Color(0.0f));
}

nvrhi::TextureHandle ResourceManager::createRenderTargetTexture(
    const uint32_t width,const uint32_t height, const std::string& name, const nvrhi::Format format)
{
    nvrhi::TextureDesc desc;
    desc.width = width;
    desc.height = height;
    desc.isUAV = true;
    desc.keepInitialState = true;
    desc.format = format;
    desc.initialState = nvrhi::ResourceStates::UnorderedAccess;
    desc.debugName = name;
    desc.isRenderTarget = true;

    return m_device->createTexture(desc);
}

nvrhi::BufferHandle ResourceManager::createBuffer(
    const uint32_t byteSize, const uint32_t strideSize, const std::string& name, const bool isUav, const bool isRawBuffer)
{
    nvrhi::BufferDesc bufferDesc = {};
    bufferDesc.byteSize = byteSize;
    bufferDesc.structStride = strideSize;
    bufferDesc.debugName = name;
    bufferDesc.canHaveTypedViews = true;
    bufferDesc.canHaveUAVs = isUav;
    bufferDesc.initialState = isUav ? nvrhi::ResourceStates::UnorderedAccess :
        nvrhi::ResourceStates::ShaderResource | nvrhi::ResourceStates::CopyDest;
    bufferDesc.canHaveRawViews = isRawBuffer;
    bufferDesc.keepInitialState = isUav ? true : false;
    return m_device->createBuffer(bufferDesc);
}
