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

#include <NRD.h>

namespace NrdConfig
{
    enum class DenoiserMethod : uint32_t
    {
        REBLUR,
        RELAX,
        MaxCount
    };

    nrd::RelaxSettings GetDefaultRELAXSettings();
    nrd::ReblurSettings GetDefaultREBLURSettings();
}