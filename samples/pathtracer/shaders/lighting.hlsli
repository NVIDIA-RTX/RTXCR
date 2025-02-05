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

#define RIS_CANDIDATES_LIGHTS 8 // Number of candidates used for resampling of analytical lights

// Decodes light vector and distance from Light structure based on the light type
void GetLightData(LightConstants light, float3 surfacePos, float2 rand2, bool enableSoftShadows, out float3 incidentVector, out float lightDistance, out float irradiance)
{
    incidentVector = 0.0f;
    lightDistance = 0.0f;
    irradiance = 0.0f;

    if (light.lightType == LightType_Directional)
    {
        if (enableSoftShadows)
        {
            float3 bitangent = normalize(GetPerpendicularVector(light.direction));
            float3 tangent = cross(bitangent, light.direction);

            float angle = rand2.x * 2.0f * M_PI;
            float distance = sqrt(rand2.y);

            incidentVector = light.direction + (bitangent * sin(angle) + tangent * cos(angle)) * tan(light.angularSizeOrInvRange * 0.5f) * distance;
            incidentVector = normalize(incidentVector);
        }
        else
        {
            incidentVector = light.direction;
        }

        lightDistance = FLT_MAX;
        irradiance = light.intensity;
    }
    else if (light.lightType == LightType_Spot || light.lightType == LightType_Point)
    {
        float3 lightToSurface = surfacePos - light.position;
        float distance = sqrt(dot(lightToSurface, lightToSurface));
        float rDistance = 1.0f / distance;
        float3 lightToSurfaceNormalized = lightToSurface * rDistance;

        float attenuation = 1.0f;
        if (light.angularSizeOrInvRange > 0.0f)
        {
            attenuation = square(saturate(1.0f - square(square(distance * light.angularSizeOrInvRange))));

            if (attenuation == 0.0f)
            {
                return;
            }
        }

        float spotlight = 1.0f;
        if (light.lightType == LightType_Spot)
        {
            float LdotD = clamp(dot(-lightToSurfaceNormalized, light.direction), -1.0f, 1.0f);
            float directionAngle = acos(LdotD);
            if (directionAngle > light.outerAngle)
            {
                return;
            }
            else if (directionAngle > light.innerAngle)
            {
                spotlight = 1.0f - smoothstep(light.innerAngle, light.outerAngle, directionAngle);
            }
        }

        if (light.radius > 0.0f)
        {
            float halfAngularSize = atan(min(light.radius * rDistance, 1.0f));

            // A good enough approximation for 2 * (1 - cos(halfAngularSize)), numerically more accurate for small angular sizes
            float solidAngleOverPi = square(halfAngularSize);
            float radianceTimesPi = light.intensity / square(light.radius);

            irradiance = radianceTimesPi * solidAngleOverPi;
        }
        else
        {
            irradiance = light.intensity * square(rDistance);
        }

        incidentVector = lightToSurfaceNormalized;
        lightDistance = distance;
        irradiance *= spotlight * attenuation;
    }
}

// Samples a random light from the pool of all lights using RIS (Resampled Importance Sampling)
bool sampleLightRIS(inout uint rngState, float3 hitPosition, out LightConstants selectedSample, out float lightSampleWeight, out int lightIndex)
{
    selectedSample = (LightConstants)0;
    lightSampleWeight = 0.0f;
    lightIndex = -1;

    if (g_Lighting.lightCount == 0)
    {
        return false;
    }

    if (g_Lighting.lightCount == 1)
    {
        selectedSample = g_Lighting.lights[0];
        lightSampleWeight = 1.0f;
        lightIndex = 0;
        return true;
    }

    float totalWeights = 0.0f;
    float samplePdfG = 0.0f;

    const uint candidateMax = min(g_Lighting.lightCount, RIS_CANDIDATES_LIGHTS);
    for (int i = 0; i < candidateMax; i++)
    {
        uint randomLightIndex = g_Global.targetLight >= 0 ? g_Global.targetLight : min(g_Lighting.lightCount - 1, uint(Rand(rngState) * g_Lighting.lightCount));
        LightConstants candidate = g_Lighting.lights[randomLightIndex];
        float2 rand2 = float2(Rand(rngState), Rand(rngState));

        float3 lightVector;
        float lightDistance;
        float irradiance;
        GetLightData(candidate, hitPosition, rand2, g_Global.enableSoftShadows, lightVector, lightDistance, irradiance);

        // PDF of uniform distribution is (1 / light count). Reciprocal of that PDF (simply a light count) is a weight of this sample
        float candidateWeight = float(g_Lighting.lightCount);
        float candidatePdfG = irradiance;
        float candidateRISWeight = candidatePdfG * candidateWeight;

        if (candidateRISWeight > 0.0f)
        {
            totalWeights += candidateRISWeight;

            if (Rand(rngState) < (candidateRISWeight / totalWeights))
            {
                selectedSample = candidate;
                samplePdfG = candidatePdfG;
                lightIndex = randomLightIndex;
            }
        }
    }

    if (totalWeights == 0.0f)
    {
        return false;
    }
    else
    {
        lightSampleWeight = (totalWeights / float(candidateMax)) / samplePdfG;
        return true;
    }
}
