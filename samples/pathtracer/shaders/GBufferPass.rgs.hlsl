/*
 * Copyright (c) 2024-2025, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#pragma pack_matrix(row_major)

#include <shared/globalCb.h>
#include <shared/lightingCb.h>

#include <rtxcr/utils/RtxcrMath.hlsli>

#include "GBufferBindings.hlsli"
#include "ray.hlsli"
#include "geometry.hlsli"
#include "material.hlsli"
#include "sampling.hlsli"
#include "hair.hlsli"

void writeGBuffer(const uint2 pixel,
                  const float3 hitPos,
                  const float3 shadingNormal,
                  const float roughness,
                  const float3 emissive,
                  const float3 diffuseAlbedo,
                  const float3 f0,
                  const float NoV)
{
    float viewZ = dot(hitPos - g_Lighting.view.matViewToWorld[3].xyz, g_Lighting.view.matViewToWorld[2].xyz);
    float nearZ = g_Lighting.view.matClipToView[3].z;
    u_OutputViewSpaceZ[pixel] = viewZ;
    u_OutputDeviceZ[pixel] = nearZ / viewZ;
    u_OutputNormalRoughness[pixel] = float4(shadingNormal, roughness);
    // Motion vectors
    {
        float4 positionClip = mul(float4(hitPos, 1.0f), g_Lighting.view.matWorldToClip);
        positionClip.xyz /= positionClip.w;
        float4 positionClipPrev = mul(float4(hitPos, 1.0f), g_Lighting.viewPrev.matWorldToClip);
        positionClipPrev.xyz /= positionClipPrev.w;

        float3 motionVector;
        motionVector.xy = (positionClipPrev.xy - positionClip.xy) * g_Lighting.view.clipToWindowScale;
        motionVector.z = positionClipPrev.w - positionClip.w;

        u_OutputMotionVectors[pixel] = float4(motionVector, 0.0f);
        u_OutputScreenSpaceMotionVectors[pixel] = motionVector.xy;
    }
    u_OutputEmissive[pixel] = float4(emissive, 1.0f);
    u_OutputDiffuseAlbedo[pixel] = float4(diffuseAlbedo, 1);
    u_OutputSpecularAlbedo[pixel] = float4(EnvBRDFApprox2(f0, roughness * roughness, NoV), 1.0f);
}

float GetRayOriginOffsetDistance(GeometrySample geometry, bool transition)
{
    float extraOffsetDistance = 0.0f;

    if (geometry.instance.IsCurveDOTS())
    {
        extraOffsetDistance = transition ? 2.0f * geometry.curveRadius : 0.0f;
    }

    return extraOffsetDistance;
}

bool traceIndirect(const MaterialSample material,
                   const GeometrySample geometry,
                   const float3 viewVector,
                   const float3 faceNormal,
                   const float3 shadingNormal,
                   const float3 hitPos,
                   inout uint rngState,
                   inout RayDesc ray)
{
    float3 bsdfWeight = float3(0.0f, 0.0f, 0.0f);
    float bsdfPdf = 0.0f;
    float refractiveIndex = 1.0f;

    bool continueTrace = false;
    // Generates a new ray direction
    const MaterialProperties materialProps = createMaterialProperties(material);
    const float2 rand2 = float2(Rand(rngState), Rand(rngState));
    continueTrace = evalIndirectCombinedBRDF(rand2, shadingNormal, faceNormal, viewVector, materialProps, TRANSMISSIVE_TYPE, refractiveIndex, ray.Direction, bsdfWeight, bsdfPdf);

    if (!continueTrace)
    {
        return false; // Current path is eliminated by the surface
    }

    // Refraction requires the ray offset to go in the opposite direction
    const float extraOffsetDistance = GetRayOriginOffsetDistance(geometry, true);
    ray.Origin = OffsetRayOrigin(hitPos, -faceNormal, extraOffsetDistance);

    return true;
}

[shader("raygeneration")]
void RayGen()
{
    const uint2 pixelIndex = DispatchRaysIndex().xy;
    const float2 pixel = float2(pixelIndex + 0.5f) - g_Global.jitterOffset;
    const uint2 launchDimensions = DispatchRaysDimensions().xy;
    uint rngState = InitRNG(pixelIndex, launchDimensions, g_Global.frameIndex);

    RayDesc ray = generatePinholeCameraRay(g_Lighting.view, pixel / (float2)launchDimensions);

    bool isLensPath = false;

    for (uint bounce = 0; bounce < g_Global.bouncesMax; bounce++)
    {
        RayPayload payload = createDefaultRayPayload();
        const uint rayFlags = (!g_Global.enableBackFaceCull) ? RAY_FLAG_NONE : RAY_FLAG_CULL_BACK_FACING_TRIANGLES;
        const uint InstanceInclusionMask = (bounce != 0) ? 0xFF : 0x1;
        TraceRay(SceneBVH, rayFlags, InstanceInclusionMask, 0, 0, 0, ray, payload);

        if (!payload.Hit())
        {
            // Motion vectors
            const float3 hitPos = ray.Origin + ray.Direction * TRACING_FAR_DISTANCE;

            float4 positionClip = mul(float4(hitPos, 1.0f), g_Lighting.view.matWorldToClip);
            positionClip.xyz /= positionClip.w;
            float4 positionClipPrev = mul(float4(hitPos, 1.0f), g_Lighting.viewPrev.matWorldToClip);
            positionClipPrev.xyz /= positionClipPrev.w;

            float3 motionVector;
            motionVector.xy = (positionClipPrev.xy - positionClip.xy) * g_Lighting.view.clipToWindowScale;
            motionVector.z = positionClipPrev.w - positionClip.w;

            u_OutputMotionVectors[pixelIndex] = float4(motionVector, 0.0f);
            u_OutputScreenSpaceMotionVectors[pixelIndex] = motionVector.xy;

            float viewZ = dot(hitPos - g_Lighting.view.matViewToWorld[3].xyz, g_Lighting.view.matViewToWorld[2].xyz);
            float nearZ = g_Lighting.view.matClipToView[3].z;
            // View Z
            u_OutputViewSpaceZ[pixelIndex] = viewZ;
            // Device Z
            u_OutputDeviceZ[pixelIndex] = nearZ / viewZ;

            break;
        }

        GeometrySample geometry = getGeometryFromHit(payload.instanceID,
                                                     payload.primitiveIndex,
                                                     payload.geometryIndex,
                                                     payload.barycentrics,
                                                     GeomAttr_All,
                                                     ray.Origin,
                                                     payload.HitT(),
                                                     payload.objectRayDirection,
                                                     payload.IsLss(),
                                                     payload.lssObjectPositionAndRadius0,
                                                     payload.lssObjectPositionAndRadius1,
                                                     t_InstanceData,
                                                     t_GeometryData,
                                                     t_MaterialConstants);
        MaterialSample material = SampleGeometryMaterial(geometry, 0, 0, 0, MatAttr_All, s_MaterialSampler);
        material.emissiveColor = g_Global.enableEmissives ? material.emissiveColor : 0;

        if (geometry.material.domain == MaterialDomain_Transmissive && material.roughness == 0.0f)
        {
            isLensPath = true;
        }

        // Default Hair Material
        RTXCR_HairMaterialData hairMaterialData = createDefaultHairMaterial();
        if (!g_Global.enableHairMaterialOverride)
        {
            hairMaterialData.baseColor = material.hairParams.baseColor;
            hairMaterialData.longitudinalRoughness = material.hairParams.longitudinalRoughness;
            hairMaterialData.azimuthalRoughness = material.hairParams.azimuthalRoughness;
            hairMaterialData.melanin = material.hairParams.melanin;
            hairMaterialData.melaninRedness = material.hairParams.melaninRedness;
            hairMaterialData.ior = material.hairParams.ior;
            hairMaterialData.cuticleAngleInDegrees = material.hairParams.cuticleAngle;
        }

        // Flip normals towards the incident ray direction (needed for backfacing triangles)
        const float3 viewVector = -ray.Direction;

        // Flip the triangle normal, based on positional data, NOT the provided vertex normal
        if (dot(geometry.faceNormal, viewVector) < 0.0f)
        {
            geometry.faceNormal = -geometry.faceNormal;
        }

        // Flip the shading normal, based on texture
        if (dot(material.shadingNormal, geometry.faceNormal) < 0.0f)
        {
            material.shadingNormal = -material.shadingNormal;
        }

        // Better precision than (ray.Origin + ray.Direction * payload.hitDistance)
        const float3 hitPos = mul(geometry.instance.transform, float4(geometry.objectSpacePosition, 1.0f)).xyz;
        float roughness = material.roughness;

        // Use appropriate roughness for hair
        if (isHairMaterial(geometry.material.flags))
        {
            switch (g_Global.hairMode)
            {
            case HairTechSelection::Chiang:
                roughness = hairMaterialData.longitudinalRoughness;
                break;
            case HairTechSelection::Farfield:
                roughness = !g_Global.enableHairMaterialOverride ? hairMaterialData.longitudinalRoughness : g_Global.hairRoughness;
                break;
            }
        }

        float3 diffuseAlbedo = material.diffuseAlbedo;
        if (isHairMaterial(geometry.material.flags) && g_Global.enableHair)
        {
            diffuseAlbedo = material.hairParams.baseColor;
        }

        const float NoV = dot(material.shadingNormal, -ray.Direction);
        writeGBuffer(pixelIndex, hitPos, material.shadingNormal, roughness,
                     g_Global.enableDirectLighting ? material.emissiveColor : 0.0f.rrr,
                     diffuseAlbedo, material.specularF0, NoV);

        if (!isLensPath || material.roughness > 0.0f ||
            !traceIndirect(material, geometry, viewVector, geometry.faceNormal, material.shadingNormal, hitPos, rngState, ray))
        {
            break;
        }
    }
}
