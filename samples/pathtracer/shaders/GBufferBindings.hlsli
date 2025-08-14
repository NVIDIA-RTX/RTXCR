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

ConstantBuffer<LightingConstants>   g_Lighting                          : register(b0, space0);
ConstantBuffer<GlobalConstants>     g_Global                            : register(b1, space0);

RaytracingAccelerationStructure     SceneBVH                            : register(t0, space0);
StructuredBuffer<InstanceData>      t_InstanceData                      : register(t1, space0);
StructuredBuffer<GeometryData>      t_GeometryData                      : register(t2, space0);
StructuredBuffer<MaterialConstants> t_MaterialConstants                 : register(t3, space0);
StructuredBuffer<uint>              t_instanceMorphTargetMetaDataBuffer : register(t4, space0);

SamplerState                        s_MaterialSampler                   : register(s0, space0);

// DLSS/NRD
RWTexture2D<float>                  u_OutputViewSpaceZ                  : register(u0, space1);
RWTexture2D<float4>                 u_OutputNormalRoughness             : register(u1, space1);
RWTexture2D<float4>                 u_OutputMotionVectors               : register(u2, space1);
RWTexture2D<float4>                 u_OutputEmissive                    : register(u3, space1);
RWTexture2D<float4>                 u_OutputDiffuseAlbedo               : register(u4, space1);
RWTexture2D<float4>                 u_OutputSpecularAlbedo              : register(u5, space1);
RWTexture2D<float2>                 u_OutputScreenSpaceMotionVectors    : register(u6, space1);

// DLSS-SR
RWTexture2D<float>                  u_OutputDeviceZ                     : register(u7, space1);

// Bindless Resources
VK_BINDING(0, 2) ByteAddressBuffer  t_BindlessBuffers[]                 : register(t0, space2);
VK_BINDING(1, 2) Texture2D          t_BindlessTextures[]                : register(t0, space3);
