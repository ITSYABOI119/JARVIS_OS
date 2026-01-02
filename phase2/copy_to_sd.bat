@echo off
REM JARVIS AI-OS - Phase 2 Week 32
REM Copy boot files to Raspberry Pi 4 SD Card
REM
REM Usage: copy_to_sd.bat E:
REM        (where E: is your SD card boot partition)

if "%1"=="" (
    echo Usage: copy_to_sd.bat [SD_DRIVE_LETTER:]
    echo Example: copy_to_sd.bat E:
    echo.
    echo Please insert your SD card and check the drive letter in File Explorer.
    exit /b 1
)

set SD_DRIVE=%1
set FIRMWARE_DIR=%~dp0firmware

echo.
echo ============================================
echo  JARVIS AI-OS - SD Card Boot File Copy
echo ============================================
echo.
echo Source: %FIRMWARE_DIR%
echo Target: %SD_DRIVE%\
echo.

REM Check if source files exist
if not exist "%FIRMWARE_DIR%\kernel8.img" (
    echo ERROR: kernel8.img not found in %FIRMWARE_DIR%
    echo Run the build first!
    exit /b 1
)

REM Check if SD card is accessible
if not exist "%SD_DRIVE%\" (
    echo ERROR: Cannot access %SD_DRIVE%
    echo Please check if SD card is inserted and drive letter is correct.
    exit /b 1
)

echo Copying files...
echo.

copy /Y "%FIRMWARE_DIR%\kernel8.img" "%SD_DRIVE%\"
if errorlevel 1 goto error

copy /Y "%FIRMWARE_DIR%\start4.elf" "%SD_DRIVE%\"
if errorlevel 1 goto error

copy /Y "%FIRMWARE_DIR%\fixup4.dat" "%SD_DRIVE%\"
if errorlevel 1 goto error

copy /Y "%FIRMWARE_DIR%\config.txt" "%SD_DRIVE%\"
if errorlevel 1 goto error

echo.
echo ============================================
echo  SUCCESS! All files copied to %SD_DRIVE%
echo ============================================
echo.
echo Files on SD card:
dir "%SD_DRIVE%\"
echo.
echo Next steps:
echo 1. Safely eject the SD card
echo 2. Insert into Raspberry Pi 4
echo 3. Connect USB-Serial adapter (GPIO14=TX, GPIO15=RX)
echo 4. Open terminal: 115200 baud, 8N1
echo 5. Power on Pi 4
echo.
echo You should see JARVIS banner on serial console!
echo.
exit /b 0

:error
echo.
echo ERROR: Failed to copy files!
exit /b 1
