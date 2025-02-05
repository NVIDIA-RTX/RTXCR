# Copyright (c) 2024-2025, NVIDIA CORPORATION.  All rights reserved.
#
# NVIDIA CORPORATION and its licensors retain all intellectual property
# and proprietary rights in and to this software, related documentation
# and any modifications thereto.  Any use, reproduction, disclosure or
# distribution of this software and related documentation without an express
# license agreement from NVIDIA CORPORATION is strictly prohibited.

cmake_minimum_required (VERSION 3.19)

file(GLOB_RECURSE sources "*.cpp" "*.h")

set(project pathtracer)
set(folder "Samples/Pathtracer")

add_executable(${project} WIN32 ${sources})
target_link_libraries(${project} donut_render donut_app donut_engine streamline )
set_target_properties(${project} PROPERTIES FOLDER ${folder})

ADD_DEFINITIONS(-DUNICODE)
ADD_DEFINITIONS(-D_UNICODE)

option(USE_DX12 "" ON)
option(USE_VK "" ON)
if(USE_DX12)
    set(USE_DX12 1)
    add_compile_definitions(USE_DX12=${USE_DX12})
endif()
if(USE_VK)
    set(USE_VK 1)
    add_compile_definitions(USE_VK=${USE_VK})
endif()

set(RTXCR_LIBRARIES_ROOT "${CMAKE_SOURCE_DIR}/libraries")
set(RTXCR_MATERIALS_LIBRARY_ROOT "${CMAKE_SOURCE_DIR}/libraries/rtxcr/material/shaders")
set(RTXCR_GEOMETRY_LIBRARY_ROOT "${CMAKE_SOURCE_DIR}/libraries/rtxcr/geometry/shaders")
include_directories("${RTXCR_LIBRARIES_ROOT}")
include_directories("${RTXCR_MATERIALS_LIBRARY_ROOT}/include")
include_directories("${RTXCR_GEOMETRY_LIBRARY_ROOT}/include")
include_directories("shared")
