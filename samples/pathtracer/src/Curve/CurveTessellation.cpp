/*
 * Copyright (c) 2024-2026, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#include <donut/core/math/math.h>

#include "shared.h"
#include "CurveTessellation.h"
#include "../Ui/PathtracerUi.h"

#include <nvrhi/common/misc.h>

CurveTessellation::CurveTessellation(const std::vector<std::shared_ptr<MeshInstance>>& meshInstances, const UIData& ui)
: m_curveOriginalGeometryInfoCache(meshInstances.size())
, m_ui(ui)
{
    for (uint32_t meshIndex = 0; meshIndex < meshInstances.size(); ++meshIndex)
    {
        const auto& mesh = meshInstances[meshIndex]->GetMesh();
        for (const auto& geometry : mesh->geometries)
        {
            m_curveOriginalGeometryInfoCache[meshIndex].push_back(*geometry);
        }
    }

    convertCurveLineStripsToLineSegments(meshInstances);
}

void CurveTessellation::convertToTrianglePolyTubes(const std::vector<std::shared_ptr<MeshInstance>>& meshInstances)
{
    for (uint32_t meshIndex = 0; meshIndex < meshInstances.size(); ++meshIndex)
    {
        auto& mesh = meshInstances[meshIndex]->GetMesh();
        auto& meshBuffers = mesh->buffers;

        if (mesh->IsCurve())
        {
            mesh->type = MeshType::CurvePolytubes;

            // RTXCR_CURVE_POLYTUBE_ORDER faces with 2 triangles (3 vertices each)
            const uint32_t numVerticesPerSegment = RTXCR_CURVE_POLYTUBE_ORDER * 2 * 3;
            // Assumption: All curve geometries in same mesh have same primitive type, so we just check the type of first geometry
            const uint32_t totalIndices = m_curvesLineSegments[meshIndex].size() * numVerticesPerSegment;
            const uint32_t totalVertices = m_curvesLineSegments[meshIndex].size() * numVerticesPerSegment;

            meshBuffers->indexData.resize(totalIndices);
            meshBuffers->positionData.resize(totalVertices);
            meshBuffers->normalData.resize(totalVertices);
            meshBuffers->tangentData.resize(totalVertices);
            meshBuffers->texcoord1Data.resize(totalVertices);
            meshBuffers->radiusData.resize(totalVertices);

            const auto& meshGeometryCache = m_curveOriginalGeometryInfoCache[meshIndex];
            const auto& lineSegments = m_curvesLineSegments[meshIndex];
            uint32_t indexOffsetInMesh = 0;
            uint32_t vertexOffsetInMesh = 0;
            uint32_t globalIndex = 0;

            for (uint32_t geometryIndex = 0; geometryIndex < mesh->geometries.size(); ++geometryIndex)
            {
                const auto& geometryCache = meshGeometryCache[geometryIndex];
                auto& geometry = mesh->geometries[geometryIndex];

                const uint32_t indexSize = (geometry->type == MeshGeometryPrimitiveType::Lines) ?
                    geometryCache.numIndices / 2 : geometryCache.numIndices - 1;
                const uint32_t vertexSize = (geometry->type == MeshGeometryPrimitiveType::Lines) ?
                    geometryCache.numVertices / 2 : geometryCache.numVertices - 1;

                const uint32_t geometryNumIndices = indexSize * numVerticesPerSegment;
                const uint32_t geometryNumVertices = vertexSize * numVerticesPerSegment;
                geometry->numIndices = geometryNumIndices;
                geometry->numVertices = geometryNumVertices;
                geometry->indexOffsetInMesh = indexOffsetInMesh;
                geometry->vertexOffsetInMesh = vertexOffsetInMesh;
                geometry->globalGeometryIndex = geometryIndex;

                globalIndex = rtxcr::geometry::convertToTrianglePolyTubes(
                    lineSegments,
                    indexSize,
                    meshBuffers->indexData.data(),
                    (float*)(meshBuffers->positionData.data()),
                    meshBuffers->normalData.data(),
                    meshBuffers->tangentData.data(),
                    (float*)meshBuffers->texcoord1Data.data(),
                    meshBuffers->radiusData.data(),
                    globalIndex);

                indexOffsetInMesh += geometryNumIndices;
                vertexOffsetInMesh += geometryNumVertices;
            }

            copyToMeshBuffersCache(TessellationType::Polytube, meshBuffers, mesh->geometries);
        }
    }
}

void CurveTessellation::convertToDisjointOrthogonalTriangleStrips(const std::vector<std::shared_ptr<MeshInstance>>& meshInstances)
{
    for (uint32_t meshIndex = 0; meshIndex < meshInstances.size(); ++meshIndex)
    {
        auto& mesh = meshInstances[meshIndex]->GetMesh();
        auto& meshBuffers = mesh->buffers;

        if (mesh->IsCurve())
        {
            mesh->type = MeshType::CurveDisjointOrthogonalTriangleStrips;

            // 4 triangles (3 vertices each)
            const uint32_t numVerticesPerSegment = 4 * 3;
            // Assumption: All curve geometries in same mesh have same primitive type, so we just check the type of first geometry
            const uint32_t totalIndices = m_curvesLineSegments[meshIndex].size() * numVerticesPerSegment;
            const uint32_t totalVertices = m_curvesLineSegments[meshIndex].size() * numVerticesPerSegment;

            meshBuffers->indexData.resize(totalIndices);
            meshBuffers->positionData.resize(totalVertices);
            meshBuffers->normalData.resize(totalVertices);
            meshBuffers->tangentData.resize(totalVertices);
            meshBuffers->texcoord1Data.resize(totalVertices);
            meshBuffers->radiusData.resize(totalVertices);

            const auto& meshGeometryCache = m_curveOriginalGeometryInfoCache[meshIndex];
            const auto& lineSegments = m_curvesLineSegments[meshIndex];
            uint32_t indexOffsetInMesh = 0;
            uint32_t vertexOffsetInMesh = 0;
            uint32_t globalIndex = 0;

            for (uint32_t geometryIndex = 0; geometryIndex < mesh->geometries.size(); ++geometryIndex)
            {
                const auto& geometryCache = meshGeometryCache[geometryIndex];
                auto& geometry = mesh->geometries[geometryIndex];

                const uint32_t indexSize = (geometry->type == MeshGeometryPrimitiveType::Lines) ?
                    geometryCache.numIndices / 2 : geometryCache.numIndices - 1;
                const uint32_t vertexSize = (geometry->type == MeshGeometryPrimitiveType::Lines) ?
                    geometryCache.numVertices / 2 : geometryCache.numVertices - 1;

                const uint32_t geometryNumIndices = indexSize * numVerticesPerSegment;
                const uint32_t geometryNumVertices = vertexSize * numVerticesPerSegment;
                geometry->numIndices = geometryNumIndices;
                geometry->numVertices = geometryNumVertices;
                geometry->indexOffsetInMesh = indexOffsetInMesh;
                geometry->vertexOffsetInMesh = vertexOffsetInMesh;
                geometry->globalGeometryIndex = geometryIndex;

                globalIndex = rtxcr::geometry::convertToDisjointOrthogonalTriangleStrips(
                    lineSegments,
                    indexSize,
                    meshBuffers->indexData.data(),
                    (float*)(meshBuffers->positionData.data()),
                    meshBuffers->normalData.data(),
                    meshBuffers->tangentData.data(),
                    (float*)meshBuffers->texcoord1Data.data(),
                    meshBuffers->radiusData.data(),
                    globalIndex);

                indexOffsetInMesh += geometryNumIndices;
                vertexOffsetInMesh += geometryNumVertices;
            }

            copyToMeshBuffersCache(TessellationType::DisjointOrthogonalTriangleStrip, meshBuffers, mesh->geometries);
        }
    }
}

void CurveTessellation::convertToLinearSweptSpheres(const std::vector<std::shared_ptr<MeshInstance>>& meshInstances)
{
    for (uint32_t meshIndex = 0; meshIndex < meshInstances.size(); ++meshIndex)
    {
        // TODO: Add support for implicit successive indexing format
        // LSS list format
        auto& mesh = meshInstances[meshIndex]->GetMesh();
        auto& meshBuffers = mesh->buffers;

        if (mesh->IsCurve())
        {
            mesh->type = MeshType::CurveLinearSweptSpheres;

            const uint32_t totalVertices = m_curvesLineSegments[meshIndex].size() * 2;

            meshBuffers->indexData.resize(0);
            meshBuffers->positionData.resize(totalVertices);
            meshBuffers->normalData.resize(0);
            meshBuffers->tangentData.resize(0);
            meshBuffers->texcoord1Data.resize(0);
            meshBuffers->radiusData.resize(totalVertices);

            const auto& meshGeometryCache = m_curveOriginalGeometryInfoCache[meshIndex];
            const auto& lineSegments = m_curvesLineSegments[meshIndex];

            uint32_t vertexOffsetInMesh = 0;
            uint32_t globalIndex = 0;

            for (uint32_t geometryIndex = 0; geometryIndex < mesh->geometries.size(); ++geometryIndex)
            {
                const auto& geometryCache = meshGeometryCache[geometryIndex];
                auto& geometry = mesh->geometries[geometryIndex];

                const uint32_t numLineSegments = (geometry->type == MeshGeometryPrimitiveType::Lines) ?
                    geometryCache.numIndices / 2 : geometryCache.numIndices - 1;
                const uint32_t geometryNumVertices = numLineSegments * 2;

                geometry->numIndices = 0;
                geometry->numVertices = geometryNumVertices;
                geometry->indexOffsetInMesh = 0;
                geometry->vertexOffsetInMesh = vertexOffsetInMesh;
                geometry->globalGeometryIndex = geometryIndex;

                globalIndex = rtxcr::geometry::convertToLinearSweptSpheres(
                    lineSegments,
                    numLineSegments,
                    (float*)meshBuffers->positionData.data(),
                    (float*)meshBuffers->radiusData.data(),
                    globalIndex);

                vertexOffsetInMesh += geometryNumVertices;
            }

            copyToMeshBuffersCache(TessellationType::LinearSweptSphere, meshBuffers, mesh->geometries);
        }
    }
}

void CurveTessellation::replacingSceneMesh(nvrhi::IDevice* device, donut::engine::DescriptorTableManager* descriptorTable, const TessellationType tessellationType, const std::vector<std::shared_ptr<MeshInstance>>& meshInstances)
{
    const auto& currentCurveMeshBuffers = m_curveMeshBuffersCache[(uint32_t)tessellationType];
    uint32_t curveIndex = 0;
    for (uint32_t meshIndex = 0; meshIndex < meshInstances.size(); ++meshIndex)
    {
        auto& mesh = meshInstances[meshIndex]->GetMesh();
        auto& meshBuffers = mesh->buffers;

        if (mesh->IsCurve())
        {
            for (uint32_t geometryIndex = 0; geometryIndex < mesh->geometries.size(); ++geometryIndex)
            {
                *mesh->geometries[geometryIndex] = currentCurveMeshBuffers[curveIndex].geometries[geometryIndex];
            }

            if (tessellationType == TessellationType::Polytube)
            {
                mesh->type = MeshType::CurvePolytubes;
            }
            else if (tessellationType == TessellationType::DisjointOrthogonalTriangleStrip)
            {
                mesh->type = MeshType::CurveDisjointOrthogonalTriangleStrips;
            }
            else if (tessellationType == TessellationType::LinearSweptSphere)
            {
                mesh->type = MeshType::CurveLinearSweptSpheres;
            }

            const auto& srcBuffers = currentCurveMeshBuffers[curveIndex].buffers;
            meshBuffers->vertexBufferRanges = srcBuffers->vertexBufferRanges;
            meshBuffers->indexData = srcBuffers->indexData;
            meshBuffers->positionData = srcBuffers->positionData;
            meshBuffers->normalData = srcBuffers->normalData;
            meshBuffers->tangentData = srcBuffers->tangentData;
            meshBuffers->texcoord1Data = srcBuffers->texcoord1Data;
            meshBuffers->radiusData = srcBuffers->radiusData;

            meshBuffers->indexBuffer = nullptr;
            meshBuffers->vertexBuffer = nullptr;
            meshBuffers->instanceBuffer = nullptr;

            if (!meshBuffers->morphTargetData.empty())
            {
                createDynamicVertexBuffer(device, descriptorTable, meshBuffers.get(), mesh->name);
            }

            ++curveIndex;
        }
    }
}

void CurveTessellation::swapDynamicVertexBuffer()
{
    for (auto& bufferGroupPrevVertexBufferPair : bufferGroupPrevVertexBufferMap)
    {
        auto meshBuffers = bufferGroupPrevVertexBufferPair.first;
        const auto curVertexBuffer = meshBuffers->vertexBuffer;
        const auto curVertexBufferDescriptor = meshBuffers->vertexBufferDescriptor;
        meshBuffers->vertexBuffer = bufferGroupPrevVertexBufferPair.second.vertexBuffer;
        meshBuffers->vertexBufferDescriptor = bufferGroupPrevVertexBufferPair.second.descriptor;
        bufferGroupPrevVertexBufferPair.second.vertexBuffer = curVertexBuffer;
        bufferGroupPrevVertexBufferPair.second.descriptor = curVertexBufferDescriptor;
    }
}

void CurveTessellation::convertCurveLineStripsToLineSegments(const std::vector<std::shared_ptr<MeshInstance>>& meshInstances)
{
    m_curvesLineSegments.resize(meshInstances.size());

    for (uint32_t meshIndex = 0; meshIndex < meshInstances.size(); ++meshIndex)
    {
        const auto& mesh = meshInstances[meshIndex]->GetMesh();

        if (mesh->IsCurve())
        {
            const auto& indices = mesh->buffers->indexData;
            const auto& positions = mesh->buffers->positionData;
            const auto& radius = mesh->buffers->radiusData;
            const auto& texCoord1 = mesh->buffers->texcoord1Data;
            const auto& geometries = mesh->geometries;

            const auto& meshGeometryCache = m_curveOriginalGeometryInfoCache[meshIndex];
            uint32_t virtualGeometryIndex = 0;
            for (uint32_t geometryIndex = 0; geometryIndex < mesh->geometries.size(); ++geometryIndex)
            {
                const auto& geometryCache = meshGeometryCache[geometryIndex];
                auto& geometry = mesh->geometries[geometryIndex];

                const uint32_t indexStep = (geometry->type == MeshGeometryPrimitiveType::Lines) ? 2 : 1;
                for (uint32_t index = 0; index < geometryCache.numIndices - 1; index += indexStep)
                {
                    const uint32_t lineIndexStart = indices[index + geometryCache.indexOffsetInMesh] + geometryCache.indexOffsetInMesh;
                    const uint32_t lineIndexEnd = indices[index + 1 + geometryCache.indexOffsetInMesh] + geometryCache.indexOffsetInMesh;

                    const auto& posStart = positions[lineIndexStart];
                    const auto& posEnd = positions[lineIndexEnd];
                    const float radiusStart = radius[lineIndexStart];
                    const float radiusEnd = radius[lineIndexEnd];

                    rtxcr::geometry::LineSegment segment;
                    segment.vertices[0].position[0] = posStart.x;
                    segment.vertices[0].position[1] = posStart.y;
                    segment.vertices[0].position[2] = posStart.z;
                    segment.vertices[0].radius = radiusStart * m_ui.hairRadiusScale;
                    segment.vertices[1].position[0] = posEnd.x;
                    segment.vertices[1].position[1] = posEnd.y;
                    segment.vertices[1].position[2] = posEnd.z;
                    segment.vertices[1].radius = radiusEnd * m_ui.hairRadiusScale;

                    // UVs
                    if (texCoord1.data())
                    {
                        const auto& uvStart = texCoord1[lineIndexStart];
                        const auto& uvEnd = texCoord1[lineIndexEnd];
                        segment.vertices[0].texCoord[0] = uvStart.x;
                        segment.vertices[0].texCoord[1] = uvStart.y;
                        segment.vertices[1].texCoord[0] = uvEnd.x;
                        segment.vertices[1].texCoord[1] = uvEnd.y;
                    }

                    // Detect line-segment geometry indices dynamically at runtime
                    if (geometry->type == MeshGeometryPrimitiveType::Lines)
                    {
                        if (!m_curvesLineSegments[meshIndex].empty())
                        {
                            const auto& prevSegmentEndVertex = m_curvesLineSegments[meshIndex].back().vertices[1];
                            // If the current segment's start vertex differs from the previous segment's end vertex,
                            // it indicates the start of a new geometry group
                            if (!isnear(posStart.x, prevSegmentEndVertex.position[0]) ||
                                !isnear(posStart.y, prevSegmentEndVertex.position[1]) ||
                                !isnear(posStart.z, prevSegmentEndVertex.position[2]))
                            {
                                ++virtualGeometryIndex;
                            }
                        }

                        segment.geometryIndex = virtualGeometryIndex;
                    }
                    else
                    {
                        segment.geometryIndex = geometryIndex;
                    }

                    m_curvesLineSegments[meshIndex].push_back(segment);
                }
            }
            m_curvesLineSegmentsIndexMap[mesh->name] = meshIndex;
        }
    }
}

void CurveTessellation::copyToMeshBuffersCache(
    const TessellationType tessellationType,
    std::shared_ptr<BufferGroup> meshBuffers,
    const std::vector<std::shared_ptr<MeshGeometry>>& geometries)
{
    CurveMeshBuffersCache meshBuffersCache;
    meshBuffersCache.buffers = std::make_shared<BufferGroup>();
    meshBuffersCache.buffers->vertexBufferRanges = meshBuffers->vertexBufferRanges;
    meshBuffersCache.buffers->indexData = meshBuffers->indexData;
    meshBuffersCache.buffers->positionData = meshBuffers->positionData;
    meshBuffersCache.buffers->normalData = meshBuffers->normalData;
    meshBuffersCache.buffers->tangentData = meshBuffers->tangentData;
    meshBuffersCache.buffers->texcoord1Data = meshBuffers->texcoord1Data;
    meshBuffersCache.buffers->radiusData = meshBuffers->radiusData;

    meshBuffersCache.geometries.reserve(geometries.size());
    for (const auto geometry : geometries)
    {
        meshBuffersCache.geometries.push_back(*geometry);
    }

    m_curveMeshBuffersCache[(uint32_t)tessellationType].push_back(meshBuffersCache);
}

void CurveTessellation::createDynamicVertexBuffer(
    nvrhi::IDevice* device,
    donut::engine::DescriptorTableManager* descriptorTable,
    BufferGroup* meshBuffers,
    std::string& meshName)
{
    auto AppendBufferRange = [](nvrhi::BufferRange& range, size_t size, uint64_t& currentBufferSize) {
        range.byteOffset = currentBufferSize;
        range.byteSize = nvrhi::align(size, size_t(16));
        currentBufferSize += range.byteSize;
    };

    nvrhi::BufferDesc bufferDesc;
    bufferDesc.isVertexBuffer = true;
    bufferDesc.byteSize = 0;
    bufferDesc.debugName = "Dynamic VertexBuffer - " + meshName + " 0";
    bufferDesc.canHaveTypedViews = true;
    bufferDesc.canHaveRawViews = true;
    bufferDesc.isAccelStructBuildInput = true;
    bufferDesc.canHaveUAVs = true;

    if (!meshBuffers->positionData.empty())
    {
        AppendBufferRange(meshBuffers->getVertexBufferRange(VertexAttribute::Position),
            meshBuffers->positionData.size() * sizeof(meshBuffers->positionData[0]), bufferDesc.byteSize);
    }

    if (!meshBuffers->normalData.empty())
    {
        AppendBufferRange(meshBuffers->getVertexBufferRange(VertexAttribute::Normal),
            meshBuffers->normalData.size() * sizeof(meshBuffers->normalData[0]), bufferDesc.byteSize);
    }

    if (!meshBuffers->tangentData.empty())
    {
        AppendBufferRange(meshBuffers->getVertexBufferRange(VertexAttribute::Tangent),
            meshBuffers->tangentData.size() * sizeof(meshBuffers->tangentData[0]), bufferDesc.byteSize);
    }

    if (!meshBuffers->texcoord1Data.empty())
    {
        AppendBufferRange(meshBuffers->getVertexBufferRange(VertexAttribute::TexCoord1),
            meshBuffers->texcoord1Data.size() * sizeof(meshBuffers->texcoord1Data[0]), bufferDesc.byteSize);
    }

    if (!meshBuffers->texcoord2Data.empty())
    {
        AppendBufferRange(meshBuffers->getVertexBufferRange(VertexAttribute::TexCoord2),
            meshBuffers->texcoord2Data.size() * sizeof(meshBuffers->texcoord2Data[0]), bufferDesc.byteSize);
    }

    if (!meshBuffers->weightData.empty())
    {
        AppendBufferRange(meshBuffers->getVertexBufferRange(VertexAttribute::JointWeights),
            meshBuffers->weightData.size() * sizeof(meshBuffers->weightData[0]), bufferDesc.byteSize);
    }

    if (!meshBuffers->jointData.empty())
    {
        AppendBufferRange(meshBuffers->getVertexBufferRange(VertexAttribute::JointIndices),
            meshBuffers->jointData.size() * sizeof(meshBuffers->jointData[0]), bufferDesc.byteSize);
    }

    if (!meshBuffers->radiusData.empty())
    {
        AppendBufferRange(meshBuffers->getVertexBufferRange(VertexAttribute::CurveRadius),
            meshBuffers->radiusData.size() * sizeof(meshBuffers->radiusData[0]), bufferDesc.byteSize);
    }

    meshBuffers->vertexBuffer = device->createBuffer(bufferDesc);

    // Create prevVertexBuffer
    bufferDesc.debugName = "Dynamic VertexBuffer - " + meshName + " 1";
    nvrhi::BufferHandle prevVertexBuffer = device->createBuffer(bufferDesc);
    std::shared_ptr<DescriptorHandle> prevVertexBufferDescriptor;

    if (descriptorTable)
    {
        meshBuffers->vertexBufferDescriptor = std::make_shared<DescriptorHandle>(
            descriptorTable->CreateDescriptorHandle(nvrhi::BindingSetItem::RawBuffer_SRV(0, meshBuffers->vertexBuffer)));

        prevVertexBufferDescriptor = std::make_shared<DescriptorHandle>(
            descriptorTable->CreateDescriptorHandle(nvrhi::BindingSetItem::RawBuffer_SRV(0, prevVertexBuffer)));
    }
    bufferGroupPrevVertexBufferMap[meshBuffers] = { prevVertexBuffer, prevVertexBufferDescriptor };

    nvrhi::ResourceStates state = nvrhi::ResourceStates::VertexBuffer | nvrhi::ResourceStates::ShaderResource | nvrhi::ResourceStates::AccelStructBuildInput;

    auto commandList = device->createCommandList();
    commandList->open();

    commandList->beginTrackingBufferState(meshBuffers->vertexBuffer, nvrhi::ResourceStates::Common);
    commandList->beginTrackingBufferState(prevVertexBuffer, nvrhi::ResourceStates::Common);

    if (!meshBuffers->positionData.empty())
    {
        const auto& range = meshBuffers->getVertexBufferRange(VertexAttribute::Position);
        commandList->writeBuffer(meshBuffers->vertexBuffer, meshBuffers->positionData.data(), range.byteSize, range.byteOffset);
        commandList->writeBuffer(prevVertexBuffer, meshBuffers->positionData.data(), range.byteSize, range.byteOffset);
        std::vector<float3>().swap(meshBuffers->positionData);
    }

    if (!meshBuffers->normalData.empty())
    {
        const auto& range = meshBuffers->getVertexBufferRange(VertexAttribute::Normal);
        commandList->writeBuffer(meshBuffers->vertexBuffer, meshBuffers->normalData.data(), range.byteSize, range.byteOffset);
        commandList->writeBuffer(prevVertexBuffer, meshBuffers->normalData.data(), range.byteSize, range.byteOffset);
        std::vector<uint32_t>().swap(meshBuffers->normalData);
    }

    if (!meshBuffers->tangentData.empty())
    {
        const auto& range = meshBuffers->getVertexBufferRange(VertexAttribute::Tangent);
        commandList->writeBuffer(meshBuffers->vertexBuffer, meshBuffers->tangentData.data(), range.byteSize, range.byteOffset);
        commandList->writeBuffer(prevVertexBuffer, meshBuffers->tangentData.data(), range.byteSize, range.byteOffset);
        std::vector<uint32_t>().swap(meshBuffers->tangentData);
    }

    if (!meshBuffers->texcoord1Data.empty())
    {
        const auto& range = meshBuffers->getVertexBufferRange(VertexAttribute::TexCoord1);
        commandList->writeBuffer(meshBuffers->vertexBuffer, meshBuffers->texcoord1Data.data(), range.byteSize, range.byteOffset);
        commandList->writeBuffer(prevVertexBuffer, meshBuffers->texcoord1Data.data(), range.byteSize, range.byteOffset);
        std::vector<float2>().swap(meshBuffers->texcoord1Data);
    }

    if (!meshBuffers->texcoord2Data.empty())
    {
        const auto& range = meshBuffers->getVertexBufferRange(VertexAttribute::TexCoord2);
        commandList->writeBuffer(meshBuffers->vertexBuffer, meshBuffers->texcoord2Data.data(), range.byteSize, range.byteOffset);
        commandList->writeBuffer(prevVertexBuffer, meshBuffers->texcoord2Data.data(), range.byteSize, range.byteOffset);
        std::vector<float2>().swap(meshBuffers->texcoord2Data);
    }

    if (!meshBuffers->weightData.empty())
    {
        const auto& range = meshBuffers->getVertexBufferRange(VertexAttribute::JointWeights);
        commandList->writeBuffer(meshBuffers->vertexBuffer, meshBuffers->weightData.data(), range.byteSize, range.byteOffset);
        commandList->writeBuffer(prevVertexBuffer, meshBuffers->weightData.data(), range.byteSize, range.byteOffset);
        std::vector<float4>().swap(meshBuffers->weightData);
    }

    if (!meshBuffers->jointData.empty())
    {
        const auto& range = meshBuffers->getVertexBufferRange(VertexAttribute::JointIndices);
        commandList->writeBuffer(meshBuffers->vertexBuffer, meshBuffers->jointData.data(), range.byteSize, range.byteOffset);
        commandList->writeBuffer(prevVertexBuffer, meshBuffers->jointData.data(), range.byteSize, range.byteOffset);
        std::vector<vector<uint16_t, 4>>().swap(meshBuffers->jointData);
    }

    if (!meshBuffers->radiusData.empty())
    {
        const auto& range = meshBuffers->getVertexBufferRange(VertexAttribute::CurveRadius);
        commandList->writeBuffer(meshBuffers->vertexBuffer, meshBuffers->radiusData.data(), range.byteSize, range.byteOffset);
        commandList->writeBuffer(prevVertexBuffer, meshBuffers->radiusData.data(), range.byteSize, range.byteOffset);
        std::vector<float>().swap(meshBuffers->radiusData);
    }

    commandList->setBufferState(meshBuffers->vertexBuffer, state);
    commandList->setBufferState(prevVertexBuffer, state);
    commandList->commitBarriers();

    commandList->close();
    device->executeCommandList(commandList);
}
