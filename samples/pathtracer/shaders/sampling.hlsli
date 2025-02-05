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
 
#include <donut/shaders/material_cb.h>

// Calculates probability of selecting BRDF (specular or diffuse) using the approximate Fresnel term
float getSpecularBrdfProbability(MaterialSample material, float3 viewVector, float3 shadingNormal)
{
    // Evaluate Fresnel term using the shading normal
    // Note: we use the shading normal instead of the microfacet normal (half-vector) for Fresnel term here. That's suboptimal for rough surfaces at grazing angles, but half-vector is yet unknown at this point
    float specularF0 = luminance(material.specularF0);
    float diffuseReflectance = luminance(material.diffuseAlbedo);

    float fresnel = saturate(luminance(evalFresnel(specularF0, shadowedF90(specularF0), max(0.0f, dot(viewVector, shadingNormal)))));

    // Approximate relative contribution of BRDFs using the Fresnel term
    float specular = fresnel;
    float diffuse = diffuseReflectance * (1.0f - fresnel); //< If diffuse term is weighted by Fresnel, apply it here as well

    // Return probability of selecting specular BRDF over diffuse BRDF
    float probability = (specular / max(0.0001f, (specular + diffuse)));

    // Clamp probability to avoid undersampling of less prominent BRDF
    return clamp(probability, 0.1f, 0.9f);
}

int calculateLobeSample(const MaterialSample material,
                        const float3         viewVector,
                        const float3         shadingNormal,
                        const bool           enableTransmission,
                        const bool           enableDiffuse,
                        inout uint           rngState,
                        out float            lobePdf)
{
    int lobe = DIFFUSE_TYPE;

    // Fast path for mirrors
    if (material.metalness == 1.0f && material.roughness == 0.0f)
    {
        lobe = SPECULAR_TYPE;
        lobePdf = 1.0f;
    }
    else
    {
        const float specularBrdfProbability = getSpecularBrdfProbability(material, viewVector, shadingNormal);

        const float random = Rand(rngState);
        if (random < specularBrdfProbability)
        {
            lobe = SPECULAR_TYPE;
            lobePdf = specularBrdfProbability;
        }
        else if (!enableDiffuse)
        {
            // Early out when diffuse lobe is disabled
            lobe = TRANSMISSIVE_TYPE;
            lobePdf = 1.0f - specularBrdfProbability;
        }
        else if (enableTransmission)
        {
            const float transmissiveProbability = (1.0f - specularBrdfProbability) * material.transmission;

            if (random < material.transmission)
            {
                lobe = TRANSMISSIVE_TYPE;
                lobePdf = transmissiveProbability;
            }
            else
            {
                lobe = DIFFUSE_TYPE;
                lobePdf = 1.0f - specularBrdfProbability - transmissiveProbability;
            }
        }
        else
        {
            lobe = DIFFUSE_TYPE;
            lobePdf = 1.0f - specularBrdfProbability;
        }
    }

    return lobe;
}
