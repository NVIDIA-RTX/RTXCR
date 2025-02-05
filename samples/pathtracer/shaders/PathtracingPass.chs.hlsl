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

#if LSS_GEOMETRY_SUPPORTED == 1
#define CONCAT(a,b) a##b
#define CONCAT_UAV(x) CONCAT(u,x)
#define NV_SHADER_EXTN_SLOT CONCAT_UAV(RTXCR_NVAPI_SHADER_EXT_SLOT)
#define NV_SHADER_EXTN_REGISTER_SPACE space0
#include "../../../external/nvapi/nvHLSLExtns.h"
#endif

#include "payloads.h"

[shader("closesthit")]
void ClosestHit(inout RayPayload payload : SV_RayPayload, in Attributes attrib : SV_IntersectionAttributes)
{
    payload.hitDistance = RayTCurrent();
    payload.instanceID = InstanceID();
    payload.primitiveIndex = PrimitiveIndex();
    payload.geometryIndex = GeometryIndex();
    payload.barycentrics = attrib.uv;

    uint packedDistance = asuint(payload.hitDistance) & (~0x1u);

#if LSS_GEOMETRY_SUPPORTED == 1
    if (NvRtIsLssHit())
    {
        packedDistance |= 0x1;

        const float2x4 lssObjectPositionsAndRadii = NvRtLssObjectPositionsAndRadii();
        payload.lssObjectPositionAndRadius0 = lssObjectPositionsAndRadii[0];
        payload.lssObjectPositionAndRadius1 = lssObjectPositionsAndRadii[1];
    }
#endif

    payload.hitDistance = asfloat(packedDistance);
    payload.objectRayDirection = ObjectRayDirection();
}

[shader("closesthit")]
void ClosestHitShadow(inout ShadowRayPayload payload: SV_RayPayload, in Attributes attrib: SV_IntersectionAttributes)
{
    payload.visibility = float3(0.0f, 0.0f, 0.0f);
}
