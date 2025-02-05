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

#include <memory>
#include <vector>
#include <donut/core/math/math.h>
#include <donut/engine/SceneGraph.h>
#include <rtxcr/geometry/include/CurveTessellation.h>

using namespace donut::math;
using namespace donut::engine;

struct UIData;

enum class TessellationType : uint32_t
{
    Polytube = 0,
    DisjointOrthogonalTriangleStrip,
    LinearSweptSphere,
    Count
};

class CurveTessellation
{
public:
    CurveTessellation(const std::vector<std::shared_ptr<MeshInstance>>& meshInstances, const UIData& ui);

    ~CurveTessellation() = default;

    // Cross section polygon order of faces (double amount of triangles) per linear segment.
    void convertToTrianglePolyTubes(const std::vector<std::shared_ptr<MeshInstance>>& meshInstances);

    /**
     * Order of triangle vertices of a single face is [0, 1, 2] and [0, 3, 1].
     *
     *  0 *---* 3       || line.points[0]
     *    |\  |         ||
     *    | \ |         ||
     *  2 *---* 1       \/ line.points[1]
     */

    // 2 faces (4 triangles) per linear segment.
    void convertToDisjointOrthogonalTriangleStrips(const std::vector<std::shared_ptr<MeshInstance>>& meshInstances);

    void convertToLinearSweptSpheres(const std::vector<std::shared_ptr<MeshInstance>>& meshInstances);

    void replacingSceneMesh(const TessellationType tessellationType, const std::vector<std::shared_ptr<MeshInstance>>& meshInstances);

    inline const std::vector<rtxcr::geometry::LineSegment>& GetCurvesLineSegments(const uint32_t meshIndex) const { return m_curvesLineSegments[meshIndex]; }

private:
    void convertCurveLineStripsToLineSegments(const std::vector<std::shared_ptr<MeshInstance>>& meshInstances);

    void copyToMeshBuffersCache(
        const TessellationType tessellationType,
        std::shared_ptr<BufferGroup> buffers,
        const std::vector<std::shared_ptr<MeshGeometry>>& geometries);

    std::vector<std::vector<rtxcr::geometry::LineSegment>> m_curvesLineSegments;

    std::vector<std::vector<MeshGeometry>> m_curveOriginalGeometryInfoCache;

    struct CurveMeshBuffersCache
    {
        std::shared_ptr<BufferGroup> buffers;
        std::vector<MeshGeometry> geometries;
    };
    std::vector<CurveMeshBuffersCache> m_curveMeshBuffersCache[(uint32_t)TessellationType::Count];

    const UIData& m_ui;
};
