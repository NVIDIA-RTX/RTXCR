@echo off
REM Script to generate a Visual Studio solution using CMake and open it

set BUILD_DIR=build
set GENERATOR="Visual Studio 17 2022"
set OPEN_VS=0
set USE_VK_STREAMLINE=ON

REM Check for -v flag
for %%i in (%*) do (
    if "%%i"=="-v" set OPEN_VS=1
    if /I "%%i"=="-disableStreamline" set USE_VK_STREAMLINE=OFF
)

REM Create build directory if it doesn't exist
if not exist %BUILD_DIR% mkdir %BUILD_DIR%

REM Navigate to build directory
cd %BUILD_DIR%

REM Run CMake to generate the Visual Studio solution
cmake -G %GENERATOR% -DUSE_VK_STREAMLINE:BOOL=%USE_VK_STREAMLINE% ..
if %errorlevel% neq 0 (
    echo CMake generation failed!
    exit /b %errorlevel%
)

REM Find the generated solution file
for %%f in (*.sln) do set SOLUTION_FILE=%%f

if not defined SOLUTION_FILE (
    echo No Visual Studio solution found!
    exit /b 1
)

if %OPEN_VS%==1 (
    REM Open the solution in Visual Studio
    start "" "%SOLUTION_FILE%"
)

cd..
