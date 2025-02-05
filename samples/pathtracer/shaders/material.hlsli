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

#include <donut/shaders/bindless.h>
#include <donut/shaders/binding_helpers.hlsli>

#define SPECULAR_BRDF MICROFACET
#define DIFFUSE_BRDF LAMBERTIAN
#include "bsdf.hlsli"

enum MaterialAttributes
{
    MatAttr_BaseColor = 0x01,
    MatAttr_Emissive = 0x02,
    MatAttr_Normal = 0x04,
    MatAttr_MetalRough = 0x08,
    MatAttr_Transmission = 0x10,

    MatAttr_All = 0x1F
};

MaterialSample SampleGeometryMaterial(
    GeometrySample gs,
    float2 texGrad_x,
    float2 texGrad_y,
    float mipLevel, // <-- Use a compile time constant for mipLevel, < 0 for aniso filtering
    MaterialAttributes attributes,
    SamplerState materialSampler)
{
    MaterialTextureSample textures = DefaultMaterialTextures();

    if ((attributes & MatAttr_BaseColor) && (gs.material.baseOrDiffuseTextureIndex >= 0) && (gs.material.flags & MaterialFlags_UseBaseOrDiffuseTexture) != 0)
    {
        Texture2D diffuseTexture = t_BindlessTextures[NonUniformResourceIndex(gs.material.baseOrDiffuseTextureIndex)];

        if (mipLevel >= 0)
        {
            textures.baseOrDiffuse = diffuseTexture.SampleLevel(materialSampler, gs.texcoord, mipLevel);
        }
        else
        {
            textures.baseOrDiffuse = diffuseTexture.SampleGrad(materialSampler, gs.texcoord, texGrad_x, texGrad_y);
        }
    }

    if ((attributes & MatAttr_Emissive) && (gs.material.emissiveTextureIndex >= 0) && (gs.material.flags & MaterialFlags_UseEmissiveTexture) != 0)
    {
        Texture2D emissiveTexture = t_BindlessTextures[NonUniformResourceIndex(gs.material.emissiveTextureIndex)];

        if (mipLevel >= 0)
        {
            textures.emissive = emissiveTexture.SampleLevel(materialSampler, gs.texcoord, mipLevel);
        }
        else
        {
            textures.emissive = emissiveTexture.SampleGrad(materialSampler, gs.texcoord, texGrad_x, texGrad_y);
        }
    }

    if ((attributes & MatAttr_Normal) && (gs.material.normalTextureIndex >= 0) && (gs.material.flags & MaterialFlags_UseNormalTexture) != 0)
    {
        Texture2D normalsTexture = t_BindlessTextures[NonUniformResourceIndex(gs.material.normalTextureIndex)];

        const float2 texcoord = gs.texcoord * gs.material.normalTextureTransformScale;
        if (mipLevel >= 0)
        {
            textures.normal = normalsTexture.SampleLevel(materialSampler, texcoord, mipLevel);
        }
        else
        {
            textures.normal = normalsTexture.SampleGrad(materialSampler, texcoord, texGrad_x, texGrad_y);
        }
    }

    if ((attributes & MatAttr_MetalRough) && (gs.material.metalRoughOrSpecularTextureIndex >= 0) && (gs.material.flags & MaterialFlags_UseMetalRoughOrSpecularTexture) != 0)
    {
        Texture2D specularTexture = t_BindlessTextures[NonUniformResourceIndex(gs.material.metalRoughOrSpecularTextureIndex)];

        if (mipLevel >= 0)
        {
            textures.metalRoughOrSpecular = specularTexture.SampleLevel(materialSampler, gs.texcoord, mipLevel);
        }
        else
        {
            textures.metalRoughOrSpecular = specularTexture.SampleGrad(materialSampler, gs.texcoord, texGrad_x, texGrad_y);
        }
    }

    if ((attributes & MatAttr_Transmission) && (gs.material.transmissionTextureIndex >= 0) && (gs.material.flags & MaterialFlags_UseTransmissionTexture) != 0)
    {
        Texture2D transmissionTexture = t_BindlessTextures[NonUniformResourceIndex(gs.material.transmissionTextureIndex)];

        if (mipLevel >= 0)
        {
            textures.transmission = transmissionTexture.SampleLevel(materialSampler, gs.texcoord, mipLevel);
        }
        else
        {
            textures.transmission = transmissionTexture.SampleGrad(materialSampler, gs.texcoord, texGrad_x, texGrad_y);
        }
    }

    return EvaluateSceneMaterial(gs.geometryNormal, gs.tangent, gs.material, textures);
}

MaterialProperties createMaterialProperties(const MaterialSample material)
{
    MaterialProperties materialProps;
    materialProps.baseColor = material.baseColor;
    materialProps.metalness = material.metalness;
    materialProps.emissiveColor = material.emissiveColor;
    materialProps.roughness = material.roughness;
    materialProps.transmissivness = material.transmission;
    materialProps.opacity = material.opacity;
    materialProps.diffuseAlbedo = material.diffuseAlbedo;
    materialProps.specularF0 = material.specularF0;
    materialProps.hasMetalRoughParams = material.hasMetalRoughParams;

    return materialProps;
}
