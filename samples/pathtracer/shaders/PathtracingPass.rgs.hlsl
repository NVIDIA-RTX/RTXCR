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

#include <donut/shaders/sky.hlsli>

#include <rtxcr/utils/RtxcrMath.hlsli>

#include "bindings.hlsli"
#include "ray.hlsli"
#include "geometry.hlsli"
#include "material.hlsli"
#include "lighting.hlsli"
#include "sampling.hlsli"
#include "debug.hlsli"

#define RUSSIAN_ROULETTE_BOUNCES_MIN    3

// 1~3 are other material lobes defined in bsdf.hlsli
#define HAIR_TYPE                       4

float3 calculateSkyValue(const float3 direction, const bool isProcedural)
{
    if (isProcedural)
    {
        return ProceduralSky(g_Global.skyParams, direction, 0.0f);
    }
    else
    {
        float elevation = asin(direction.y);
        float azimuth = 0.0f;
        if (abs(direction.y) < 1.0f)
        {
            azimuth = atan2(direction.z, direction.x);
        }

        float2 uv;
        uv.x = azimuth / (2.0f * RTXCR_PI) - 0.25f;
        uv.y = 0.5f - elevation / RTXCR_PI;

        return t_EnvironmentMap.SampleLevel(s_MaterialSampler, uv, 0).rgb;
    }
}

void resolveMiss(const RayDesc ray,
                 const uint2   pixelIndex,
                 const uint    bounce,
                 const float3  throughput,
                 inout float3  directRadiance,
                 inout float3  indirectRadiance,
                 inout float3  debugColor)
{
    float3 skyValue = 0.0f;
    if (g_Global.debugOutputMode != RtxcrDebugOutputType::WhiteFurnace)
    {
        skyValue = calculateSkyValue(ray.Direction, g_Global.skyParams.angularSizeOfLight >= 0.0f);
    }
    else
    {
        skyValue = g_Lighting.skyColor.rgb;
    }

    if (bounce == 0)
    {
        directRadiance += skyValue * throughput;
        u_Output[pixelIndex] = float4(g_Global.enableDirectLighting ? directRadiance : 0.0f.rrr, 1.0f);
    }
    else
    {
        indirectRadiance += skyValue * throughput * g_Global.environmentLightIntensity;
    }

    if (g_Global.debugOutputMode == RtxcrDebugOutputType::Emissives)
    {
        debugColor = 2.0f * (float)bounce / (float)g_Global.bouncesMax;
    }
    else if (g_Global.debugOutputMode == RtxcrDebugOutputType::WhiteFurnace)
    {
        if (bounce == 0)
        {
            debugColor = skyValue;
        }
    }
}

struct AccumulatedSampleData
{
    float3 radiance;
    float hitDistance;
    uint diffuseSampleNum;
    float3 specularRadiance;
    float specularHitDistance;
};

void accumulateSample(inout AccumulatedSampleData accumulatedSampleData,
                      const float3 sampleRadiance,
                      const bool isDiffusePath,
                      const bool isSssPath,
                      const float hitDistance)
{
    if (isDiffusePath)
    {
        accumulatedSampleData.radiance += sampleRadiance;
        accumulatedSampleData.hitDistance = hitDistance;

        ++accumulatedSampleData.diffuseSampleNum;

        if (isSssPath)
        {
            accumulatedSampleData.specularHitDistance = hitDistance;
        }
    }
    else // Specular
    {
        accumulatedSampleData.specularRadiance += sampleRadiance;
        accumulatedSampleData.specularHitDistance = hitDistance;
    }
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

float3 evalulateNEE(const MaterialSample material,
                    const RTXCR_HairMaterialData hairMaterialData,
                    const GeometrySample geometry,
                    const float3 viewVector,
                    const float3 hitPos,
                    inout uint rngState)
{
    float3 radiance = float3(0.0f, 0.0f, 0.0f);

    const float3 faceNormal = geometry.faceNormal;
    const float3 shadingNormal = material.shadingNormal;

    LightConstants light;
    float lightWeight;
    uint lightIndex;

    if (sampleLightRIS(rngState, hitPos, light, lightWeight, lightIndex))
    {
        const float3 shadowV = viewVector;

        // Prepare data needed to evaluate the light
        float3 incidentVector = 0.0f;
        float lightDistance = 0.0f;
        float irradiance = 0.0f;
        const float2 rand2 = float2(Rand(rngState), Rand(rngState));
        GetLightData(light, hitPos, rand2, g_Global.enableSoftShadows, incidentVector, lightDistance, irradiance);

        const float3 vectorToLight = normalize(-incidentVector);
        const float3 lightVector = -incidentVector;

        const bool transition = dot(vectorToLight, faceNormal) <= 0.0f;
        const float3 offsetNormal = transition ? -faceNormal : faceNormal;
        const float extraOffsetDistance = GetRayOriginOffsetDistance(geometry, transition);
        const float3 hitPosAdjusted = OffsetRayOrigin(hitPos, offsetNormal, extraOffsetDistance);

        // Cast shadow ray towards the selected light
        const float3 lightVisibility = castShadowRay(SceneBVH, hitPosAdjusted, vectorToLight, lightDistance, g_Global.enableBackFaceCull);
        if (any(lightVisibility > 0.0f))
        {
            const float3 lightRadiance = light.color * irradiance * lightWeight * lightVisibility;
            if (!isHairMaterial(geometry.material.flags) || !g_Global.enableHair)
            {
                // If light is not in shadow, evaluate BRDF and accumulate its contribution into radiance
                MaterialProperties materialProps = createMaterialProperties(material);

                // This is an entry point for evaluation of all other BRDFs based on selected configuration (for direct light)
                float3 bsdf = 0.0f;
                if (!isEyesCorneaMaterial(geometry.material))
                {
                    bsdf = !g_Global.forceLambertianBRDF && all(geometry.material.normalTextureTransformScale == 1.0f) ?
                        evalCombinedBRDF(shadingNormal, vectorToLight, shadowV, materialProps) :
                        evalLambertianBRDF(shadingNormal, vectorToLight, shadowV, materialProps);
                }
                else
                {
                    // Only specular and transmission lobes are enabled for cornea
                    bsdf = evalSpecular(shadingNormal, vectorToLight, shadowV, materialProps);
                }

                radiance = bsdf * lightRadiance;
            }
            else // Hair
            {
                const float3 tangentWorld = any(dot(geometry.tangent.xyz, geometry.tangent.xyz) > 1e-5f) ?
                    geometry.tangent.xyz : cross(shadingNormal, float3(0.0f, 1.0f, 0.0f));
                const float3 biTangentWorld = cross(shadingNormal, tangentWorld);

                const float3x3 hairTangentBasis = float3x3(tangentWorld, biTangentWorld, shadingNormal); // TBN

                const float3 viewVectorLocal = mul(hairTangentBasis, viewVector);
                const float3 lightVectorLocal = mul(hairTangentBasis, vectorToLight);

                RTXCR_HairInteractionSurface hairInteractionSurface;
                hairInteractionSurface.incidentRayDirection = viewVectorLocal;
                hairInteractionSurface.shadingNormal = float3(0.0f, 0.0f, 1.0f);
                hairInteractionSurface.tangent = float3(1.0f, 0.0f, 0.0f);

                float3 hairBsdf = float3(0.0f, 0.0f, 0.0f);
                switch (g_Global.hairMode)
                {
                    case HairTechSelection::Chiang:
                    {
                        RTXCR_HairMaterialInteraction hairMaterialInteraction = RTXCR_CreateHairMaterialInteraction(hairMaterialData, hairInteractionSurface);
                        hairBsdf = RTXCR_HairChiangBsdfEval(hairMaterialInteraction, lightVectorLocal, viewVectorLocal);
                        break;
                    }
                    case HairTechSelection::Farfield:
                    {
                        RTXCR_HairMaterialInteractionBcsdf hairMaterialInteractionBcsdf;
                        if (!g_Global.enableHairMaterialOverride)
                        {
                            hairMaterialInteractionBcsdf = RTXCR_CreateHairMaterialInteractionBcsdf(hairMaterialData, material.hairParams.diffuseReflectionTint, material.hairParams.diffuseReflectionWeight, hairMaterialData.longitudinalRoughness);
                        }
                        else
                        {
                           hairMaterialInteractionBcsdf = RTXCR_CreateHairMaterialInteractionBcsdf(hairMaterialData, g_Global.diffuseReflectionTint, g_Global.diffuseReflectionWeight, g_Global.hairRoughness);
                        }

                        float3 bsdfSpecular = float3(0.0f, 0.0f, 0.0f);
                        float3 bsdfDiffuse = float3(0.0f, 0.0f, 0.0f);
                        float pdf = 0.0f;
                        RTXCR_HairFarFieldBcsdfEval(hairInteractionSurface,
                                                    hairMaterialInteractionBcsdf,
                                                    lightVectorLocal, viewVectorLocal,
                                                    bsdfSpecular, bsdfDiffuse, pdf);
                        const float3 bsdf = bsdfSpecular + bsdfDiffuse;

                        hairBsdf = pdf > 0.0f ? bsdf : 0.0f.rrr;

                        break;
                    }
                }
                radiance = hairBsdf * lightRadiance;
            }
        }
    }

    return radiance;
}

bool indirectIntegrator(const MaterialSample material,
                        const RTXCR_HairMaterialData hairMaterialData,
                        const GeometrySample geometry,
                        const float3 viewVector,
                        const float3 faceNormal,
                        const float3 shadingNormal,
                        const float3 hitPos,
                        const uint bounce,
                        inout uint rngState,
                        inout bool isDiffusePath,
                        inout bool internalRay,
                        inout float3 throughput,
                        inout RayDesc ray)
{
    // Sample BSDF to generate the next ray
    // Figure out whether to sample diffuse, specular or transmission BSDF
    int bsdfType = DIFFUSE_TYPE;
    if ((!isHairMaterial(geometry.material.flags) || !g_Global.enableHair))
    {
        float lobePdf = 0.0f;
        const bool enableDiffuse = (!isEyesCorneaMaterial(geometry.material) || !g_Global.enableTransmission);
        bsdfType = calculateLobeSample(material, viewVector, shadingNormal, g_Global.enableTransmission, enableDiffuse, rngState, lobePdf);
        throughput /= max(lobePdf, 1e-7f);

        if (bounce == 0)
        {
            isDiffusePath = (bsdfType == DIFFUSE_TYPE || bsdfType == TRANSMISSIVE_TYPE);
        }
    }
    else
    {
        bsdfType = HAIR_TYPE;
        isDiffusePath = false;
    }

    // Run importance sampling of selected BRDF to generate reflecting ray direction
    float3 bsdfWeight = float3(0.0f, 0.0f, 0.0f);
    float bsdfPdf = 0.0f;
    float refractiveIndex = 1.0f;

    bool continueTrace = false;
    if (!isHairMaterial(geometry.material.flags) || !g_Global.enableHair)
    {
        // Generates a new ray direction
        const MaterialProperties materialProps = createMaterialProperties(material);
        const float2 rand2 = float2(Rand(rngState), Rand(rngState));
        continueTrace = evalIndirectCombinedBRDF(rand2, shadingNormal, faceNormal, viewVector, materialProps, bsdfType, refractiveIndex, ray.Direction, bsdfWeight, bsdfPdf);
    }
    else // Hair
    {
        const float3 tangentWorld = geometry.tangent.xyz;
        const float3 biTangentWorld = cross(shadingNormal, tangentWorld);
        const float3x3 hairTangentBasis = float3x3(tangentWorld, biTangentWorld, shadingNormal); // TBN

        const float3 viewVectorLocal = mul(hairTangentBasis, viewVector);
        RTXCR_HairInteractionSurface hairInteractionSurface = (RTXCR_HairInteractionSurface)0;
        hairInteractionSurface.incidentRayDirection = viewVectorLocal;
        hairInteractionSurface.shadingNormal = float3(0.0f, 0.0f, 1.0f);
        hairInteractionSurface.tangent = float3(1.0f, 0.0f, 0.0f);

        float2 rand2[2] = { float2(Rand(rngState), Rand(rngState)), float2(Rand(rngState), Rand(rngState)) };
        float3 sampleDirection = float3(0.0f, 0.0f, 0.0f);
        switch (g_Global.hairMode)
        {
            case HairTechSelection::Chiang:
            {
                RTXCR_HairLobeType lobeType;
                RTXCR_HairMaterialInteraction hairMaterialInteraction = RTXCR_CreateHairMaterialInteraction(hairMaterialData, hairInteractionSurface);
                continueTrace = RTXCR_SampleChiangBsdf(hairMaterialInteraction, viewVectorLocal, rand2, sampleDirection, bsdfPdf, bsdfWeight, lobeType);
                break;
            }
            case HairTechSelection::Farfield:
            {
                RTXCR_HairMaterialInteractionBcsdf hairMaterialInteractionBcsdf;
                if (!g_Global.enableHairMaterialOverride)
                {
                    hairMaterialInteractionBcsdf = RTXCR_CreateHairMaterialInteractionBcsdf(hairMaterialData, material.hairParams.diffuseReflectionTint, material.hairParams.diffuseReflectionWeight, hairMaterialData.longitudinalRoughness);
                }
                else
                {
                    hairMaterialInteractionBcsdf = RTXCR_CreateHairMaterialInteractionBcsdf(hairMaterialData, g_Global.diffuseReflectionTint, g_Global.diffuseReflectionWeight, g_Global.hairRoughness);
                }

                const float h = 2.0f * Rand(rngState) - 1.0f;
                const float lobeRandom = Rand(rngState);
                float3 bsdfSpecular = float3(0.0f, 0.0f, 0.0f);
                float3 bsdfDiffuse = float3(0.0f, 0.0f, 0.0f);
                continueTrace = RTXCR_SampleFarFieldBcsdf(hairInteractionSurface, hairMaterialInteractionBcsdf, viewVectorLocal, h, lobeRandom, rand2, sampleDirection, bsdfSpecular, bsdfDiffuse, bsdfPdf);
                bsdfWeight = bsdfSpecular + bsdfDiffuse;
                break;
            }
        }

        if (continueTrace)
        {
            bsdfWeight /= bsdfPdf;
            ray.Direction = mul(transpose(hairTangentBasis), sampleDirection);
        }
    }

    if (!continueTrace)
    {
        return false; // Current path is eliminated by the surface
    }

    // Refraction requires the ray offset to go in the opposite direction
    const bool transition = dot(faceNormal, ray.Direction) <= 0.0f;
    const float3 offsetNormal = transition ? -faceNormal : faceNormal;
    const float extraOffsetDistance = GetRayOriginOffsetDistance(geometry, transition);
    ray.Origin = OffsetRayOrigin(hitPos, offsetNormal, extraOffsetDistance);

    // If we are internal, assume we will be leaving the object on a transition and air has an ior of ~1.0
    if (internalRay)
    {
        refractiveIndex = 1.0f / refractiveIndex;
    }

    if (g_Global.enableBackFaceCull &&
        transition &&
        (!isHairMaterial(geometry.material.flags) || !g_Global.enableHair))
    {
        internalRay = !internalRay;
    }

    if (g_Global.enableOcclusion)
    {
        throughput *= material.occlusion;
    }

    // Resolve the path space BSDF integration
    throughput *= bsdfWeight;

    // Early out when the contribution of current path segment is very low, the bias caused by this is subtle
    if (luminance(throughput) < g_Global.throughputThreshold)
    {
        return false;
    }

    return true;
}

[shader("raygeneration")]
void RayGen()
{
    const uint2 pixelIndex = DispatchRaysIndex().xy;
    const float2 pixel = float2(pixelIndex + 0.5f) - g_Global.jitterOffset;
    const uint2 launchDimensions = DispatchRaysDimensions().xy;
    uint rngState = InitRNG(pixelIndex, launchDimensions, g_Global.frameIndex);

    AccumulatedSampleData accumulatedSampleData = (AccumulatedSampleData)0;
    float3 debugColor = float3(0.0f, 0.0f, 0.0f);

    bool isSssPath = false;
    for (uint sampleIndex = 0; sampleIndex < g_Global.samplesPerPixel; sampleIndex++)
    {
        RayDesc ray = generatePinholeCameraRay(g_Lighting.view, pixel / (float2)launchDimensions);
        float3 directRadiance = float3(0.0f, 0.0f, 0.0f);
        float3 indirectRadiance = float3(0.0f, 0.0f, 0.0f);
        float3 throughput = float3(1.0f, 1.0f, 1.0f);

        bool internalRay = false;
        bool isEyePath = false;
        bool isLensPath = false;

        // Denoiser Vars
        bool isDiffusePath = true;
        float pathHitDistance = 0.0f;

        for (uint bounce = 0; bounce < g_Global.bouncesMax; bounce++)
        {
            RayPayload payload = createDefaultRayPayload();
            const uint rayFlags = (!g_Global.enableBackFaceCull || internalRay) ? RAY_FLAG_NONE : RAY_FLAG_CULL_BACK_FACING_TRIANGLES;
            const uint InstanceInclusionMask = (bounce != 0) ? (!isSssPath ? 0xFF : 0x3) : 0x5;
            TraceRay(SceneBVH, rayFlags, InstanceInclusionMask, 0, 0, 0, ray, payload);

            if (!payload.Hit())
            {
                resolveMiss(ray, pixelIndex, bounce, throughput, directRadiance, indirectRadiance, debugColor);

                pathHitDistance = (bounce == 0) ? TRACING_FAR_DISTANCE : pathHitDistance;

                break;
            }

            pathHitDistance += payload.hitDistance;

            GeometrySample geometry = getGeometryFromHit(payload.instanceID,
                                                         payload.primitiveIndex,
                                                         payload.geometryIndex,
                                                         payload.barycentrics,
                                                         GeomAttr_All,
                                                         ray.Origin,
                                                         payload.HitT(),
                                                         payload.objectRayDirection,
                                                         payload.lssObjectPositionAndRadius0,
                                                         payload.lssObjectPositionAndRadius1,
                                                         false, // We don't calculate motion vector in PT pass
                                                         t_InstanceData,
                                                         t_GeometryData,
                                                         t_MaterialConstants);
            MaterialSample material = SampleGeometryMaterial(geometry, 0, 0, 0, MatAttr_All, s_MaterialSampler);
            material.emissiveColor = g_Global.enableEmissives ? material.emissiveColor : 0;

            isEyePath = isEyesCorneaMaterial(geometry.material) ? true : isEyePath;
            isLensPath = (geometry.material.domain == MaterialDomain_Transmissive && material.roughness == 0.0f) ? true : isLensPath;

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
            geometry.faceNormal *= dot(geometry.faceNormal, viewVector) < 0.0f ? -1.0f : 1.0f;

            // Flip the shading normal, based on texture
            material.shadingNormal *= dot(material.shadingNormal, geometry.faceNormal) < 0.0f ? -1.0f : 1.0f;

            // Better precision than (ray.Origin + ray.Direction * payload.hitDistance)
            const float3 hitPos = mul(geometry.instance.transform, float4(geometry.objectSpacePosition, 1.0f)).xyz;

            if (g_Global.enableLighting)
            {
                const bool isSssMat = isSubsurfaceMaterial(geometry.material.flags) && g_Global.enableSss;
                const bool isHairMat = isHairMaterial(geometry.material.flags) && g_Global.enableHair;

                float3 radiance = float3(0.0f, 0.0f, 0.0f);
                if (!isSssPath)
                {
                    if (!isSssMat || isHairMat || isEyesCorneaMaterial(geometry.material) || geometry.instance.IsCurveLSS())
                    {
                        radiance = evalulateNEE(material, hairMaterialData, geometry, viewVector, hitPos, rngState);
                    }
                    else
                    {
                        const float maxRadius = !isEyePath ? g_Global.maxSampleRadius : (geometry.material.roughness >= 1.0f ? 1e-2f : 0.4f);

                        ByteAddressBuffer indexBuffer = t_BindlessBuffers[NonUniformResourceIndex(geometry.geometry.indexBufferIndex)];
                        ByteAddressBuffer vertexBuffer = t_BindlessBuffers[NonUniformResourceIndex(geometry.geometry.vertexBufferIndex)];

                        radiance = evaluateSubsurfaceNEE(
                            material,
                            geometry,
                            viewVector,
                            hitPos,
                            payload.instanceID,
                            payload.geometryIndex,
                            pixelIndex,
                            maxRadius,
                            indexBuffer,
                            vertexBuffer,
                            rngState);

                        // Only do SSS sampling once for every path
                        isSssPath = true;
                    }
                }

                directRadiance += (bounce == 0) ? (material.emissiveColor + radiance) : 0.0f;
                indirectRadiance += (bounce > 0) ? (material.emissiveColor + radiance) * throughput : 0.0f;
            }

            if (bounce == 0 && sampleIndex == 0)
            {
                debugColor = gBufferDebugColor(g_Global.debugOutputMode, geometry, material, payload,
                                               hairMaterialData,
                                               g_Global.whiteFurnaceSampleCount,
                                               g_Global.hairMode,
                                               g_Global.diffuseReflectionTint,
                                               g_Global.diffuseReflectionWeight,
                                               !g_Global.enableHairMaterialOverride ? hairMaterialData.longitudinalRoughness : g_Global.hairRoughness,
                                               g_Lighting.skyColor.rgb,
                                               material.shadingNormal,
                                               rngState);
            }

            // Terminate loop early on last bounce (we don't need to sample BRDF)
            if (bounce == g_Global.bouncesMax - 1 || (isSssPath && !isEyePath && (g_Global.enableSssIndirect == false || bounce > 2)))
            {
                break;
            }

            // Russian roulette
            if (g_Global.enableRussianRoulette)
            {
                if (bounce > RUSSIAN_ROULETTE_BOUNCES_MIN)
                {
                    float rrProbability = min(0.95f, luminance(throughput));
                    const bool terminate = (rrProbability < Rand(rngState));
                    if (terminate)
                    {
                        break;
                    }
                    else
                    {
                        throughput /= rrProbability;
                    }
                }
            }

            bool continueTrace = false;
            bool isDiffusePathLocal = true;
            continueTrace = indirectIntegrator(material, hairMaterialData, geometry,
                                               viewVector, geometry.faceNormal, material.shadingNormal, hitPos, bounce,
                                               rngState, isDiffusePathLocal, internalRay, throughput, ray);
            if (bounce == 0)
            {
                isDiffusePath = isDiffusePathLocal || isSssPath;
            }

            if (!continueTrace)
            {
                break;
            }
        }

        float3 exitantRadiance = float3(0.0f, 0.0f, 0.0f);
        if (g_Global.enableDirectLighting)
        {
            exitantRadiance += directRadiance;
        }
        if (g_Global.enableIndirectLighting)
        {
            exitantRadiance += indirectRadiance;
        }

        if (g_Global.enableDenoiser)
        {
            accumulateSample(accumulatedSampleData, exitantRadiance, isDiffusePath, isSssPath, pathHitDistance);
        }
        else
        {
            accumulatedSampleData.radiance += exitantRadiance;

            if (g_Global.debugOutputMode != RtxcrDebugOutputType::None)
            {
                if (isDiffusePath)
                {
                    accumulatedSampleData.hitDistance = pathHitDistance;
                    if (isSssPath)
                    {
                        accumulatedSampleData.specularHitDistance = pathHitDistance;
                    }
                }
                else
                {
                    accumulatedSampleData.specularHitDistance = pathHitDistance;
                }
            }
        }
    }

    if (g_Global.enableDenoiser)
    {
        // Specular
        const uint specularSampleNum = g_Global.samplesPerPixel - accumulatedSampleData.diffuseSampleNum;
        if (specularSampleNum > 0)
        {
            accumulatedSampleData.specularRadiance *= (1.0f / (float)specularSampleNum);
        }
        else
        {
            if (!isSssPath)
            {
                accumulatedSampleData.specularRadiance = 0;
                accumulatedSampleData.specularHitDistance = 0;
            }
        }
        u_OutputSpecularRadianceHitDistance[pixelIndex] = float4(accumulatedSampleData.specularRadiance, accumulatedSampleData.specularHitDistance);
        u_OutputSpecularHitDistance[pixelIndex] = accumulatedSampleData.specularHitDistance;

        // Diffuse
        const uint diffuseSampleNum = accumulatedSampleData.diffuseSampleNum;
        if (diffuseSampleNum > 0)
        {
            accumulatedSampleData.radiance *= (1.0f / diffuseSampleNum);
        }
        else
        {
            accumulatedSampleData.radiance = 0;
            accumulatedSampleData.hitDistance = 0;
        }

        u_OutputDiffuseRadianceHitDistance[pixelIndex] = float4(accumulatedSampleData.radiance, accumulatedSampleData.hitDistance);

        accumulatedSampleData.radiance *= (1.0f / g_Global.samplesPerPixel);
        u_Output[pixelIndex] = float4(accumulatedSampleData.radiance + accumulatedSampleData.specularRadiance, 1.0f);
    }
    else
    {
        // Write radiance to output buffer
        accumulatedSampleData.radiance *= (1.0f / g_Global.samplesPerPixel);
        u_Output[pixelIndex] = float4(accumulatedSampleData.radiance, 1.0f);
    }

    // Debugging
    if (g_Global.debugOutputMode != RtxcrDebugOutputType::None)
    {
        writeDebugColor(g_Global.debugOutputMode,
                        t_OutputDiffuseAlbedo,
                        t_OutputEmissive,
                        t_OutputMotionVectors,
                        t_OutputViewSpaceZ,
                        t_OutputDeviceZ,
                        u_Output,
                        accumulatedSampleData.hitDistance,
                        accumulatedSampleData.specularHitDistance,
                        accumulatedSampleData.radiance,
                        pixelIndex,
                        g_Global.debugScale,
                        g_Global.debugMin,
                        g_Global.debugMax,
                        debugColor);

        u_Output[pixelIndex] = float4(debugColor, 1.0f);
    }
}
