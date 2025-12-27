@echo off
REM ============================================================================
REM JARVIS AI-OS - Transfer AI Model to Raspberry Pi 4
REM ============================================================================
REM
REM This script transfers the TinyLlama model from your PC to the Pi 4.
REM Run this from your Windows PC after connecting Pi 4 to your network.
REM
REM Usage:
REM   transfer_model_to_pi.bat [pi_address] [username]
REM
REM Examples:
REM   transfer_model_to_pi.bat                    (uses defaults: raspberrypi, pi)
REM   transfer_model_to_pi.bat 192.168.1.100      (custom IP, default user)
REM   transfer_model_to_pi.bat 192.168.1.100 user (custom IP and user)
REM
REM ============================================================================

setlocal EnableDelayedExpansion

REM Configuration
set "MODEL_NAME=tinyllama-1.1b-chat-v1.0.Q4_K_M.gguf"
set "LOCAL_MODELS_DIR=%USERPROFILE%\Documents\JARVIS_OS\models"
set "REMOTE_MODELS_DIR=~/models"

REM Parse arguments
if "%1"=="" (
    set "PI_HOST=raspberrypi"
) else (
    set "PI_HOST=%1"
)

if "%2"=="" (
    set "PI_USER=pi"
) else (
    set "PI_USER=%2"
)

echo ============================================================
echo   JARVIS AI-OS - Model Transfer to Pi 4
echo ============================================================
echo.
echo   Target: %PI_USER%@%PI_HOST%
echo   Model: %MODEL_NAME%
echo.

REM Check if model exists locally
set "MODEL_PATH=%LOCAL_MODELS_DIR%\%MODEL_NAME%"
if not exist "%MODEL_PATH%" (
    echo [ERROR] Model not found at: %MODEL_PATH%
    echo.
    echo Please download the model first:
    echo   1. Go to: https://huggingface.co/TheBloke/TinyLlama-1.1B-Chat-v1.0-GGUF
    echo   2. Download: %MODEL_NAME%
    echo   3. Save to: %LOCAL_MODELS_DIR%\
    echo.
    pause
    exit /b 1
)

echo [1/3] Model found: %MODEL_PATH%
for %%A in ("%MODEL_PATH%") do set "MODEL_SIZE=%%~zA"
set /a "MODEL_SIZE_MB=%MODEL_SIZE% / 1048576"
echo       Size: %MODEL_SIZE_MB% MB

REM Create remote directory
echo.
echo [2/3] Creating remote directory...
ssh %PI_USER%@%PI_HOST% "mkdir -p %REMOTE_MODELS_DIR%"
if errorlevel 1 (
    echo [ERROR] Failed to create remote directory
    echo Make sure:
    echo   - Pi 4 is powered on and connected to network
    echo   - SSH is enabled on Pi 4
    echo   - Hostname/IP is correct: %PI_HOST%
    echo.
    pause
    exit /b 1
)
echo       Created: %REMOTE_MODELS_DIR%

REM Transfer model
echo.
echo [3/3] Transferring model (~640MB)...
echo       This will take 5-15 minutes over WiFi, 1-3 minutes over Ethernet.
echo.
scp "%MODEL_PATH%" "%PI_USER%@%PI_HOST%:%REMOTE_MODELS_DIR%/%MODEL_NAME%"
if errorlevel 1 (
    echo.
    echo [ERROR] Transfer failed
    pause
    exit /b 1
)

echo.
echo ============================================================
echo   Transfer Complete!
echo ============================================================
echo.
echo   Model is now at: %PI_HOST%:%REMOTE_MODELS_DIR%/%MODEL_NAME%
echo.
echo   Next steps:
echo   1. SSH to Pi: ssh %PI_USER%@%PI_HOST%
echo   2. Run setup: ./setup_pi4_local_ai.sh
echo   3. Or skip setup and test directly
echo.
pause
