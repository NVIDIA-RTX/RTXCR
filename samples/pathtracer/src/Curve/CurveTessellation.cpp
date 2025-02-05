/*
 * Copyright (c) 2024-2025, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#include "CurveTessellation.h"
#include "shared.h"
#include "../Ui/PathtracerUi.h"

namespace
{
    // Generate a vector that is orthogonal to the input vector
    // This can be used to invent a tangent frame for meshes that don't have real tangents/bitangents.
    inline float3 perpStark(const float3& u)
    {
        float3 a = abs(u);
        uint32_t uyx = (a.x - a.y) < 0 ? 1 : 0;
        uint32_t uzx = (a.x - a.z) < 0 ? 1 : 0;
        uint32_t uzy = (a.y - a.z) < 0 ? 1 : 0;
        uint32_t xm = uyx & uzx;
        uint32_t ym = (1 ^ xm) & uzy;
        uint32_t zm = 1 ^ (xm | ym); // 1 ^ (xm & ym)
        float3 v = normalize(cross(u, float3(xm, ym, zm)));
        return v;
    }

    // Build a local frame from a unit normal vector.
    inline void buildFrame(const float3& n, float3& t, float3& b)
    {
        t = perpStark(n);
        b = cross(n, t);
    }

    inline float3 getUnitCircleCoords(float3 xAxis, float3 yAxis, float angleRadians)
    {
        // We only care about angles < 2PI
        float unused = 0.f;
        float unitCircleFraction = std::modf(angleRadians / TWO_PI, &unused);

        if (unitCircleFraction < 0.f)
        {
            unitCircleFraction = 1.0f - unitCircleFraction;
        }

        float adjustedAngleRadians = unitCircleFraction * TWO_PI;

        return std::cos(adjustedAngleRadians) * xAxis + std::sin(adjustedAngleRadians) * yAxis;
    }
} // namespace

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
            const uint32_t totalIndices = (mesh->geometries[0]->type == MeshGeometryPrimitiveType::Lines) ?
                m_curvesLineSegments[meshIndex].size() * 2 * numVerticesPerSegment :
                m_curvesLineSegments[meshIndex].size() * numVerticesPerSegment;
            const uint32_t totalVertices = (mesh->geometries[0]->type == MeshGeometryPrimitiveType::Lines) ?
                m_curvesLineSegments[meshIndex].size() * 2 * numVerticesPerSegment :
                m_curvesLineSegments[meshIndex].size() * numVerticesPerSegment;

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

                const uint32_t geometryNumIndices = (geometryCache.numIndices - 1) * numVerticesPerSegment;
                const uint32_t geometryNumVertices = (geometryCache.numVertices - 1) * numVerticesPerSegment;
                geometry->numIndices = geometryNumIndices;
                geometry->numVertices = geometryNumVertices;
                geometry->indexOffsetInMesh = indexOffsetInMesh;
                geometry->vertexOffsetInMesh = vertexOffsetInMesh;
                geometry->globalGeometryIndex = geometryIndex;

                const uint32_t indexSize = (geometry->type == MeshGeometryPrimitiveType::Lines) ?
                    geometryCache.numIndices / 2 : geometryCache.numIndices - 1;
                for (uint32_t index = 0; index < indexSize; ++index)
                {
                    const auto& line = lineSegments[globalIndex];

                    // Build the initial frame
                    float3 fwd, s, t;
                    fwd = normalize(float3(line.vertices[1].position) - float3(line.vertices[0].position));
                    buildFrame(fwd, s, t);

                    for (uint32_t face = 0; face < RTXCR_CURVE_POLYTUBE_ORDER; ++face)
                    {
                        const uint32_t baseIndex = globalIndex * numVerticesPerSegment + face * 6;
                        const uint32_t baseGeometryIndex = index * numVerticesPerSegment + face * 6;

                        meshBuffers->indexData[baseIndex] = baseGeometryIndex;
                        meshBuffers->indexData[baseIndex + 1] = baseGeometryIndex + 1;
                        meshBuffers->indexData[baseIndex + 2] = baseGeometryIndex + 2;
                        meshBuffers->indexData[baseIndex + 3] = baseGeometryIndex + 3;
                        meshBuffers->indexData[baseIndex + 4] = baseGeometryIndex + 4;
                        meshBuffers->indexData[baseIndex + 5] = baseGeometryIndex + 5;

                        float angleRadians1 = TWO_PI * face / RTXCR_CURVE_POLYTUBE_ORDER;
                        float angleRadians2 = TWO_PI * (face + 1.0f) / RTXCR_CURVE_POLYTUBE_ORDER;

                        float3 v1 = getUnitCircleCoords(s, t, angleRadians1);
                        float3 v2 = getUnitCircleCoords(s, t, angleRadians2);

                        // Necessary to make up for lost volume of PolyTube approximation of a circular tube
                        const float polyTubesVolumeCompensationScale = 1.0f / (std::sin(PI / RTXCR_CURVE_POLYTUBE_ORDER) / (PI / RTXCR_CURVE_POLYTUBE_ORDER));
                        meshBuffers->positionData[baseIndex] = float3(line.vertices[0].position) + v1 * line.vertices[0].radius * polyTubesVolumeCompensationScale;
                        meshBuffers->positionData[baseIndex + 1] = float3(line.vertices[1].position) + v2 * line.vertices[1].radius * polyTubesVolumeCompensationScale;
                        meshBuffers->positionData[baseIndex + 2] = float3(line.vertices[1].position) + v1 * line.vertices[1].radius * polyTubesVolumeCompensationScale;
                        meshBuffers->positionData[baseIndex + 3] = float3(line.vertices[0].position) + v1 * line.vertices[0].radius * polyTubesVolumeCompensationScale;
                        meshBuffers->positionData[baseIndex + 4] = float3(line.vertices[0].position) + v2 * line.vertices[0].radius * polyTubesVolumeCompensationScale;
                        meshBuffers->positionData[baseIndex + 5] = float3(line.vertices[1].position) + v2 * line.vertices[1].radius * polyTubesVolumeCompensationScale;

                        uint n1 = vectorToSnorm8(v1);
                        uint n2 = vectorToSnorm8(v2);
                        meshBuffers->normalData[baseIndex] = n1;
                        meshBuffers->normalData[baseIndex + 1] = n2;
                        meshBuffers->normalData[baseIndex + 2] = n1;
                        meshBuffers->normalData[baseIndex + 3] = n1;
                        meshBuffers->normalData[baseIndex + 4] = n2;
                        meshBuffers->normalData[baseIndex + 5] = n2;

                        uint tangent = vectorToSnorm8(fwd);
                        meshBuffers->tangentData[baseIndex] = tangent;
                        meshBuffers->tangentData[baseIndex + 1] = tangent;
                        meshBuffers->tangentData[baseIndex + 2] = tangent;
                        meshBuffers->tangentData[baseIndex + 3] = tangent;
                        meshBuffers->tangentData[baseIndex + 4] = tangent;
                        meshBuffers->tangentData[baseIndex + 5] = tangent;

                        meshBuffers->texcoord1Data[baseIndex] = float2(line.vertices[0].texCoord);
                        meshBuffers->texcoord1Data[baseIndex + 1] = float2(line.vertices[1].texCoord);
                        meshBuffers->texcoord1Data[baseIndex + 2] = float2(line.vertices[1].texCoord);
                        meshBuffers->texcoord1Data[baseIndex + 3] = float2(line.vertices[0].texCoord);
                        meshBuffers->texcoord1Data[baseIndex + 4] = float2(line.vertices[0].texCoord);
                        meshBuffers->texcoord1Data[baseIndex + 5] = float2(line.vertices[1].texCoord);

                        meshBuffers->radiusData[baseIndex] = line.vertices[0].radius * polyTubesVolumeCompensationScale;
                        meshBuffers->radiusData[baseIndex + 1] = line.vertices[1].radius * polyTubesVolumeCompensationScale;
                        meshBuffers->radiusData[baseIndex + 2] = line.vertices[1].radius * polyTubesVolumeCompensationScale;
                        meshBuffers->radiusData[baseIndex + 3] = line.vertices[0].radius * polyTubesVolumeCompensationScale;
                        meshBuffers->radiusData[baseIndex + 4] = line.vertices[0].radius * polyTubesVolumeCompensationScale;
                        meshBuffers->radiusData[baseIndex + 5] = line.vertices[1].radius * polyTubesVolumeCompensationScale;
                    }

                    ++globalIndex;
                }

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
            const uint32_t totalIndices = (mesh->geometries[0]->type == MeshGeometryPrimitiveType::Lines) ?
                m_curvesLineSegments[meshIndex].size() * 2 * numVerticesPerSegment :
                m_curvesLineSegments[meshIndex].size() * numVerticesPerSegment;
            const uint32_t totalVertices = (mesh->geometries[0]->type == MeshGeometryPrimitiveType::Lines) ?
                m_curvesLineSegments[meshIndex].size() * 2 * numVerticesPerSegment :
                m_curvesLineSegments[meshIndex].size() * numVerticesPerSegment;

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

                const uint32_t geometryNumIndices = (geometryCache.numIndices - 1) * numVerticesPerSegment;
                const uint32_t geometryNumVertices = (geometryCache.numVertices - 1) * numVerticesPerSegment;
                geometry->numIndices = geometryNumIndices;
                geometry->numVertices = geometryNumVertices;
                geometry->indexOffsetInMesh = indexOffsetInMesh;
                geometry->vertexOffsetInMesh = vertexOffsetInMesh;
                geometry->globalGeometryIndex = geometryIndex;

                const uint32_t indexSize = (geometry->type == MeshGeometryPrimitiveType::Lines) ?
                    geometryCache.numIndices / 2 : geometryCache.numIndices - 1;
                for (uint32_t index = 0; index < indexSize; ++index)
                {
                    const auto& line = lineSegments[globalIndex];

                    // Build the initial frame
                    float3 fwd, s, t;
                    fwd = normalize(float3(line.vertices[1].position) - float3(line.vertices[0].position));
                    buildFrame(fwd, s, t);

                    float3 v[2] = { s, t };

                    for (uint32_t face = 0; face < 2; ++face)
                    {
                        const uint32_t baseIndex = globalIndex * numVerticesPerSegment + face * 6;
                        const uint32_t baseGeometryIndex = index * numVerticesPerSegment + face * 6;

                        meshBuffers->indexData[baseIndex] = baseGeometryIndex;
                        meshBuffers->indexData[baseIndex + 1] = baseGeometryIndex + 1;
                        meshBuffers->indexData[baseIndex + 2] = baseGeometryIndex + 2;
                        meshBuffers->indexData[baseIndex + 3] = baseGeometryIndex + 3;
                        meshBuffers->indexData[baseIndex + 4] = baseGeometryIndex + 4;
                        meshBuffers->indexData[baseIndex + 5] = baseGeometryIndex + 5;

                        // Necessary to make up for lost volume of PolyTube approximation of a circular tube
                        const float dotsVolumeCompensationScale = 1.0f / (std::sin(PI / 4.0f) / (PI / 4.0f));
                        meshBuffers->positionData[baseIndex] = float3(line.vertices[0].position) + v[face] * line.vertices[0].radius * dotsVolumeCompensationScale;
                        meshBuffers->positionData[baseIndex + 1] = float3(line.vertices[1].position) - v[face] * line.vertices[1].radius * dotsVolumeCompensationScale;
                        meshBuffers->positionData[baseIndex + 2] = float3(line.vertices[1].position) + v[face] * line.vertices[1].radius * dotsVolumeCompensationScale;
                        meshBuffers->positionData[baseIndex + 3] = float3(line.vertices[0].position) + v[face] * line.vertices[0].radius * dotsVolumeCompensationScale;
                        meshBuffers->positionData[baseIndex + 4] = float3(line.vertices[0].position) - v[face] * line.vertices[0].radius * dotsVolumeCompensationScale;
                        meshBuffers->positionData[baseIndex + 5] = float3(line.vertices[1].position) - v[face] * line.vertices[1].radius * dotsVolumeCompensationScale;

                        uint n[2] = { vectorToSnorm8(-v[face]), vectorToSnorm8(v[face]) };
                        meshBuffers->normalData[baseIndex] = n[1];
                        meshBuffers->normalData[baseIndex + 1] = n[0];
                        meshBuffers->normalData[baseIndex + 2] = n[1];
                        meshBuffers->normalData[baseIndex + 3] = n[1];
                        meshBuffers->normalData[baseIndex + 4] = n[0];
                        meshBuffers->normalData[baseIndex + 5] = n[0];

                        uint tangent = vectorToSnorm8(fwd);
                        meshBuffers->tangentData[baseIndex] = tangent;
                        meshBuffers->tangentData[baseIndex + 1] = tangent;
                        meshBuffers->tangentData[baseIndex + 2] = tangent;
                        meshBuffers->tangentData[baseIndex + 3] = tangent;
                        meshBuffers->tangentData[baseIndex + 4] = tangent;
                        meshBuffers->tangentData[baseIndex + 5] = tangent;

                        meshBuffers->texcoord1Data[baseIndex] = float2(line.vertices[0].texCoord);
                        meshBuffers->texcoord1Data[baseIndex + 1] = float2(line.vertices[1].texCoord);
                        meshBuffers->texcoord1Data[baseIndex + 2] = float2(line.vertices[1].texCoord);
                        meshBuffers->texcoord1Data[baseIndex + 3] = float2(line.vertices[0].texCoord);
                        meshBuffers->texcoord1Data[baseIndex + 4] = float2(line.vertices[0].texCoord);
                        meshBuffers->texcoord1Data[baseIndex + 5] = float2(line.vertices[1].texCoord);

                        meshBuffers->radiusData[baseIndex] = line.vertices[0].radius * dotsVolumeCompensationScale;
                        meshBuffers->radiusData[baseIndex + 1] = line.vertices[1].radius * dotsVolumeCompensationScale;
                        meshBuffers->radiusData[baseIndex + 2] = line.vertices[1].radius * dotsVolumeCompensationScale;
                        meshBuffers->radiusData[baseIndex + 3] = line.vertices[0].radius * dotsVolumeCompensationScale;
                        meshBuffers->radiusData[baseIndex + 4] = line.vertices[0].radius * dotsVolumeCompensationScale;
                        meshBuffers->radiusData[baseIndex + 5] = line.vertices[1].radius * dotsVolumeCompensationScale;
                    }

                    ++globalIndex;
                }

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

                for (uint32_t index = 0; index < numLineSegments; ++index)
                {
                    meshBuffers->positionData[2 * globalIndex] = float3(lineSegments[globalIndex].vertices[0].position);
                    meshBuffers->positionData[2 * globalIndex + 1] = float3(lineSegments[globalIndex].vertices[1].position);
                    meshBuffers->radiusData[2 * globalIndex] = lineSegments[globalIndex].vertices[0].radius;
                    meshBuffers->radiusData[2 * globalIndex + 1] = lineSegments[globalIndex].vertices[1].radius;
                    ++globalIndex;
                }

                vertexOffsetInMesh += geometryNumVertices;
            }

            copyToMeshBuffersCache(TessellationType::LinearSweptSphere, meshBuffers, mesh->geometries);
        }
    }
}

void CurveTessellation::replacingSceneMesh(const TessellationType tessellationType, const std::vector<std::shared_ptr<MeshInstance>>& meshInstances)
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

            ++curveIndex;
        }
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

                    segment.geometryIndex = geometryIndex;

                    m_curvesLineSegments[meshIndex].push_back(segment);
                }
            }
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
