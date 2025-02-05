# Copyright (c) 2024-2025, NVIDIA CORPORATION.  All rights reserved.
#
# NVIDIA CORPORATION and its licensors retain all intellectual property
# and proprietary rights in and to this software, related documentation
# and any modifications thereto.  Any use, reproduction, disclosure or
# distribution of this software and related documentation without an express
# license agreement from NVIDIA CORPORATION is strictly prohibited.

cmake_minimum_required (VERSION 3.19)

project(
    RTXCR
    DESCRIPTION "Samples showcasing character rendering features"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

# Set MSVC debug level
if(MSVC)
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /D_ITERATOR_DEBUG_LEVEL=1")
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Find GIT
find_package(Git)
if(Git_FOUND)
    message("Git found: ${GIT_EXECUTABLE}")
else()
    message("Git NOT found!")
endif()

# Warnings as errors
set(CMAKE_COMPILE_WARNING_AS_ERROR ON)

# -----------------------------------------------------------------------------
# Other packages downloaded from zips
# -----------------------------------------------------------------------------

# Helper to download and unzip a package from a URL
# Uses a zero-length file to identify the version of the package
function(CheckAndDownloadPackage NAME VERSION LOCAL_PATH URL)
    # Do we already have the correct version?
    if(NOT EXISTS ${LOCAL_PATH}/${VERSION}.ver)
        # Was there a previous version that we need to delete?
        if(EXISTS ${LOCAL_PATH})
            message(STATUS "Deleting old " ${NAME})
            file(REMOVE_RECURSE ${LOCAL_PATH})
        endif()
        message(STATUS "Obtaining " ${NAME} " " ${VERSION})
        file(DOWNLOAD ${URL} ${LOCAL_PATH}.zip)
        message(STATUS "Extracting " ${NAME})
        file(ARCHIVE_EXTRACT INPUT ${LOCAL_PATH}.zip DESTINATION ${LOCAL_PATH})
        file(REMOVE ${LOCAL_PATH}.zip)
        # Create an empty file so we know which version we have
        file(WRITE ${LOCAL_PATH}/${VERSION}.ver)
    endif()
endfunction()

CheckAndDownloadPackage("DXC" "v1.7.2308" ${CMAKE_CURRENT_SOURCE_DIR}/external/dxc https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.7.2308/dxc_2023_08_14.zip)

option(DONUT_WITH_DX11 "" OFF)
option(DONUT_WITH_DX12 "" ON)
option(DONUT_WITH_VULKAN "" ON)
option(DONUT_WITH_LZ4 "" OFF)
option(DONUT_WITH_MINIZ "" OFF)

# -----------------------------------------------------------------------------
# Configure options
# -----------------------------------------------------------------------------

# Setup NVAPI support
option(NVRHI_WITH_NVAPI "Include NVAPI support (requires NVAPI SDK)" ON)
set(NVAPI_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/nvapi/" CACHE STRING "Path to NVAPI include headers/shaders" )
set(NVAPI_LIBRARY "${CMAKE_CURRENT_SOURCE_DIR}/external/nvapi/amd64/nvapi64.lib" CACHE STRING "Path to NVAPI .lib file")

# Setup shader compiler
set(DXC_DXIL_EXECUTABLE "${CMAKE_CURRENT_SOURCE_DIR}/external/dxc/bin/x64/dxc.exe" CACHE STRING "DXC shader compiler path")
set(DXC_PATH "${CMAKE_CURRENT_SOURCE_DIR}/external/dxc/bin/x64/dxc.exe")
set(DXC_SPIRV_PATH "${CMAKE_CURRENT_SOURCE_DIR}/external/dxc/bin/x64/dxc.exe")

# Setup asset importer support
option(DONUT_WITH_ASSIMP "" OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
set(DONUT_SHADERS_OUTPUT_DIR "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders/framework")

set(COMPILE_FROM_TC_BUILD_AGENT FALSE CACHE BOOL "Flag required by TeamCity build agent for setup.")

unset(DONUT_WITH_VULKAN CACHE)
unset(NVRHI_WITH_VULKAN CACHE)

# Force Vulkan support
option(DONUT_WITH_VULKAN "Enable the Vulkan version of Donut" ON)

# Add projects
add_subdirectory(external/donut)

message(STATUS "NVRHI Vulkan support " "${NVRHI_WITH_VULKAN}")
message(STATUS "Donut Vulkan support " "${DONUT_WITH_VULKAN}")

if(NVRHI_WITH_VULKAN OR NVRHI_WITH_DX12)
    add_subdirectory(samples/pathtracer)
    add_subdirectory(samples/pathtracer/shaders)

    # Set Pathtracer sample project as a startup project
    set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT pathtracer)
endif()

if(NOT COMPILE_FROM_TC_BUILD_AGENT)
    # Fetch latest version of the assets
    if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/assets)
        # Fetch assets (in the future we can swap to FetchContent(URL ARCHIVE) to obtain stand-alone asset dirs)
        include(FetchContent)
        FetchContent_Populate(
            assets
            GIT_REPOSITORY https://github.com/NVIDIA-RTX/RTXCR-Assets.git
            GIT_TAG main
            SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/assets
        )
    else()
        execute_process(
            COMMAND ${GIT_EXECUTABLE} pull origin main WORKING_DIRECTORY
            ${CMAKE_CURRENT_SOURCE_DIR}/assets RESULT_VARIABLE
            GIT_SUBMOD_RESULT
        )
    endif()

    execute_process(
        COMMAND ${GIT_EXECUTABLE} lfs pull WORKING_DIRECTORY
        ${CMAKE_CURRENT_SOURCE_DIR}/assets RESULT_VARIABLE
        GIT_SUBMOD_RESULT
    )
endif()

# Add RTXCR includes to the solution
set(RTXCR_MATERIAL_LIBRARY_SHADERS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/libraries/rtxcr/material/shaders")
file(GLOB_RECURSE INCLUDE_RTXCR_MATERIAL_SHADERS "${RTXCR_MATERIAL_LIBRARY_SHADERS_ROOT}/*.h" "${RTXCR_MATERIAL_LIBRARY_SHADERS_ROOT}/*.hlsl" "${RTXCR_MATERIAL_LIBRARY_SHADERS_ROOT}/*.hlsli")

set(RTXCR_GEOMETRY_LIBRARY_INCLUDE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/libraries/rtxcr/geometry/include")
file(GLOB_RECURSE INCLUDE_RTXCR_GEOMETRY "${RTXCR_GEOMETRY_LIBRARY_INCLUDE_ROOT}/*.h")

set(RTXCR_GEOMETRY_LIBRARY_SHADERS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/libraries/rtxcr/geometry/shaders")
file(GLOB_RECURSE INCLUDE_RTXCR_GEOMETRY_SHADERS "${RTXCR_GEOMETRY_LIBRARY_SHADERS_ROOT}/*.h" "${RTXCR_GEOMETRY_LIBRARY_SHADERS_ROOT}/*.hlsl" "${RTXCR_GEOMETRY_LIBRARY_SHADERS_ROOT}/*.hlsli")

set(VS_FOLDER_NAME "libraries")

add_library(libraries ${INCLUDE_RTXCR_GEOMETRY})
set_target_properties(libraries PROPERTIES LINKER_LANGUAGE CXX)
set_target_properties(libraries PROPERTIES FOLDER ${VS_FOLDER_NAME})

add_library(libraries_shaders ${INCLUDE_RTXCR_MATERIAL_SHADERS} ${INCLUDE_RTXCR_GEOMETRY_SHADERS})
set_target_properties(libraries_shaders PROPERTIES LINKER_LANGUAGE CXX)
set_target_properties(libraries_shaders PROPERTIES FOLDER ${VS_FOLDER_NAME})

# Include external
add_subdirectory(external)

# Create Screenshot Directory
file(MAKE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin/screenshots")
