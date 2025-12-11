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

#include <donut/shaders/packing.hlsli>
#include <donut/shaders/scene_material.hlsli>

#include <rtxcr/geometry.hlsli>

enum GeometryAttributes
{
    GeomAttr_Position = 0x01,
    GeomAttr_TexCoord = 0x02,
    GeomAttr_Normal = 0x04,
    GeomAttr_Tangents = 0x08,
    GeomAttr_Radius = 0x10,
    GeomAttr_All = 0x1F
};

struct GeometrySample
{
    InstanceData instance;
    GeometryData geometry;
    MaterialConstants material;

    float3 objectSpacePosition;
    float2 texcoord;
    float3 faceNormal;
    float3 geometryNormal;
    float4 tangent;
    float curveRadius;

    // Motion Vector Positions
    float3 curveObjectSpacePosition;
    float3 curveObjectSpacePositionPrev;
};

float3 getGeometryFromHitPreviousFrameDOTS(
    const GeometrySample gs,
    const uint3 indices,
    const float3 barycentrics,
    const float3 normal0,
    const float3 normal2,
    ByteAddressBuffer prevVertexBuffer,
    const float3 objectRayOrigin,
    const float3 objectRayDirection)
{
    float3 vertexPosition0 = 0.0f;
    float3 vertexPosition2 = 0.0f;

    float3 vertexPositions[3];
    vertexPositions[0] = asfloat(prevVertexBuffer.Load3(gs.geometry.positionOffset + indices.x * c_SizeOfPosition));
    vertexPositions[1] = asfloat(prevVertexBuffer.Load3(gs.geometry.positionOffset + indices.y * c_SizeOfPosition));
    vertexPositions[2] = asfloat(prevVertexBuffer.Load3(gs.geometry.positionOffset + indices.z * c_SizeOfPosition));

    vertexPosition0 = vertexPositions[0];
    vertexPosition2 = vertexPositions[2];

    float curveRadius[3];
    curveRadius[0] = asfloat(prevVertexBuffer.Load(gs.geometry.curveRadiusOffset + indices.x * c_SizeOfCurveRadius));
    curveRadius[1] = asfloat(prevVertexBuffer.Load(gs.geometry.curveRadiusOffset + indices.y * c_SizeOfCurveRadius));
    curveRadius[2] = asfloat(prevVertexBuffer.Load(gs.geometry.curveRadiusOffset + indices.z * c_SizeOfCurveRadius));

    // The first vertex of each triangle is at the beginning of curve segment
    const float3 objectCurveVertex0Pos = vertexPosition0 - curveRadius[0] * normal0;

    // The last vertex of each triangle is at the end of curve segment
    const float3 objectCurveVertex1Pos = vertexPosition2 - curveRadius[2] * normal2;

    const float3 objectNormal = AdjustGeometryNormalDOTS(objectRayOrigin, objectRayDirection, objectCurveVertex0Pos, objectCurveVertex1Pos, curveRadius[0], curveRadius[2]);

    const float3 objectTangent = normalize(objectCurveVertex1Pos - objectCurveVertex0Pos);
    const float u = dot(objectTangent, gs.objectSpacePosition - objectCurveVertex0Pos);
    const float3 objectCurvePositionPrev = objectCurveVertex0Pos + u * objectTangent;
    const float3 curveObjectSpacePositionPrev = objectCurvePositionPrev + gs.curveRadius * objectNormal;

    return curveObjectSpacePositionPrev;
}

// Assuming that scattering happens only on triangle-based meshes
GeometrySample getGeometryFromHitFastSss(
    const GeometrySample initialSssGeometry,
    ByteAddressBuffer indexBuffer,
    ByteAddressBuffer vertexBuffer,
    uint primitiveIndex,
    float2 rayBarycentrics,
    GeometryAttributes attributes,
    float3 objectRayOrigin,
    float hitDistance,
    float3 objectRayDirection,
    const bool isMorphTarget,
    ByteAddressBuffer prevVertexBuffer)
{
    GeometrySample gs = (GeometrySample)0;
    gs.instance = initialSssGeometry.instance;
    gs.geometry = initialSssGeometry.geometry;
    gs.material = initialSssGeometry.material;

    const float3 barycentrics = float3(1.0f - (rayBarycentrics.x + rayBarycentrics.y), rayBarycentrics.xy);

    const uint3 indices = indexBuffer.Load3(gs.geometry.indexOffset + primitiveIndex * c_SizeOfTriangleIndices);

    float3 vertexPosition0;
    float3 vertexPosition2;
    const bool hasPosition = attributes & GeomAttr_Position;
    {
        float3 vertexPositions[3];
        vertexPositions[0] = hasPosition ? asfloat(vertexBuffer.Load3(gs.geometry.positionOffset + indices.x * c_SizeOfPosition)) : 0.0f;
        vertexPositions[1] = hasPosition ? asfloat(vertexBuffer.Load3(gs.geometry.positionOffset + indices.y * c_SizeOfPosition)) : 0.0f;
        vertexPositions[2] = hasPosition ? asfloat(vertexBuffer.Load3(gs.geometry.positionOffset + indices.z * c_SizeOfPosition)) : 0.0f;

        gs.objectSpacePosition = interpolate(vertexPositions, barycentrics);
        if (hasPosition && isMorphTarget)
        {
            float3 vertexPositionsPrev[3];
            vertexPositionsPrev[0] = asfloat(prevVertexBuffer.Load3(gs.geometry.positionOffset + indices.x * c_SizeOfPosition));
            vertexPositionsPrev[1] = asfloat(prevVertexBuffer.Load3(gs.geometry.positionOffset + indices.y * c_SizeOfPosition));
            vertexPositionsPrev[2] = asfloat(prevVertexBuffer.Load3(gs.geometry.positionOffset + indices.z * c_SizeOfPosition));

            gs.curveObjectSpacePosition = gs.objectSpacePosition;
            gs.curveObjectSpacePositionPrev = interpolate(vertexPositionsPrev, barycentrics);
        }

        vertexPosition0 = vertexPositions[0];
        vertexPosition2 = vertexPositions[2];

        const float3 objectFaceNormal = hasPosition ? normalize(cross(vertexPositions[1] - vertexPositions[0], vertexPositions[2] - vertexPositions[0])) : 0.0f;
        gs.faceNormal = hasPosition ? normalize(mul(gs.instance.transform, float4(objectFaceNormal, 0.0)).xyz) : 0.0f;
    }

    {
        const bool hasTexCoord = (attributes & GeomAttr_TexCoord) && (gs.geometry.texCoord1Offset != ~0u);
        float2 vertexTexcoords[3];
        vertexTexcoords[0] = hasTexCoord ? asfloat(vertexBuffer.Load2(gs.geometry.texCoord1Offset + indices.x * c_SizeOfTexcoord)) : 0.0f;
        vertexTexcoords[1] = hasTexCoord ? asfloat(vertexBuffer.Load2(gs.geometry.texCoord1Offset + indices.y * c_SizeOfTexcoord)) : 0.0f;
        vertexTexcoords[2] = hasTexCoord ? asfloat(vertexBuffer.Load2(gs.geometry.texCoord1Offset + indices.z * c_SizeOfTexcoord)) : 0.0f;
        gs.texcoord = interpolate(vertexTexcoords, barycentrics);
    }

    float3 normal0;
    float3 normal2;
    {
        const bool hasNormals = (attributes & GeomAttr_Normal) && (gs.geometry.normalOffset != ~0u);
        float3 normals[3];
        normals[0] = hasNormals ? Unpack_RGB8_SNORM(vertexBuffer.Load(gs.geometry.normalOffset + indices.x * c_SizeOfNormal)) : 0.0f;
        normals[1] = hasNormals ? Unpack_RGB8_SNORM(vertexBuffer.Load(gs.geometry.normalOffset + indices.y * c_SizeOfNormal)) : 0.0f;
        normals[2] = hasNormals ? Unpack_RGB8_SNORM(vertexBuffer.Load(gs.geometry.normalOffset + indices.z * c_SizeOfNormal)) : 0.0f;

        normal0 = normals[0];
        normal2 = normals[2];

        gs.geometryNormal = interpolate(normals, barycentrics);
        gs.geometryNormal = mul(gs.instance.transform, float4(gs.geometryNormal, 0.0)).xyz;
        gs.geometryNormal = hasNormals ? normalize(gs.geometryNormal) : 0.0f;
    }

    {
        const bool hasTangents = (attributes & GeomAttr_Tangents) && (gs.geometry.tangentOffset != ~0u);
        float4 tangents[3];
        tangents[0] = hasTangents ? Unpack_RGBA8_SNORM(vertexBuffer.Load(gs.geometry.tangentOffset + indices.x * c_SizeOfNormal)) : 0.0f;
        tangents[1] = hasTangents ? Unpack_RGBA8_SNORM(vertexBuffer.Load(gs.geometry.tangentOffset + indices.y * c_SizeOfNormal)) : 0.0f;
        tangents[2] = hasTangents ? Unpack_RGBA8_SNORM(vertexBuffer.Load(gs.geometry.tangentOffset + indices.z * c_SizeOfNormal)) : 0.0f;
        gs.tangent.xyz = interpolate(tangents, barycentrics).xyz;
        gs.tangent.xyz = mul(gs.instance.transform, float4(gs.tangent.xyz, 0.0)).xyz;
        gs.tangent.xyz = hasTangents ? normalize(gs.tangent.xyz) : 0.0f;
        gs.tangent.w = hasTangents ? tangents[0].w : 0.0f;
    }

    {
        const bool hasRadius = (attributes & GeomAttr_Radius) && (gs.geometry.curveRadiusOffset != ~0u);
        const bool isDots = (gs.instance.flags & InstanceFlags_CurveDisjointOrthogonalTriangleStrips) != 0;

        float curveRadius[3];
        curveRadius[0] = hasRadius ? asfloat(vertexBuffer.Load(gs.geometry.curveRadiusOffset + indices.x * c_SizeOfCurveRadius)) : 0.0f;
        curveRadius[1] = hasRadius ? asfloat(vertexBuffer.Load(gs.geometry.curveRadiusOffset + indices.y * c_SizeOfCurveRadius)) : 0.0f;
        curveRadius[2] = hasRadius ? asfloat(vertexBuffer.Load(gs.geometry.curveRadiusOffset + indices.z * c_SizeOfCurveRadius)) : 0.0f;
        gs.curveRadius = interpolate(curveRadius, barycentrics);

        if (isDots)
        {
            // The first vertex of each triangle is at the beginning of curve segment
            float3 objectCurveVertex0Pos = vertexPosition0 - curveRadius[0] * normal0;

            // The last vertex of each triangle is at the end of curve segment
            float3 objectCurveVertex1Pos = vertexPosition2 - curveRadius[2] * normal2;

            float3 objectNormal = AdjustGeometryNormalDOTS(objectRayOrigin, objectRayDirection, objectCurveVertex0Pos, objectCurveVertex1Pos, curveRadius[0], curveRadius[2]);

            gs.geometryNormal = mul(gs.instance.transform, float4(objectNormal, 0.0)).xyz;
            gs.geometryNormal = normalize(gs.geometryNormal);
            gs.faceNormal = gs.geometryNormal;

            float3 objectTangent = normalize(objectCurveVertex1Pos - objectCurveVertex0Pos);
            float u = dot(objectTangent, gs.objectSpacePosition - objectCurveVertex0Pos);
            float3 objectCurvePosition = objectCurveVertex0Pos + u * objectTangent;
            gs.objectSpacePosition = objectCurvePosition + gs.curveRadius * objectNormal;

            if (hasPosition && hasRadius && isMorphTarget)
            {
                gs.curveObjectSpacePosition = gs.objectSpacePosition;
                gs.curveObjectSpacePositionPrev = getGeometryFromHitPreviousFrameDOTS(gs, indices, barycentrics, normal0, normal2, prevVertexBuffer, objectRayOrigin, objectRayDirection);
            }
        }
    }

    return gs;
}

GeometrySample getGeometryFromHit(
    const uint instanceIndex,
    const uint primitiveIndex,
    const uint geometryIndex,
    const float2 rayBarycentrics,
    const GeometryAttributes attributes,
    const float3 objectRayOrigin,
    const float hitDistance,
    const float3 objectRayDirection,
    const float4 lssObjectPositionAndRadius0,
    const float4 lssObjectPositionAndRadius1,
    const bool isMorphTarget,
    StructuredBuffer<InstanceData> instanceBuffer,
    StructuredBuffer<GeometryData> geometryBuffer,
    StructuredBuffer<MaterialConstants> materialBuffer)
{
    GeometrySample gs;
    gs.instance = instanceBuffer[instanceIndex];
    gs.geometry = geometryBuffer[gs.instance.firstGeometryIndex + geometryIndex];
    gs.material = materialBuffer[gs.geometry.materialIndex];

    const uint vertexBufferIndex = gs.geometry.vertexBufferIndex + (uint)isMorphTarget * (g_Global.frameIndex % 2);

    ByteAddressBuffer indexBuffer = t_BindlessBuffers[NonUniformResourceIndex(gs.geometry.indexBufferIndex)];
    ByteAddressBuffer vertexBuffer = t_BindlessBuffers[NonUniformResourceIndex(vertexBufferIndex)];
    ByteAddressBuffer prevVertexBuffer = t_BindlessBuffers[NonUniformResourceIndex(gs.geometry.vertexBufferIndex + ((g_Global.frameIndex + 1) % 2))];

    gs.curveObjectSpacePosition = 0.0f;
    gs.curveObjectSpacePositionPrev = 0.0f;

    if (!gs.instance.IsCurveLSS())
    {
        gs = getGeometryFromHitFastSss(
            gs,
            indexBuffer,
            vertexBuffer,
            primitiveIndex,
            rayBarycentrics,
            attributes,
            objectRayOrigin,
            hitDistance,
            objectRayDirection,
            isMorphTarget,
            prevVertexBuffer);
    }
    else
    {
#if API_DX12 == 1
        const float3 p0 = lssObjectPositionAndRadius0.xyz;
        const float3 p1 = lssObjectPositionAndRadius1.xyz;

        const float r0 = lssObjectPositionAndRadius0.w;
        const float r1 = lssObjectPositionAndRadius1.w;
#else
        const uint2 indices = uint2(primitiveIndex * 2, primitiveIndex * 2 + 1);
        const float3 p0 = asfloat(vertexBuffer.Load3(gs.geometry.positionOffset + indices.x * c_SizeOfPosition));
        const float3 p1 = asfloat(vertexBuffer.Load3(gs.geometry.positionOffset + indices.y * c_SizeOfPosition));

        const float r0 = asfloat(vertexBuffer.Load(gs.geometry.curveRadiusOffset + indices.x * c_SizeOfCurveRadius));
        const float r1 = asfloat(vertexBuffer.Load(gs.geometry.curveRadiusOffset + indices.y * c_SizeOfCurveRadius));
#endif
        const float u = rayBarycentrics.x;
        const float3 p = lerp(p0, p1, u);

        gs.objectSpacePosition = objectRayOrigin + hitDistance * objectRayDirection;

        gs.geometryNormal = gs.objectSpacePosition - p;
        gs.geometryNormal = mul(gs.instance.transform, float4(gs.geometryNormal, 0.0)).xyz;
        gs.geometryNormal = normalize(gs.geometryNormal);
        gs.faceNormal = gs.geometryNormal;

        gs.tangent.xyz = mul(gs.instance.transform, float4(p1 - p0, 0.0)).xyz;
        gs.tangent.xyz = normalize(gs.tangent.xyz);
        // TODO: Confirm this is correct
        gs.tangent.w = 0.0f;

        gs.curveRadius = lerp(r0, r1, u);

        // Previous Frame Vertex Position
        if (isMorphTarget)
        {
            ByteAddressBuffer prevVertexBuffer = t_BindlessBuffers[NonUniformResourceIndex(gs.geometry.vertexBufferIndex + ((g_Global.frameIndex + 1) % 2))];
            const float3 p0Prev = asfloat(prevVertexBuffer.Load3(gs.geometry.positionOffset + (2 * primitiveIndex) * c_SizeOfPosition));
            const float3 p1Prev = asfloat(prevVertexBuffer.Load3(gs.geometry.positionOffset + (2 * primitiveIndex + 1) * c_SizeOfPosition));
            const float3 prevPos = lerp(p0Prev, p1Prev, u);

            gs.curveObjectSpacePosition = p;
            gs.curveObjectSpacePositionPrev = lerp(p0Prev, p1Prev, u);
        }
    }

    return gs;
}
