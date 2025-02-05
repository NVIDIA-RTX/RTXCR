@echo off
REM Script to run pathtracer.exe from the bin directory with parameters

set EXE_PATH=bin\pathtracer.exe
set WORKING_DIR=build\samples\pathtracer

REM Check if the executable exists
if not exist %EXE_PATH% (
    echo pathtracer.exe not found!
    exit /b 1
)


REM Run the application with parameters
"%EXE_PATH%" %*
