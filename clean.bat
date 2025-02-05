@echo off

if exist bin (
    rmdir /s /q bin
    echo Deleted 'bin' folder.
) else (
    echo 'bin' folder not found.
)

if exist build (
    rmdir /s /q build
    echo Deleted 'build' folder.
) else (
    echo 'build' folder not found.
)
