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

#include "shared.h"

#include <donut/shaders/sky_cb.h>

struct GlobalConstants
{
    int enableBackFaceCull;
    int bouncesMax;
    int frameIndex;
    int samplesPerPixel;

    float2 jitterOffset;
    RtxcrDebugOutputType debugOutputMode;
    uint pad0;

    uint enableAccumulation;
    float recipAccumulatedFrames;
    int accumulatedFramesMax;
    bool enableDenoiser;

    uint enableEmissives;
    uint enableLighting;
    uint enableDirectLighting;
    uint enableIndirectLighting;

    uint enableTransparentShadows;
    uint enableSoftShadows;
    uint enableRussianRoulette;
    uint enableHair;

    uint enableSss;
    uint enableSssIndirect;
    int targetLight;
    float environmentLightIntensity;

    float throughputThreshold;
    float exposureScale;
    uint toneMappingOperator;
    uint clamp;

    // Chiang Hair Override
    HairTechSelection hairMode;
    float3 hairBaseColor;

    float longitudinalRoughness;
    float azimuthalRoughness;
    float hairIor;
    float cuticleAngleInDegrees;

    // OV Hair Override
    uint  absorptionModel;
    float melanin;
    float melaninRedness;
    float hairRoughness;

    float3 diffuseReflectionTint;
    float diffuseReflectionWeight;

    uint analyticalFresnel;
    int enableTransmission;
    int enableOcclusion;
    uint whiteFurnaceSampleCount;

    // SSS
    uint enableSssTransmission;
    uint enableSssMaterialOverride;
    uint enableDiffusionProfile;
    uint enableSssDebug;

    uint sssSampleCount;
    float sssScale;
    float maxSampleRadius;
    float sssAnisotropy;

    float3 sssScatteringColor;
    float sssWeight;

    // SSS - Transmission
    uint sssTransmissionPerBsdfScatteringSampleCount;
    uint sssTransmissionBsdfSampleCount;
    uint useMaterialDiffuseAlbedoAsSssTransmission;
    uint useMaterialSpecularAlbedoAsSssTransmission;

    float3 sssTransmissionColor;
    uint enableSingleScatteringDiffusionProfileCorrection;

    // SSS - Specular
    uint enableSssMicrofacet;
    float sssSpecularWeight;
    uint enableSssRoughnessOverride;
    uint sssRoughnessOverride;

    // SSS - Debug
    uint2 sssDebugCoordinate;
    bool forceLambertianBRDF;
    uint enableHairMaterialOverride;

    // Sky
    ProceduralSkyShaderParameters skyParams;

    uint enableDlssRR;
    float debugScale;
    float debugMin;
    float debugMax;
};
