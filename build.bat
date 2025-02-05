@echo off
REM Script to build a solution using CMake

set BUILD_DIR=build
set GENERATOR="Visual Studio 17 2022"

REM Create build directory if it doesn't exist
if not exist %BUILD_DIR% mkdir %BUILD_DIR%

REM Navigate to build directory
cd %BUILD_DIR%

REM Run CMake to generate the build system
cmake -G %GENERATOR% ..
if %errorlevel% neq 0 (
    echo CMake generation failed!
    exit /b %errorlevel%
)

REM Build the solution using CMake
cmake --build . --config Release
if %errorlevel% neq 0 (
    echo Build failed!
    exit /b %errorlevel%
)

cd..

echo Build completed successfully!
