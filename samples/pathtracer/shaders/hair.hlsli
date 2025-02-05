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

#include <rtxcr/HairChiangBSDF.hlsli>
#include <rtxcr/HairFarFieldBCSDF.hlsli>

bool isHairMaterial(const uint flags)
{
    return (flags & MaterialFlags_Hair) != 0 ||
            flags == 0; // Hack, when the material is empty, we treat it as hair material in RTXCR Sample
}

RTXCR_HairMaterialData createDefaultHairMaterial()
{
    RTXCR_HairMaterialData hairMaterialData;
    hairMaterialData.baseColor = g_Global.hairBaseColor;
    hairMaterialData.longitudinalRoughness = g_Global.longitudinalRoughness;
    hairMaterialData.azimuthalRoughness = g_Global.azimuthalRoughness;
    hairMaterialData.fresnelApproximation = !g_Global.analyticalFresnel;
    hairMaterialData.ior = g_Global.hairIor;
    hairMaterialData.eta = 1.0f / hairMaterialData.ior;
    hairMaterialData.cuticleAngleInDegrees = g_Global.cuticleAngleInDegrees;
    hairMaterialData.absorptionModel = g_Global.absorptionModel;
    hairMaterialData.melanin = g_Global.melanin;
    hairMaterialData.melaninRedness = g_Global.melaninRedness;

    return hairMaterialData;
}
