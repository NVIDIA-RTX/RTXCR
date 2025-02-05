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

#include <shared/shared.h>
#include "hair.hlsli"
#include "subsurface.hlsli"

float3 whiteFurnaceTest(const RTXCR_HairMaterialData hairMaterialData,
                        const uint whiteFurnaceSampleCount,
                        const HairTechSelection hairMode,
                        const float3 diffuseReflectionTint,
                        const float diffuseReflectionWeight,
                        const float hairRoughness,
                        const float3 skyColor,
                        inout uint rngState)
{
    const float3 testWo = UniformSampleSphere(float2(Rand(rngState), Rand(rngState)));

    float3 result = float3(0.0f, 0.0f, 0.0f);
    for (uint i = 0; i < whiteFurnaceSampleCount; ++i)
    {
        const float3 testWi = UniformSampleSphere(float2(Rand(rngState), Rand(rngState)));

        RTXCR_HairInteractionSurface hairInteractionSurface;
        hairInteractionSurface.incidentRayDirection = testWo;
        hairInteractionSurface.shadingNormal = float3(0.0f, 0.0f, 1.0f);
        hairInteractionSurface.tangent = float3(1.0f, 0.0f, 0.0f);

        float3 hairBsdf = float3(0.0f, 0.0f, 0.0f);
        switch (hairMode)
        {
            case HairTechSelection::Chiang:
            {
                    RTXCR_HairMaterialInteraction hairMaterialInteraction =
                    RTXCR_CreateHairMaterialInteraction(hairMaterialData, hairInteractionSurface);
                    hairMaterialInteraction.h = -1.0f + Rand(rngState);
                    hairMaterialInteraction.absorptionCoefficient = float3(0.0f, 0.0f, 0.0f);
                    hairBsdf = RTXCR_HairChiangBsdfEval(hairMaterialInteraction, testWi, testWo);
                    break;
            }
            case HairTechSelection::Farfield:
            {
                    RTXCR_HairMaterialInteractionBcsdf hairMaterialInteractionBcsdf =
                    RTXCR_CreateHairMaterialInteractionBcsdf(hairMaterialData, diffuseReflectionTint, diffuseReflectionWeight, hairRoughness);
                    hairMaterialInteractionBcsdf.absorptionCoefficient = float3(0.0f, 0.0f, 0.0f);

                    float3 bsdf = float3(0.0f, 0.0f, 0.0f);
                    float3 bsdfDiffuse = float3(0.0f, 0.0f, 0.0f);
                    float pdf = 0.0f;
                    RTXCR_HairFarFieldBcsdfEval(hairInteractionSurface, hairMaterialInteractionBcsdf, testWi, testWo, bsdf, bsdfDiffuse, pdf);
                    hairBsdf = pdf > 0.0f ? (bsdf + bsdfDiffuse) : 0.0f.rrr;
                    break;
            }
        }
        result += hairBsdf;
    }

    return RTXCR_FOUR_PI * result / float(whiteFurnaceSampleCount).rrr * skyColor.rgb;
}

float3 gBufferDebugColor(const RtxcrDebugOutputType debugOutputMode,
                         const GeometrySample geometry,
                         const MaterialSample material,
                         const RayPayload payload,
                         const RTXCR_HairMaterialData hairMaterialData,
                         const uint whiteFurnaceSampleCount,
                         const HairTechSelection hairMode,
                         const float3 diffuseReflectionTint,
                         const float diffuseReflectionWeight,
                         const float hairRoughness,
                         const float3 skyColor,
                         const float3 shadingNormal,
                         inout int rngState)
{
    float3 debugColor = float3(0.0f, 0.0f, 0.0f);
    switch (debugOutputMode)
    {
        case RtxcrDebugOutputType::WorldSpaceNormals:
        {
            debugColor = geometry.geometryNormal * 0.5f + 0.5f;
            break;
        }
        case RtxcrDebugOutputType::ShadingNormals:
        {
            debugColor = shadingNormal * 0.5f + 0.5f;
            break;
        }
        case RtxcrDebugOutputType::WorldSpaceTangents:
        {
            debugColor = geometry.tangent.xyz * 0.5 + 0.5f;
            break;
        }
        case RtxcrDebugOutputType::WorldSpacePosition:
        {
            debugColor = mul(geometry.instance.transform, float4(geometry.objectSpacePosition, 1.0f)).xyz;
            break;
        }
        case RtxcrDebugOutputType::CurveRadius:
        {
            debugColor = geometry.curveRadius.xxx;
            break;
        }
        case RtxcrDebugOutputType::Barycentrics:
        {
            debugColor = float3(1 - payload.barycentrics.x - payload.barycentrics.y, payload.barycentrics.x, payload.barycentrics.y);
            break;
        }
        case RtxcrDebugOutputType::InstanceID:
        {
            debugColor = HashAndColor(payload.instanceID);
            break;
        }
        case RtxcrDebugOutputType::WhiteFurnace:
        {
            if (isHairMaterial(geometry.material.flags))
            {
                debugColor = whiteFurnaceTest(hairMaterialData,
                                              whiteFurnaceSampleCount,
                                              hairMode,
                                              diffuseReflectionTint,
                                              diffuseReflectionWeight,
                                              hairRoughness,
                                              skyColor,
                                              rngState);
            }
            break;
        }
        default:
            break;
    }
    return debugColor;
}

float3 scaleAndClip(float value, float scale, float minValue, float maxValue)
{
    float finalValue = value * scale;
    if (minValue <= finalValue && finalValue <= maxValue)
        return finalValue.rrr;
    else
        return float3(0.1f, 0.0f, 0.1f);
}

void writeDebugColor(const RtxcrDebugOutputType debugOutputMode,
                     Texture2D<float4> diffuseAlbedo,
                     Texture2D<float4> emissive,
                     Texture2D<float4> motionVectors,
                     Texture2D<float> viewZ,
                     Texture2D<float> deviceZ,
                     RWTexture2D<float4> pathTracerOutput,
                     const float diffuseHitT,
                     const float specularHitT,
                     const float3 outputColor,
                     const uint2 pixel,
                     const float debugScale,
                     const float debugMin,
                     const float debugMax,
                     inout float3 debugColor)
{
    switch (debugOutputMode)
    {
        case RtxcrDebugOutputType::DiffuseReflectance:
        {
            debugColor = diffuseAlbedo[pixel].rgb;
            break;
        }
        case RtxcrDebugOutputType::SpecularReflectance:
        {
            debugColor = t_OutputSpecularAlbedo[pixel].rgb;
            break;
        }
        case RtxcrDebugOutputType::Roughness:
        {
            debugColor = t_OutputNormalRoughness[pixel].a;
            break;
        }
        case RtxcrDebugOutputType::ViewSpaceZ:
        {
            debugColor = scaleAndClip(viewZ[pixel], debugScale, debugMin, debugMax);
            break;
        }
        case RtxcrDebugOutputType::DeviceZ:
        {
            debugColor = scaleAndClip(deviceZ[pixel], debugScale, debugMin, debugMax);
            break;
        }
        case RtxcrDebugOutputType::DiffuseHitT:
        {
            debugColor = scaleAndClip(diffuseHitT, debugScale, debugMin, debugMax);
            break;
        }
        case RtxcrDebugOutputType::SpecularHitT:
        {
            debugColor = scaleAndClip(specularHitT, debugScale, debugMin, debugMax);
            break;
        }
        case RtxcrDebugOutputType::Emissives:
        {
            debugColor = emissive[pixel].rgb;
            break;
        }
        case RtxcrDebugOutputType::BounceHeatmap:
        {
            // TODO
            break;
        }
        case RtxcrDebugOutputType::MotionVector: // 3-D Motion Vector
        {
            debugColor = motionVectors[pixel].xyz;
            break;
        }
        case RtxcrDebugOutputType::PathTracerOutput:
        {
            debugColor = pathTracerOutput[pixel].xyz;
            break;
        }
        case RtxcrDebugOutputType::NaN:
        {
            debugColor = any(isnan(outputColor)) ? float3(1.0f, 0.0f, 0.0f) : float3(1.0f, 1.0f, 1.0f);
            break;
        }
        default:
            break;
    }
}
