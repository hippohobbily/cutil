@echo off
cl /O2 /Fe:writefile.exe writefile.c
if %errorlevel% == 0 (
    echo Build successful!
) else (
    echo Build failed!
)