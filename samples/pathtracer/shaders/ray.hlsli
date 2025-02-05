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

#include <donut/shaders/view_cb.h>
#include "utils.hlsli"
#include "payloads.h"

#define SHADOW_RAY_INDEX 1

RayDesc generatePinholeCameraRay(const PlanarViewConstants view, const float2 normalisedDeviceCoordinate)
{
    // Set up the ray
    RayDesc ray;
    ray.Origin = view.matViewToWorld[3].xyz;
    ray.TMin = 0.0f;
    ray.TMax = FLT_MAX;

    // Extract the aspect ratio and fov from the projection matrix
    float aspect = view.matViewToClip[1][1] / view.matViewToClip[0][0];
    float tanHalfFovY = 1.0f / view.matViewToClip[1][1];

    // Compute the ray direction
    ray.Direction = normalize(
        ((normalisedDeviceCoordinate.x * 2.f - 1.f) * view.matViewToWorld[0].xyz * tanHalfFovY * aspect) -
        ((normalisedDeviceCoordinate.y * 2.f - 1.f) * view.matViewToWorld[1].xyz * tanHalfFovY) +
        view.matViewToWorld[2].xyz);

    return ray;
}

RayDesc setupPrimaryRay(const PlanarViewConstants view, const uint2 pixelPosition)
{
    float2 uv = (float2(pixelPosition) + 0.5f) * view.viewportSizeInv;
    float4 clipPos = float4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, 0.5f, 1.0f);
    float4 worldPos = mul(clipPos, view.matClipToWorld);
    worldPos.xyz /= worldPos.w;

    RayDesc ray;
    ray.Origin = view.cameraDirectionOrPosition.xyz;
    ray.Direction = normalize(worldPos.xyz - ray.Origin);
    ray.TMin = 0.0f;
    ray.TMax = FLT_MAX;
    return ray;
}

RayDesc setupShadowRay(const float3 direction,
                       const float3 surfacePos,
                       const float3 viewIncident)
{
    RayDesc ray;
    ray.Origin = surfacePos - viewIncident * 0.001;
    ray.Direction = direction;
    ray.TMin = 0.0f;
    ray.TMax = FLT_MAX;
    return ray;
}

// Casts a shadow ray and returns true if light is not occluded ie. it hits nothing
// Note that we use dedicated hit group with simpler shaders for shadow rays
float3 castShadowRay(const RaytracingAccelerationStructure sceneBVH,
                     const float3 rayOrigin,
                     const float3 rayDirection,
                     const float tracingDistance,
                     const bool backFaceCulling = true)
{
    RayDesc ray;
    ray.Origin = rayOrigin;
    ray.Direction = rayDirection;
    ray.TMin = 0.0f;
    ray.TMax = tracingDistance;

    ShadowRayPayload payload = createDefaultShadowRayPayload();

    uint rayFlags = backFaceCulling ? RAY_FLAG_CULL_BACK_FACING_TRIANGLES : RAY_FLAG_NONE;
    rayFlags |= RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH;

    TraceRay(sceneBVH, rayFlags, 0x3, SHADOW_RAY_INDEX, 0, SHADOW_RAY_INDEX, ray, payload);

    return payload.visibility;
}

RayPayload sampleSubsurface(const RaytracingAccelerationStructure sceneBVH,
                            const float3 samplePosition,
                            const float3 surfaceNormal,
                            const float tracingDistance)
{
    RayDesc sssSampleRay;
    sssSampleRay.Origin = samplePosition;
    sssSampleRay.Direction = -surfaceNormal; // Shooting ray towards the surface
    sssSampleRay.TMin = 0.0f;
    sssSampleRay.TMax = tracingDistance;

    RayPayload payload = createDefaultRayPayload();

    TraceRay(SceneBVH, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, 0xFF, 0, 0, 0, sssSampleRay, payload);

    return payload;
}
