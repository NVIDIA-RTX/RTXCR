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

#define USE_DIFFUSE_MEAN_FREE_PATH 1

#include <rtxcr/SubsurfaceScattering.hlsli>
#include <rtxcr/Transmission.hlsli>

#include "subsurfaceMaterial.hlsli"

float3 evalSingleScatteringTransmission(
    const MaterialSample initialSssMaterial,
    const GeometrySample initialSssGeometry,
    const float3 viewVector,
    const float3 hitPos,
    const uint initialInstanceID,
    const uint initialGeometryIndex,
    ByteAddressBuffer initialIndexBuffer,
    ByteAddressBuffer initialVertexBuffer,
    const RTXCR_SubsurfaceMaterialData subsurfaceMaterialData,
    const RTXCR_SubsurfaceInteraction subsurfaceInteraction,
    inout uint rndState)
{
    float3 radiance = float3(0.0f, 0.0f, 0.0f);

    const RTXCR_SubsurfaceMaterialCoefficients sssMaterialCoeffcients = RTXCR_ComputeSubsurfaceMaterialCoefficients(subsurfaceMaterialData);

    for (int bsdfSampleIndex = 0; bsdfSampleIndex < g_Global.sssTransmissionBsdfSampleCount; ++bsdfSampleIndex)
    {
        // Trace rays for diffuse transmittance into the volume
        const float3 refractedRayDirection = RTXCR_CalculateRefractionRay(subsurfaceInteraction, float2(Rand(rndState), Rand(rndState)));
        const float3 hitPos = subsurfaceInteraction.centerPosition;

        float thickness = 0.0f;
        float3 backPosition;
        {
            RayPayload payload = createDefaultRayPayload();
            {
                RayDesc transmissionRay;
                transmissionRay.Origin = OffsetRayOrigin(hitPos, -initialSssGeometry.faceNormal);
                transmissionRay.Direction = refractedRayDirection;
                transmissionRay.TMin = 0.0f;
                transmissionRay.TMax = FLT_MAX;

                const uint rayFlags = RAY_FLAG_FORCE_OPAQUE;
                TraceRay(SceneBVH, rayFlags, 0xFF, 0, 0, 0, transmissionRay, payload);

                thickness = payload.HitT();
                backPosition = transmissionRay.Origin + thickness * transmissionRay.Direction;
            }

            if (payload.Hit())
            {
#if API_DX12 == 1
                GeometrySample geometrySample = getGeometryFromHitFastSss(
                    initialSssGeometry,
                    initialIndexBuffer,
                    initialVertexBuffer,
                    payload.primitiveIndex,
                    payload.barycentrics,
                    GeomAttr_All,
                    hitPos,
                    payload.HitT(),
                    payload.objectRayDirection,
                    false,
                    initialVertexBuffer); // Dummy Buffer
#else
                // TODO: Investigate the reason we need this WAR here for VK
                GeometrySample geometrySample = getGeometryFromHit(
                    payload.instanceID,
                    payload.primitiveIndex,
                    payload.geometryIndex,
                    payload.barycentrics,
                    GeomAttr_All,
                    hitPos,
                    payload.HitT(),
                    payload.objectRayDirection,
                    payload.lssObjectPositionAndRadius0,
                    payload.lssObjectPositionAndRadius1,
                    false,
                    t_InstanceData,
                    t_GeometryData,
                    t_MaterialConstants);
#endif

                const MaterialSample materialSample = SampleGeometryMaterial(geometrySample, 0, 0, 0, MatAttr_All, s_MaterialSampler);
                const float3 sampleGeometryNormal = geometrySample.faceNormal;
                const float3 sampleShadingNormal = materialSample.shadingNormal;

                backPosition = OffsetRayOrigin(backPosition, sampleGeometryNormal);

                LightConstants light;
                float lightWeight;
                uint unused_lightIndex;

                if (sampleLightRIS(rndState, backPosition, light, lightWeight, unused_lightIndex))
                {
                    // Prepare data needed to evaluate the light
                    float3 incidentVector = 0.0f;
                    float lightDistance = 0.0f;
                    float irradiance = 0.0f;
                    const float2 rand2 = float2(Rand(rndState), Rand(rndState));
                    GetLightData(light, backPosition, rand2, g_Global.enableSoftShadows, incidentVector, lightDistance, irradiance);

                    const float3 vectorToLight = normalize(-incidentVector);

                    // Cast shadow ray towards the selected light
                    const float3 lightVisibility = castShadowRay(SceneBVH, backPosition, vectorToLight, lightDistance, g_Global.enableBackFaceCull);

                    if (any(lightVisibility > 0.0f))
                    {
                        const float3 lightRadiance = light.color * irradiance * lightWeight * lightVisibility;
                        const float3 transmissionBsdf = RTXCR_EvaluateBoundaryTerm(initialSssMaterial.shadingNormal,
                                                                                   vectorToLight,
                                                                                   refractedRayDirection,
                                                                                   sampleShadingNormal,
                                                                                   thickness,
                                                                                   sssMaterialCoeffcients);

                        // Li * bsdf * cosTheta / CosineLobePDF = Li * bsdf * cosTheta / (cosTheta / pi) = Li * bsdf * pi
                        radiance += lightRadiance * transmissionBsdf * PI;
                    }
                }
            }
        }

        // Trace rays along the scattering ray
        const float stepSize = thickness / (g_Global.sssTransmissionPerBsdfScatteringSampleCount + 1);
        float accumulatedT = 0.0f;
        float3 scatteringThroughput = float3(0.0f, 0.0f, 0.0f);

        for (int sampleIndex = 0; sampleIndex < g_Global.sssTransmissionPerBsdfScatteringSampleCount; ++sampleIndex)
        {
            // TODO: Important Sampling along the scattering ray direction
            const float currentT = accumulatedT + stepSize;
            accumulatedT = currentT;

            if (currentT >= thickness)
            {
                // TODO: Here should be continue if important sampling
                break;
            }

            const float3 samplePosition = hitPos + currentT * refractedRayDirection;
            const float2 hgRnd = float2(Rand(rndState), Rand(rndState));
            const float3 scatteringDirection = RTXCR_SampleDirectionHenyeyGreenstein(hgRnd, subsurfaceMaterialData.g, refractedRayDirection);
            RayDesc scatteringRay;
            scatteringRay.Origin = samplePosition;
            scatteringRay.Direction = scatteringDirection;
            scatteringRay.TMin = 0.0f;
            scatteringRay.TMax = FLT_MAX;

            RayPayload scatteringPayload = createDefaultRayPayload();
            const uint rayFlags = RAY_FLAG_FORCE_OPAQUE;
            TraceRay(SceneBVH, rayFlags, 0xFF, 0, 0, 0, scatteringRay, scatteringPayload);

            if (scatteringPayload.Hit())
            {
#if API_DX12 == 1
                GeometrySample geometrySample = getGeometryFromHitFastSss(
                    initialSssGeometry,
                    initialIndexBuffer,
                    initialVertexBuffer,
                    scatteringPayload.primitiveIndex,
                    scatteringPayload.barycentrics,
                    GeomAttr_All,
                    scatteringRay.Origin,
                    scatteringPayload.HitT(),
                    scatteringPayload.objectRayDirection,
                    false,
                    initialVertexBuffer); // Dummy Buffer
#else
                GeometrySample geometrySample = getGeometryFromHit(
                    scatteringPayload.instanceID,
                    scatteringPayload.primitiveIndex,
                    scatteringPayload.geometryIndex,
                    scatteringPayload.barycentrics,
                    GeomAttr_All,
                    scatteringRay.Origin,
                    scatteringPayload.HitT(),
                    scatteringPayload.objectRayDirection,
                    scatteringPayload.lssObjectPositionAndRadius0,
                    scatteringPayload.lssObjectPositionAndRadius1,
                    false,
                    t_InstanceData,
                    t_GeometryData,
                    t_MaterialConstants);
#endif
                const MaterialSample materialScatteringSample = SampleGeometryMaterial(geometrySample, 0, 0, 4, MatAttr_All, s_MaterialSampler);
                const float3 scatteringSampleGeometryNormal = geometrySample.faceNormal;

                float3 scatteringBoundaryPosition = samplePosition + scatteringPayload.HitT() * scatteringDirection;
                scatteringBoundaryPosition = OffsetRayOrigin(scatteringBoundaryPosition, scatteringSampleGeometryNormal);

                LightConstants light;
                float lightWeight;
                uint unused_lightIndex;

                if (sampleLightRIS(rndState, scatteringBoundaryPosition, light, lightWeight, unused_lightIndex))
                {
                    // Prepare data needed to evaluate the light
                    float3 incidentVector = 0.0f;
                    float lightDistance = 0.0f;
                    float irradiance = 0.0f;
                    const float2 rand2 = float2(Rand(rndState), Rand(rndState));
                    GetLightData(light, scatteringBoundaryPosition, rand2, g_Global.enableSoftShadows, incidentVector, lightDistance, irradiance);

                    const float3 vectorToLight = normalize(-incidentVector);

                    // Cast shadow ray towards the selected light
                    const float3 lightVisibility = castShadowRay(SceneBVH, backPosition, vectorToLight, lightDistance, g_Global.enableBackFaceCull);

                    if (any(lightVisibility > 0.0f))
                    {
                        const float3 lightRadiance = light.color * irradiance * lightWeight * lightVisibility;
                        const float totalScatteringDistance = currentT + scatteringPayload.HitT();
                        const float3 ssTransmissionBsdf = RTXCR_EvaluateSingleScattering(vectorToLight,
                                                                                         materialScatteringSample.shadingNormal,
                                                                                         totalScatteringDistance,
                                                                                         sssMaterialCoeffcients);

                        scatteringThroughput += lightRadiance * ssTransmissionBsdf * stepSize; // Li * BSDF / PDF
                    }
                }
            }
        }

        radiance += scatteringThroughput / g_Global.sssTransmissionPerBsdfScatteringSampleCount;
    }

    radiance /= g_Global.sssTransmissionBsdfSampleCount;

    return radiance;
}

float3 evaluateSubsurfaceNEE(
    const MaterialSample initialSssMaterial,
    const GeometrySample initialSssGeometry,
    const float3 viewVector,
    const float3 hitPos,
    const uint initialInstanceID,
    const uint initialGeometryIndex,
    const uint2 pixelIndex,
    const float maxRadius,
    ByteAddressBuffer initialIndexBuffer,
    ByteAddressBuffer initialVertexBuffer,
    inout uint rngState)
{
    RTXCR_SubsurfaceMaterialData subsurfaceMaterialData = RTXCR_CreateDefaultSubsurfaceMaterialData();
    subsurfaceMaterialData.transmissionColor = g_Global.sssTransmissionColor;
    subsurfaceMaterialData.scatteringColor = g_Global.sssScatteringColor;
    subsurfaceMaterialData.scale = g_Global.sssScale;
    subsurfaceMaterialData.g = g_Global.sssAnisotropy;

    if (!g_Global.enableSssMaterialOverride)
    {
        subsurfaceMaterialData.transmissionColor = initialSssMaterial.subsurfaceParams.transmissionColor;
        subsurfaceMaterialData.scatteringColor = initialSssMaterial.subsurfaceParams.scatteringColor;
        subsurfaceMaterialData.scale = initialSssMaterial.subsurfaceParams.scale;
        subsurfaceMaterialData.g = initialSssMaterial.subsurfaceParams.anisotropy;
    }

    const float3 geometryNormal = initialSssGeometry.faceNormal;
    const float3 shadingNormal = initialSssMaterial.shadingNormal;

    const float3 tangentWorld = any(dot(initialSssGeometry.tangent.xyz, initialSssGeometry.tangent.xyz) > 1e-5f) ?
            initialSssGeometry.tangent.xyz :
            (dot(geometryNormal, float3(0.0f, 1.0f, 0.0f)) < 0.999f ? cross(geometryNormal, float3(0.0f, 1.0f, 0.0f)) :
                                                                      cross(geometryNormal, float3(1.0f, 0.0f, 0.0f)));
    const float3 biTangentWorld = cross(tangentWorld, geometryNormal);
    RTXCR_SubsurfaceInteraction subsurfaceInteraction =
        RTXCR_CreateSubsurfaceInteraction(hitPos, shadingNormal, tangentWorld, biTangentWorld);

    float3 radiance = float3(0.0f, 0.0f, 0.0f);

    LightConstants light;
    float lightWeight;
    uint unused_lightIndex;

    if (sampleLightRIS(rngState, hitPos, light, lightWeight, unused_lightIndex))
    {
        float3 incidentVector;
        float lightDistance;
        float irradiance;
        const float2 rand2 = float2(Rand(rngState), Rand(rngState));
        GetLightData(light, hitPos, rand2, g_Global.enableSoftShadows, incidentVector, lightDistance, irradiance);

        // [SSS] TODO: There's a gap between center pixel to light and sample position to the light, only same when directional light.
        const float3 vectorToLight = normalize(-incidentVector);
        const float3 lightVector = -incidentVector;

        if (g_Global.enableDiffusionProfile)
        {
            const float3 centerSpecularF0 = (initialSssMaterial.hasMetalRoughParams) ? baseColorToSpecularF0(initialSssMaterial.baseColor, initialSssMaterial.metalness) : initialSssMaterial.specularF0;
            const float3 diffuseAlbedo = (initialSssMaterial.hasMetalRoughParams) ? baseColorToDiffuseReflectance(initialSssMaterial.baseColor, initialSssMaterial.metalness) : initialSssMaterial.diffuseAlbedo;

            subsurfaceMaterialData.transmissionColor = g_Global.useMaterialSpecularAlbedoAsSssTransmission ? centerSpecularF0 : subsurfaceMaterialData.transmissionColor;
            subsurfaceMaterialData.transmissionColor = g_Global.useMaterialDiffuseAlbedoAsSssTransmission ? diffuseAlbedo : subsurfaceMaterialData.transmissionColor;

            const float3 cameraUp = float3(
                g_Lighting.view.matViewToWorld[0][0],
                g_Lighting.view.matViewToWorld[1][0],
                g_Lighting.view.matViewToWorld[2][0]);

            const float3 cameraDirection = float3(
                g_Lighting.view.matViewToWorld[0][2],
                g_Lighting.view.matViewToWorld[1][2],
                g_Lighting.view.matViewToWorld[2][2]);

            if (Rand(rngState) <= 0.5f)
            {
                subsurfaceInteraction.normal = -cameraDirection;
                subsurfaceInteraction.tangent = cameraUp;
                subsurfaceInteraction.biTangent = cross(cameraUp, -cameraDirection);
            }

            uint effectiveSample = 0;

            for (uint sssSampleIndex = 0; sssSampleIndex < g_Global.sssSampleCount; ++sssSampleIndex)
            {
                RTXCR_SubsurfaceSample subsurfaceSample;

                const float2 rand2 = float2(Rand(rngState), Rand(rngState));
                RTXCR_EvalBurleyDiffusionProfile(subsurfaceMaterialData,
                                                 subsurfaceInteraction,
                                                 maxRadius,
                                                 (g_Global.enableSssTransmission && g_Global.enableSingleScatteringDiffusionProfileCorrection),
                                                 rand2,
                                                 subsurfaceSample);

                RayPayload samplePayload = sampleSubsurface(SceneBVH, subsurfaceSample.samplePosition, subsurfaceInteraction.normal, FLT_MAX);

                if (samplePayload.Hit() && samplePayload.instanceID == initialInstanceID && samplePayload.geometryIndex == initialGeometryIndex)
                {
                    GeometrySample geometrySample = getGeometryFromHitFastSss(
                        initialSssGeometry,
                        initialIndexBuffer,
                        initialVertexBuffer,
                        samplePayload.primitiveIndex,
                        samplePayload.barycentrics,
                        GeomAttr_All,
                        subsurfaceSample.samplePosition,
                        samplePayload.HitT(),
                        samplePayload.objectRayDirection,
                        false,
                        initialVertexBuffer); // Dummy Buffer

                    const MaterialSample materialSample = SampleGeometryMaterial(geometrySample, 0, 0, 0, MatAttr_All, s_MaterialSampler);
                    const float3 sampleGeometryNormal = geometrySample.faceNormal;
                    const float3 sampleShadingNormal = materialSample.shadingNormal;
                    const bool transition = dot(vectorToLight, sampleGeometryNormal) < 0.0f;
                    const float3 samplePosition = subsurfaceSample.samplePosition - subsurfaceInteraction.normal * samplePayload.HitT();

                    float3 sampleShadowHitPos = mul(geometrySample.instance.transform, float4(geometrySample.objectSpacePosition, 1.0f)).xyz;
                    sampleShadowHitPos = OffsetRayOrigin(sampleShadowHitPos, transition ? -sampleGeometryNormal : sampleGeometryNormal);

                    LightConstants sampleLight;
                    float sampleLightWeight;
                    uint unused_lightIndex;

                    if (sampleLightRIS(rngState, samplePosition, sampleLight, sampleLightWeight, unused_lightIndex))
                    {
                        // Prepare data needed to evaluate the sample light
                        float3 sampleIncidentVector = float3(0.0f, 0.0f, 0.0f);
                        float sampleLightDistance = 0.0f;
                        float sampleLightIrradiance = 0.0f;
                        const float2 rand2 = float2(Rand(rngState), Rand(rngState));
                        GetLightData(sampleLight, sampleShadowHitPos, rand2, g_Global.enableSoftShadows, sampleIncidentVector, sampleLightDistance, sampleLightIrradiance);

                        const float3 vectorToLight = normalize(-incidentVector);

                        // Cast shadow ray towards the selected light for current SSS sample
                        const float3 sampleLightVisibility = castShadowRay(
                            SceneBVH,
                            sampleShadowHitPos,
                            vectorToLight,
                            sampleLightDistance,
                            g_Global.enableBackFaceCull);

                        if (any(sampleLightVisibility > 0.0f))
                        {
                            const float3 sampleLightRadiance = sampleLight.color * sampleLightIrradiance * sampleLightWeight;
                            const float cosThetaI = min(max(0.00001f, dot(sampleShadingNormal, vectorToLight)), 1.0f);
                            radiance += RTXCR_EvalBssrdf(subsurfaceSample, sampleLightRadiance, cosThetaI);

                            ++effectiveSample;
                        }
                    }
                }
            }

            radiance /= (float) g_Global.sssSampleCount;
        }

        if (g_Global.enableSssTransmission)
        {
            radiance += evalSingleScatteringTransmission(
                initialSssMaterial,
                initialSssGeometry,
                viewVector,
                hitPos,
                initialInstanceID,
                initialGeometryIndex,
                initialIndexBuffer,
                initialVertexBuffer,
                subsurfaceMaterialData,
                subsurfaceInteraction,
                rngState);
        }

        radiance *= g_Global.sssWeight;

        if (g_Global.enableSssMicrofacet)
        {
            const bool transition = dot(vectorToLight, geometryNormal) <= 0.0f;
            const float3 shadowHitPosOffset = OffsetRayOrigin(hitPos, transition ? -geometryNormal : geometryNormal);
            const float3 shadowV = viewVector;
            // Cast shadow ray towards the selected light
            const float3 lightVisibility = castShadowRay(SceneBVH, shadowHitPosOffset, vectorToLight, lightDistance, g_Global.enableBackFaceCull);

            if (any(lightVisibility > 0.0f))
            {
                const float3 lightRadiance = light.color * irradiance * lightWeight * lightVisibility;
                // If light is not in shadow, evaluate BRDF and accumulate its contribution into radiance
                MaterialProperties materialProps = createMaterialProperties(initialSssMaterial);

                if (g_Global.enableSssRoughnessOverride)
                {
                    materialProps.roughness = g_Global.sssRoughnessOverride;
                }

                // This is an entry point for evaluation of all other BRDFs based on selected configuration (for direct light)
                const float3 bsdfSpecular = evalSpecular(shadingNormal, vectorToLight, shadowV, materialProps);
                const float3 bsdf = g_Global.sssSpecularWeight * bsdfSpecular;
                radiance += bsdf * lightRadiance;
            }
        }
    }

    return radiance;
}
