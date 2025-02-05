/*
 * Copyright (c) 2024-2025, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef PAYLOADS_H_
#define PAYLOADS_H_

struct RayPayload
{
    float hitDistance;
    uint instanceID;
    uint primitiveIndex;
    uint geometryIndex;
    float2 barycentrics;
    float3 objectRayDirection;
    float4 lssObjectPositionAndRadius0;
    float4 lssObjectPositionAndRadius1;

#ifndef __cplusplus

    bool Hit()
    {
        return hitDistance > 0.0f;
    }

    bool IsLss()
    {
        return asuint(hitDistance) & 0x1;
    }

    float HitT()
    {
        return hitDistance;
    }

#endif // __cplusplus
};

struct ShadowRayPayload
{
    float3 visibility;
};

#ifndef __cplusplus

struct Attributes
{
    float2 uv;
};

RayPayload createDefaultRayPayload()
{
    RayPayload rayPayload = (RayPayload) 0;
    rayPayload.hitDistance = -1.0f;
    rayPayload.instanceID = ~0U;
    rayPayload.primitiveIndex = ~0U;
    rayPayload.geometryIndex = ~0U;
    rayPayload.barycentrics = 0;
    rayPayload.objectRayDirection = 0.0f;
    rayPayload.lssObjectPositionAndRadius0 = 0.0f;
    rayPayload.lssObjectPositionAndRadius1 = 0.0f;

    return rayPayload;
}

ShadowRayPayload createDefaultShadowRayPayload()
{
    ShadowRayPayload shadowRayPayload = (ShadowRayPayload) 0;
    shadowRayPayload.visibility = float3(1.0f, 1.0f, 1.0f);
    return shadowRayPayload;
}

#endif // __cplusplus

#endif // PAYLOADS_H_
