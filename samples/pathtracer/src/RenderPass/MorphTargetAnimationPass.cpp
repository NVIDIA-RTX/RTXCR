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

#include "../SampleScene.h"
#include "../ScopeMarker.h"
#include "MorphTargetAnimationPass.h"
#include "../shared/shared.h"

std::vector<donut::engine::ShaderMacro> polytubeShaderMacro =
{
    { "RTXCR_CURVE_TESSELLATION_TYPE", std::to_string(RTXCR_CURVE_TESSELLATION_TYPE_POLYTUBE) }
};

std::vector<donut::engine::ShaderMacro> dotsShaderMacro =
{
    { "RTXCR_CURVE_TESSELLATION_TYPE", std::to_string(RTXCR_CURVE_TESSELLATION_TYPE_DOTS) }
};

std::vector<donut::engine::ShaderMacro> lssShaderMacro =
{
    { "RTXCR_CURVE_TESSELLATION_TYPE", std::to_string(RTXCR_CURVE_TESSELLATION_TYPE_LSS) }
};

MorphTargetAnimationPass::MorphTargetAnimationPass(
    nvrhi::IDevice* const device,
    std::shared_ptr<donut::engine::ShaderFactory> shaderFactory)
: m_device(device)
, m_shaderFactory(shaderFactory)
, m_totalTime(0.0f)
, m_prevAnimationTimestampPerFrame(0.0f)
{
}

void MorphTargetAnimationPass::createShaders()
{
    m_shaders[(uint32_t)TessellationType::Polytube] =
        m_shaderFactory->CreateShader("app/morphTargetAnimation.cs.hlsl", "main_cs", &polytubeShaderMacro, nvrhi::ShaderType::Compute);

    m_shaders[(uint32_t)TessellationType::DisjointOrthogonalTriangleStrip] =
        m_shaderFactory->CreateShader("app/morphTargetAnimation.cs.hlsl", "main_cs", &dotsShaderMacro, nvrhi::ShaderType::Compute);

    m_shaders[(uint32_t)TessellationType::LinearSweptSphere] =
        m_shaderFactory->CreateShader("app/morphTargetAnimation.cs.hlsl", "main_cs", &lssShaderMacro, nvrhi::ShaderType::Compute);

    // TODO: Debug triangles
}

bool MorphTargetAnimationPass::CreateMorphTargetAnimationPipeline(const TessellationType tessellationType)
{
    nvrhi::BindingLayoutDesc bindingLayoutDesc;
    bindingLayoutDesc.visibility = nvrhi::ShaderType::Compute;
    bindingLayoutDesc.bindings = {
        nvrhi::BindingLayoutItem::ConstantBuffer(0),
        nvrhi::BindingLayoutItem::StructuredBuffer_SRV(0),
        nvrhi::BindingLayoutItem::StructuredBuffer_SRV(1),
        nvrhi::BindingLayoutItem::StructuredBuffer_SRV(2),
        nvrhi::BindingLayoutItem::RawBuffer_UAV(0),
        nvrhi::BindingLayoutItem::RawBuffer_UAV(1),
        nvrhi::BindingLayoutItem::RawBuffer_UAV(2),
    };

    // Index buffer data are cleared in CurveTessellation when using LSS geometry mode, as index buffers are not currently supported for LSS.
    if (tessellationType != TessellationType::LinearSweptSphere)
    {
        bindingLayoutDesc.bindings.push_back(nvrhi::BindingLayoutItem::RawBuffer_SRV(3));
    }

    m_bindingLayout = m_device->createBindingLayout(bindingLayoutDesc);

    createShaders();

    return true;
}

void MorphTargetAnimationPass::RecompileMorphTargetAnimationShaders()
{
    createShaders();

    m_pso = nullptr;
}

void MorphTargetAnimationPass::Update(const float fElapsedTimeSeconds)
{
    m_totalTime += fElapsedTimeSeconds;
}

void MorphTargetAnimationPass::Dispatch(
    const std::shared_ptr<donut::engine::MeshInfo> mesh,
    nvrhi::CommandListHandle commandList,
    const ResourceManager::MorphTargetResources& morphTargetResources,
    const TessellationType tessellationType,
    const float animationTimestampPerFrame,
    const bool enableDebugOverride,
    const uint32_t overrideKeyFrameIndex,
    const float overrideKeyFrameWeight,
    const float animationSmoothingFactor)
{
    if (morphTargetResources.vertexSize == 0)
    {
        // Not a morph target animation resource
        return;
    }

    ScopedMarker scopedMarker(commandList, "Morph Target Animation");

    if (!m_pso)
    {
        nvrhi::ComputePipelineDesc pipelineDesc;
        pipelineDesc.CS = m_shaders[(uint32_t)tessellationType];
        pipelineDesc.addBindingLayout(m_bindingLayout);
        m_pso = m_device->createComputePipeline(pipelineDesc);
    }

    const auto& positionBufferRange = mesh->buffers->getVertexBufferRange(VertexAttribute::Position);
    const auto& normalBufferRange = mesh->buffers->getVertexBufferRange(VertexAttribute::Normal);
    const auto& tangentBufferRange = mesh->buffers->getVertexBufferRange(VertexAttribute::Tangent);

    // Calculate morph target buffers that are needed for interpolation
    const uint32_t morphTargetSize = mesh->buffers->morphTargetBufferRange.size();
    const float totalAnimationTime = (morphTargetSize - 1) * animationTimestampPerFrame;
    const float adjustedTotalAnimationTime = totalAnimationTime + animationSmoothingFactor * animationTimestampPerFrame;
    m_totalTime *= m_prevAnimationTimestampPerFrame != 0.0f ?
        (animationTimestampPerFrame / m_prevAnimationTimestampPerFrame) : 1.0f;
    while (m_totalTime > adjustedTotalAnimationTime)
    {
        m_totalTime -= adjustedTotalAnimationTime;
    }

    uint32_t keyFrameIndex = 0;
    if (!enableDebugOverride)
    {
        if (m_totalTime < totalAnimationTime)
        {
            keyFrameIndex = static_cast<uint32_t>(m_totalTime / animationTimestampPerFrame);
        }
        else
        {
            keyFrameIndex = morphTargetSize - 1;
        }
    }
    else
    {
        keyFrameIndex = overrideKeyFrameIndex % morphTargetSize;
    }

    // All morph target buffer data are packed into a single buffer 'morphTargetDataBuffer', so we don't need to upload data every frame.
    // Instead, we calculate the 2 keyframes we need, and use buffer range to bind to the animation shader.
    const auto& morphTargetBufferKeyframeRange = mesh->buffers->morphTargetBufferRange[keyFrameIndex];
    const auto& morphTargetBufferNextKeyframeRange = mesh->buffers->morphTargetBufferRange[(keyFrameIndex + 1) % morphTargetSize];

    // Slow down last frame to avoid flickering
    float adjustedAnimationTimestampPerFrame = animationTimestampPerFrame;
    if ((keyFrameIndex + 1) % morphTargetSize == 0)
    {
        adjustedAnimationTimestampPerFrame *= animationSmoothingFactor;
    }
    else
    {
        adjustedAnimationTimestampPerFrame *= 1;
    }

    // Update CB
    MorphTargetConstants morphTargetConstants = {};
    morphTargetConstants.vertexCount = morphTargetResources.vertexSize;
    morphTargetConstants.lerpWeight = !enableDebugOverride ?
        saturate((m_totalTime - keyFrameIndex * animationTimestampPerFrame) / adjustedAnimationTimestampPerFrame) :
        saturate(overrideKeyFrameWeight);

    if (morphTargetConstants.lerpWeight < 0.0f || morphTargetConstants.lerpWeight > 1.0f)
    {
        donut::log::warning("Morph Target interpolation weight must be in the range between 0 and 1.");
    }

    commandList->beginTrackingBufferState(morphTargetResources.morphTargetConstantBuffer, nvrhi::ResourceStates::Common);
    commandList->writeBuffer(morphTargetResources.morphTargetConstantBuffer, &morphTargetConstants, sizeof(morphTargetConstants));

    commandList->beginTrackingBufferState(morphTargetResources.morphTargetDataBuffer, nvrhi::ResourceStates::Common);
    commandList->setBufferState(morphTargetResources.morphTargetDataBuffer, nvrhi::ResourceStates::ShaderResource);
    commandList->commitBarriers();

    commandList->beginTrackingBufferState(mesh->buffers->vertexBuffer, nvrhi::ResourceStates::UnorderedAccess);

    nvrhi::BindingSetDesc bindingSetDesc;
    bindingSetDesc.bindings = {
        nvrhi::BindingSetItem::ConstantBuffer(0, morphTargetResources.morphTargetConstantBuffer),
        nvrhi::BindingSetItem::StructuredBuffer_SRV(0, morphTargetResources.morphTargetDataBuffer, nvrhi::Format::UNKNOWN, morphTargetBufferKeyframeRange),
        nvrhi::BindingSetItem::StructuredBuffer_SRV(1, morphTargetResources.morphTargetDataBuffer, nvrhi::Format::UNKNOWN, morphTargetBufferNextKeyframeRange),
        nvrhi::BindingSetItem::StructuredBuffer_SRV(2, morphTargetResources.lineSegmentsBuffer),
        nvrhi::BindingSetItem::RawBuffer_UAV(0, mesh->buffers->vertexBuffer, positionBufferRange),
        nvrhi::BindingSetItem::RawBuffer_UAV(1, mesh->buffers->vertexBuffer, normalBufferRange),
        nvrhi::BindingSetItem::RawBuffer_UAV(2, mesh->buffers->vertexBuffer, tangentBufferRange),
    };
    // Index buffer data are cleared in CurveTessellation when using LSS geometry mode, as index buffers are not currently supported for LSS.
    if (tessellationType != TessellationType::LinearSweptSphere)
    {
        bindingSetDesc.bindings.push_back(nvrhi::BindingSetItem::RawBuffer_SRV(3, mesh->buffers->indexBuffer));
    }

    m_bindingSet = m_device->createBindingSet(bindingSetDesc, m_bindingLayout);

    nvrhi::ComputeState state;
    state.setPipeline(m_pso);
    state.addBindingSet(m_bindingSet);

    commandList->setComputeState(state);
    commandList->dispatch((morphTargetResources.vertexSize - 1) / 32 + 1);

    m_prevAnimationTimestampPerFrame = animationTimestampPerFrame;
    while (m_totalTime > adjustedTotalAnimationTime)
    {
        m_totalTime -= adjustedTotalAnimationTime;
    }
}
