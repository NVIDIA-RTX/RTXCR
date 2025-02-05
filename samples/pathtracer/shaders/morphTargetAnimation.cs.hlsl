/*
 * Copyright (c) 2024-2025, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#include <shared/shared.h>

#include <rtxcr/utils/RtxcrMath.hlsli>

#pragma pack_matrix(row_major)

ConstantBuffer<MorphTargetConstants> g_Constants                   : register(b0);

StructuredBuffer<float4>             t_MorphTargetKeyframeData     : register(t0);
StructuredBuffer<float4>             t_MorphTargetNextKeyframeData : register(t1);
StructuredBuffer<LineSegment>        t_LineSegments                : register(t2);
Buffer<uint>                         t_MeshIndexBuffer             : register(t3);

RWByteAddressBuffer                  u_MorphTargetPosition         : register(u0);
RWBuffer<uint>                       u_MorphTargetNormal           : register(u1);
RWBuffer<uint>                       u_MorphTargetTangent          : register(u2);

#define VERTEX_PER_FACE 6

// Polytube
#define POLY_TUBE_ORDER 3
#define POLY_TUBE_TOTAL_VERTEX_PER_STRAND_SEGMENT (POLY_TUBE_ORDER * VERTEX_PER_FACE)

// DOTS
#define DOTS_FACE_ORDER 2
#define DOTS_TOTAL_VERTEX_PER_STRAND_SEGMENT (DOTS_FACE_ORDER * VERTEX_PER_FACE)

static uint morphTargetPositionIndexMapping[VERTEX_PER_FACE] = { 0, 1, 1, 0, 0, 1 };

#if RTXCR_CURVE_TESSELLATION_TYPE == RTXCR_CURVE_TESSELLATION_TYPE_POLYTUBE
static uint polyTubeVMapping[VERTEX_PER_FACE] = { 0, 1, 0, 0, 1, 1 };
#elif RTXCR_CURVE_TESSELLATION_TYPE == RTXCR_CURVE_TESSELLATION_TYPE_DOTS
static float dotsNormalSignMapping[VERTEX_PER_FACE] = { 1.0f, -1.0f, 1.0f, 1.0f, -1.0f, -1.0f };
#endif

uint vectorToSnorm8(in const float3 v)
{
    float scale = 127.0f / sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    int x = int(v.x * scale);
    int y = int(v.y * scale);
    int z = int(v.z * scale);
    return (x & 0xff) | ((y & 0xff) << 8) | ((z & 0xff) << 16);
}

// Generate a vector that is orthogonal to the input vector
// This can be used to invent a tangent frame for meshes that don't have real tangents/bitangents.
float3 perpStark(in const float3 u)
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
void buildFrame(in const float3 n, inout float3 t, inout float3 b)
{
    t = perpStark(n);
    b = cross(n, t);
}

float3 getUnitCircleCoords(in const float3 xAxis, in const float3 yAxis, in const float angleRadians)
{
    // We only care about angles < 2PI
    float unused = 0.f;
    float unitCircleFraction = modf(angleRadians / RTXCR_TWO_PI, unused);

    if (unitCircleFraction < 0.f)
    {
        unitCircleFraction = 1.0f - unitCircleFraction;
    }

    float adjustedAngleRadians = unitCircleFraction * RTXCR_TWO_PI;

    return cos(adjustedAngleRadians) * xAxis + sin(adjustedAngleRadians) * yAxis;
}

#if RTXCR_CURVE_TESSELLATION_TYPE == RTXCR_CURVE_TESSELLATION_TYPE_POLYTUBE

// SIMD version of convertToTrianglePolyTubes in CurveTessellation.cpp

void convertToTrianglePolyTubes(const uint index)
{
    uint lineSegmentIndex = index / POLY_TUBE_TOTAL_VERTEX_PER_STRAND_SEGMENT;
    const LineSegment lineSegment = t_LineSegments[lineSegmentIndex];
    lineSegmentIndex += lineSegment.geometryIndex;

    const float3 morphTargetLerpData[2] =
    {
        lerp(t_MorphTargetKeyframeData[lineSegmentIndex], t_MorphTargetNextKeyframeData[lineSegmentIndex], g_Constants.lerpWeight).xyz,
        lerp(t_MorphTargetKeyframeData[lineSegmentIndex + 1], t_MorphTargetNextKeyframeData[lineSegmentIndex + 1], g_Constants.lerpWeight).xyz
    };

    const float3 morphTargetPosition[2] =
    {
        lineSegment.point0 + morphTargetLerpData[0],
        lineSegment.point1 + morphTargetLerpData[1]
    };

    // Build the initial frame
    float3 fwd, s, t;
    fwd = normalize(morphTargetPosition[1] - morphTargetPosition[0]);
    buildFrame(fwd, s, t);
    const uint tangent = vectorToSnorm8(fwd);

    const uint face = (index % POLY_TUBE_TOTAL_VERTEX_PER_STRAND_SEGMENT) / VERTEX_PER_FACE;
    const uint mappingIndex = (index % POLY_TUBE_TOTAL_VERTEX_PER_STRAND_SEGMENT) % VERTEX_PER_FACE;

    const uint vIndex = polyTubeVMapping[mappingIndex];
    const float angleRadians = (RTXCR_TWO_PI * (float)(face + vIndex) / POLY_TUBE_ORDER);
    const float3 v = getUnitCircleCoords(s, t, angleRadians);

    const float r[2] = { lineSegment.radius0, lineSegment.radius1 };

    const uint morphTargetPositionIndex = morphTargetPositionIndexMapping[mappingIndex];

    // Necessary to make up for lost volume of PolyTube approximation of a circular tube

    // Position
    const float polyTubesVolumeCompensationScale = 1.0f / (sin(RTXCR_PI / POLY_TUBE_ORDER) / (RTXCR_PI / POLY_TUBE_ORDER));
    const float3 vertexPosition = morphTargetPosition[morphTargetPositionIndex] + r[morphTargetPositionIndex] * v * polyTubesVolumeCompensationScale;
    u_MorphTargetPosition.Store3(index * 4 * 3, asuint(vertexPosition));

    // Normal
    u_MorphTargetNormal[index] = vectorToSnorm8(v);

    // Tangent
    u_MorphTargetTangent[index] = tangent;
}

#elif RTXCR_CURVE_TESSELLATION_TYPE == RTXCR_CURVE_TESSELLATION_TYPE_DOTS

// SIMD version of convertToDisjointOrthogonalTriangleStrips in CurveTessellation.cpp
void convertToDisjointOrthogonalTriangleStrips(const uint index)
{
    uint lineSegmentIndex = index / DOTS_TOTAL_VERTEX_PER_STRAND_SEGMENT;
    const LineSegment lineSegment = t_LineSegments[lineSegmentIndex];
    lineSegmentIndex += lineSegment.geometryIndex;

    // Get the line vertex position of 2 keyframes and do interpolation:
    //   t_MorphTargetKeyframeData is the morph target line vertex buffer of keyframe N,
    //   t_MorphTargetNextKeyframeData is the morph target line vertex buffer of keyframe N + 1
    //   With lineSegmentIndex and lineSegmentIndex + 1 we get start and end vertex of the line for both these 2 keyframes,
    //   then we just simply do lerp to get the interpolated line.
    const float3 morphTargetLerpData[2] =
    {
        lerp(t_MorphTargetKeyframeData[lineSegmentIndex], t_MorphTargetNextKeyframeData[lineSegmentIndex], g_Constants.lerpWeight).xyz,
        lerp(t_MorphTargetKeyframeData[lineSegmentIndex + 1], t_MorphTargetNextKeyframeData[lineSegmentIndex + 1], g_Constants.lerpWeight).xyz
    };

    const float3 morphTargetPosition[DOTS_FACE_ORDER] =
    {
        lineSegment.point0 + morphTargetLerpData[0],
        lineSegment.point1 + morphTargetLerpData[1]
    };

    // Build the initial frame
    float3 fwd, s, t;
    fwd = normalize(morphTargetPosition[1] - morphTargetPosition[0]);
    buildFrame(fwd, s, t);

    const float3 v[DOTS_FACE_ORDER] = { s, t };

    const float weight[VERTEX_PER_FACE] = {
        lineSegment.radius0, -lineSegment.radius1,  lineSegment.radius1,
        lineSegment.radius0, -lineSegment.radius0, -lineSegment.radius1 };
    const uint mappingIndex = (index % DOTS_TOTAL_VERTEX_PER_STRAND_SEGMENT) % VERTEX_PER_FACE;

    const uint morphTargetPositionIndex = morphTargetPositionIndexMapping[mappingIndex];

    const uint face = (index % DOTS_TOTAL_VERTEX_PER_STRAND_SEGMENT) / VERTEX_PER_FACE;

    // Necessary to make up for lost volume of PolyTube approximation of a circular tube

    // Position
    const float dotsVolumeCompensationScale = 1.0f / (sin(RTXCR_PI / 4.0f) / (RTXCR_PI / 4.0f));
    const float3 vertexPosition = morphTargetPosition[morphTargetPositionIndex] + weight[mappingIndex] * v[face] * dotsVolumeCompensationScale;
    u_MorphTargetPosition.Store3(index * 4 * 3, asuint(vertexPosition));

    // Normal
    const float normalSign = dotsNormalSignMapping[mappingIndex];
    const uint normalPacked = vectorToSnorm8(normalSign.xxx * v[face]);
    u_MorphTargetNormal[index] = normalPacked;

    // Tangent
    const uint tangentPacked = vectorToSnorm8(fwd);
    u_MorphTargetTangent[index] = tangentPacked;
}

#elif RTXCR_CURVE_TESSELLATION_TYPE == RTXCR_CURVE_TESSELLATION_TYPE_LSS

void convertToLinearSweptSpheres(const uint globalIndex)
{
    // TODO: Consider updating multiple vertices per thread
    uint index = t_MeshIndexBuffer[globalIndex];
    uint nextIndex = t_MeshIndexBuffer[globalIndex + 1];

    uint lineSegmentIndex = globalIndex;
    const LineSegment lineSegment = t_LineSegments[lineSegmentIndex];
    lineSegmentIndex += lineSegment.geometryIndex;

    float3 morphTargetLerpData = lerp(t_MorphTargetKeyframeData[lineSegmentIndex], t_MorphTargetNextKeyframeData[lineSegmentIndex], g_Constants.lerpWeight).xyz;
    float3 vertexPosition = lineSegment.point0 + morphTargetLerpData;

    // Position
    u_MorphTargetPosition.Store3(index * 4 * 3, asuint(vertexPosition));

    if (globalIndex == g_Constants.vertexCount - 1 || nextIndex != index + 1)
    {
        // float3 morphTargetLerpData = lerp(t_MorphTargetKeyframeData[lineSegmentIndex + 1], t_MorphTargetNextKeyframeData[lineSegmentIndex + 1], g_Constants.lerpWeight).xyz;
        float3 vertexPosition = lineSegment.point1 + morphTargetLerpData;
        u_MorphTargetPosition.Store3((index + 1) * 4 * 3, asuint(vertexPosition));
    }
}

#endif // RTXCR_CURVE_TESSELLATION_TYPE

[numthreads(64, 1, 1)]
void main_cs(in int globalIndex : SV_DispatchThreadID)
{
    if (globalIndex >= g_Constants.vertexCount)
    {
        return;
    }

#if RTXCR_CURVE_TESSELLATION_TYPE == RTXCR_CURVE_TESSELLATION_TYPE_POLYTUBE
    convertToTrianglePolyTubes(globalIndex);
#elif RTXCR_CURVE_TESSELLATION_TYPE == RTXCR_CURVE_TESSELLATION_TYPE_DOTS
    convertToDisjointOrthogonalTriangleStrips(globalIndex);
#elif RTXCR_CURVE_TESSELLATION_TYPE == RTXCR_CURVE_TESSELLATION_TYPE_LSS
    convertToLinearSweptSpheres(globalIndex);
#elif RTXCR_TRIANGLES == RTXCR_CURVE_TESSELLATION_TYPE_TRIANGLE
    // TODO: Debug Triangles
#endif
}
