/*
* Copyright (c) 2024-2025, NVIDIA CORPORATION. All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/

#pragma once

#define TRACING_FAR_DISTANCE 100000.0f
#define TRACING_FAR_DENOISING_DISTANCE 10000.0f

#define RTXCR_CURVE_TESSELLATION_TYPE_POLYTUBE 0
#define RTXCR_CURVE_TESSELLATION_TYPE_DOTS     1
#define RTXCR_CURVE_TESSELLATION_TYPE_LSS      2
#define RTXCR_CURVE_TESSELLATION_TYPE_TRIANGLE 3

#define RTXCR_CURVE_POLYTUBE_ORDER 3
#define PI 3.141593f
#define TWO_PI 6.283185f

#define RTXCR_NVAPI_SHADER_EXT_SLOT 10

#ifdef __cplusplus
using namespace donut::math;
#endif

enum class RtxcrDebugOutputType : uint32_t
{
    None                = 0,
    DiffuseReflectance  = 1,
    SpecularReflectance = 2,
    Roughness           = 3,
    WorldSpaceNormals   = 4,
    ShadingNormals      = 5,
    WorldSpaceTangents  = 6,
    WorldSpacePosition  = 7,
    CurveRadius         = 8,
    ViewSpaceZ          = 9,
    DeviceZ             = 10,
    Barycentrics        = 11,
    DiffuseHitT         = 12,
    SpecularHitT        = 13,
    InstanceID          = 14,
    Emissives           = 15,
    BounceHeatmap       = 16,
    MotionVector        = 17,
    PathTracerOutput    = 18,
    NaN                 = 19,
    WhiteFurnace        = 20,
    IsMorphTarget       = 21,
};

enum class HairTechSelection : uint32_t
{
    Chiang   = 0,
    Farfield = 1,
};

enum class JitterMode : uint32_t
{
    None        = 0,
    Halton      = 1,
    Halton_DLSS = 2,
};

enum class SkyType : uint32_t
{
    Constant = 0,
    Procedural = 1,
    Environment_Map = 2,
};

struct LineSegment
{
    int geometryIndex;
    float3 pad0;

    float3 point0;
    float radius0;

    float3 point1;
    float radius1;
};

struct MorphTargetConstants
{
    int vertexCount;
    float lerpWeight;
    float prevLerpWeight;
    float pad0;
};
