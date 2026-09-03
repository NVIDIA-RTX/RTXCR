# Copyright (c) 2024-2026, NVIDIA CORPORATION.  All rights reserved.
#
# NVIDIA CORPORATION and its licensors retain all intellectual property
# and proprietary rights in and to this software, related documentation
# and any modifications thereto.  Any use, reproduction, disclosure or
# distribution of this software and related documentation without an express
# license agreement from NVIDIA CORPORATION is strictly prohibited.

set(NRD_NORMAL_ENCODING "2" CACHE STRING "Normal encoding variant (0-4, matches nrd::NormalEncoding)")
set(NRD_ROUGHNESS_ENCODING "1" CACHE STRING "Roughness encoding variant (0-2, matches nrd::RoughnessEncoding)")
set(NRD_EMBEDS_SPIRV_SHADERS OFF CACHE BOOL "NRD embeds SPIRV shaders" FORCE)
set(NRD_EMBEDS_DXIL_SHADERS OFF CACHE BOOL "NRD embeds DXIL shaders" FORCE)
set(NRD_EMBEDS_DXBC_SHADERS OFF CACHE BOOL "NRD embeds DXBC shaders" FORCE)

add_subdirectory("RayTracingDenoiser")

file(GLOB_RECURSE NRD_SHADER_SOURCES CONFIGURE_DEPENDS
    "RayTracingDenoiser/Shaders/*.hlsl"
    "RayTracingDenoiser/Shaders/*.hlsli"
)

get_target_property(NRD_MATHLIB_SOURCE_DIR MathLib SOURCE_DIR)

set(SHADERMAKE_GENERAL_ARGS
    "--flatten --stripReflection --WX --allResourcesBound -D NRD_INTERNAL"
)
set(SHADERMAKE_SPIRV_ARGS
    "${SHADERMAKE_GENERAL_ARGS} --sRegShift 0 --bRegShift 2 --uRegShift 3 --tRegShift 20"
)

donut_compile_shaders(
    TARGET nrd_shaders
    CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/RayTracingDenoiser/Shaders/Shaders.cfg
    SOURCES ${NRD_SHADER_SOURCES}
    FOLDER "NRD"
    DXIL ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders/nrd/dxil
    SPIRV_DXC ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders/nrd/spirv
    INCLUDES ${NRD_MATHLIB_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/RayTracingDenoiser/Shaders
    SHADERMAKE_OPTIONS_DXIL ${SHADERMAKE_GENERAL_ARGS}
    SHADERMAKE_OPTIONS_SPIRV ${SHADERMAKE_SPIRV_ARGS}
)
