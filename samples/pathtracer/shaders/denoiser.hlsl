/*
 * Copyright (c) 2024-2025, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */
 
#include <shared/globalCb.h>
#include <shared/lightingCb.h>

#include <NRD.hlsli>

#define BLOCK_SIZE 16

ConstantBuffer<GlobalConstants>     g_Global                    : register(b0, space0);

RWTexture2D<float4>                 u_Output                    : register(u0, space0);

RWTexture2D<float4>                 u_OutputDiffuseHitDistance  : register(u0, space1);
RWTexture2D<float4>                 u_OutputSpecularHitDistance : register(u1, space1);
RWTexture2D<float>                  u_OutputViewSpaceZ          : register(u2, space1);
RWTexture2D<float4>                 u_OutputNormalRoughness     : register(u3, space1);
RWTexture2D<float4>                 u_OutputMotionVectors       : register(u4, space1);
RWTexture2D<float4>                 u_OutputEmissive            : register(u5, space1);
RWTexture2D<float4>                 u_OutputDiffuseAlbedo       : register(u6, space1);
RWTexture2D<float4>                 u_OutputSpecularAlbedo      : register(u7, space1);

[numthreads(BLOCK_SIZE, BLOCK_SIZE, 1)]
void demodulate(in uint2 pixel : SV_DispatchThreadID)
{
    float viewSpaceZ = u_OutputViewSpaceZ[pixel];
    if (viewSpaceZ > TRACING_FAR_DENOISING_DISTANCE)
    {
        return;
    }

    float4 normalRoughness = u_OutputNormalRoughness[pixel];
    float3 emissive = u_OutputEmissive[pixel].xyz;

    // Diffuse
    {
        // Demodulate Diffuse Signal
        float4 diffuseData = u_OutputDiffuseHitDistance[pixel];
        if (any(diffuseData.xyz))
        {
            float3 diffuseAlbedo = u_OutputDiffuseAlbedo[pixel].xyz;
            diffuseAlbedo += diffuseAlbedo == 0.0f;
            diffuseData.xyz -= emissive;
            diffuseData.xyz /= diffuseAlbedo;
        }

#if USE_RELAX
        diffuseData = RELAX_FrontEnd_PackRadianceAndHitDist(diffuseData.xyz, diffuseData.w, true);
        u_OutputDiffuseHitDistance[pixel] = diffuseData;
#else
        float normalizedHitDistance = REBLUR_FrontEnd_GetNormHitDist(diffuseData.w, viewSpaceZ, g_Global.nrdHitDistanceParams, normalRoughness.w);
        diffuseData = REBLUR_FrontEnd_PackRadianceAndNormHitDist(diffuseData.xyz, normalizedHitDistance, true);
        u_OutputDiffuseHitDistance[pixel] = diffuseData;
#endif
    }

    // Specular
    {
        // Demodulate Specular Signal
        float4 specularData = u_OutputSpecularHitDistance[pixel];
        if (any(specularData.xyz))
        {
            float3 specularAlbedo = u_OutputSpecularAlbedo[pixel].xyz;
            specularAlbedo += specularAlbedo == 0.0f;
            specularData.xyz -= emissive;
            specularData.xyz /= specularAlbedo;
        }

#if USE_RELAX
        specularData = RELAX_FrontEnd_PackRadianceAndHitDist(specularData.xyz, specularData.w, true);
        u_OutputSpecularHitDistance[pixel] = specularData;
#else
        float normalizedHitDistance = REBLUR_FrontEnd_GetNormHitDist(specularData.w, viewSpaceZ, g_Global.nrdHitDistanceParams, normalRoughness.w);
        specularData = REBLUR_FrontEnd_PackRadianceAndNormHitDist(specularData.xyz, normalizedHitDistance, true);
        u_OutputSpecularHitDistance[pixel] = specularData;
#endif
    }

    normalRoughness = NRD_FrontEnd_PackNormalAndRoughness(normalRoughness.xyz, normalRoughness.w, true);
    u_OutputNormalRoughness[pixel] = normalRoughness;
}

[numthreads(BLOCK_SIZE, BLOCK_SIZE, 1)]
void composite(in uint2 pixel : SV_DispatchThreadID)
{
    float4 outputColor = u_OutputEmissive[pixel];
    float viewSpaceZ = u_OutputViewSpaceZ[pixel];

    if (viewSpaceZ < TRACING_FAR_DENOISING_DISTANCE)
    {
        // Diffuse
        {
            float4 diffuseData = u_OutputDiffuseHitDistance[pixel];
            float3 diffuseAlbedo = u_OutputDiffuseAlbedo[pixel].xyz;

#if USE_RELAX
            diffuseData = RELAX_BackEnd_UnpackRadiance(diffuseData);
#else
            diffuseData = REBLUR_BackEnd_UnpackRadianceAndNormHitDist(diffuseData);
#endif
            outputColor.xyz += diffuseData.xyz * diffuseAlbedo;
        }

        // Specular
        {
            float4 specularData = u_OutputSpecularHitDistance[pixel];
            float3 specularAlbedo = u_OutputSpecularAlbedo[pixel].xyz;

#if USE_RELAX
            specularData = RELAX_BackEnd_UnpackRadiance(specularData);
#else
            specularData = REBLUR_BackEnd_UnpackRadianceAndNormHitDist(specularData);
#endif
            outputColor.xyz += specularData.xyz * specularAlbedo;
        }

        u_Output[pixel] = outputColor;
    }
}
