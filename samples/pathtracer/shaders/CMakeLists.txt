# Copyright (c) 2024-2025, NVIDIA CORPORATION.  All rights reserved.
#
# NVIDIA CORPORATION and its licensors retain all intellectual property
# and proprietary rights in and to this software, related documentation
# and any modifications thereto.  Any use, reproduction, disclosure or
# distribution of this software and related documentation without an express
# license agreement from NVIDIA CORPORATION is strictly prohibited.

cmake_minimum_required (VERSION 3.19)

include(${CMAKE_SOURCE_DIR}/external/donut/compileshaders.cmake)
file(GLOB_RECURSE shaders "*.hlsl" "*.hlsli")

set(project pathtracer)
set(folder "Samples/Pathtracer")

# Set External includes
set(EXTERNAL_DIR ${CMAKE_SOURCE_DIR}/external)

# Set SDK includes
set(RTXCR_SDK_DIR ${CMAKE_SOURCE_DIR}/libraries)

set (SHADERMAKE_GENERAL_ARGS_SPIRV "-I ${EXTERNAL_DIR}/MathLib
                                    -I ${CMAKE_CURRENT_SOURCE_DIR}/..
                                    -I ${RTXCR_SDK_DIR}/rtxcr/material/shaders/include
                                    -I ${RTXCR_SDK_DIR}/rtxcr/geometry/shaders/include")
set (SHADERMAKE_GENERAL_ARGS_DXIL "-I ${EXTERNAL_DIR}/MathLib
                                   -I ${CMAKE_CURRENT_SOURCE_DIR}/..
                                   -I ${RTXCR_SDK_DIR}/rtxcr/material/shaders/include
                                   -I ${RTXCR_SDK_DIR}/rtxcr/geometry/shaders/include")

if(DONUT_WITH_AFTERMATH OR DONUT_EMBED_SHADER_PDBS)
        set(shadermake_options --embedPDB)
endif()

donut_compile_shaders(
        TARGET ${project}_shaders
        CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/shaders.cfg
        SOURCES ${shaders}
        FOLDER ${folder}
        SPIRV_DXC ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders/${project}/spirv
        DXIL ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders/${project}/dxil
        SHADERMAKE_OPTIONS ${shadermake_options}
        SHADERMAKE_OPTIONS_SPIRV ${SHADERMAKE_GENERAL_ARGS_SPIRV}
        SHADERMAKE_OPTIONS_DXIL ${SHADERMAKE_GENERAL_ARGS_DXIL}
)

add_dependencies(${project} ${project}_shaders)
set_target_properties(${project} PROPERTIES FOLDER ${folder})
