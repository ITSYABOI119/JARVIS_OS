@echo off
REM ==========================================================================
REM JARVIS AI-OS Windows Installer for Raspberry Pi 4
REM ==========================================================================
REM
REM Prepares an SD card with JARVIS seL4 firmware from Windows.
REM
REM What this script does:
REM   1. Formats an SD card (FAT32 boot partition) using diskpart
REM   2. Copies all firmware files to the boot partition
REM   3. Verifies the installation
REM   4. Provides serial console setup instructions
REM
REM Boot flow: GPU firmware -> U-Boot -> boot.scr -> kernel8.img -> seL4 -> JARVIS
REM UART: 115200 baud, 8N1, GPIO14(TX) / GPIO15(RX)
REM
REM Usage:
REM   install_jarvis.bat              (interactive - prompts for disk number)
REM   install_jarvis.bat --help       (show help)
REM   install_jarvis.bat --list       (list available disks)
REM   install_jarvis.bat --disk N     (use disk number N)
REM   install_jarvis.bat --drive X:   (skip format, copy to existing drive)
REM
REM IMPORTANT: Run this script as Administrator for formatting operations.
REM ==========================================================================

setlocal enabledelayedexpansion

set "VERSION=1.0.0"
set "SCRIPT_DIR=%~dp0"
set "FIRMWARE_DIR=%SCRIPT_DIR%..\firmware"
set "JARVIS_ROOT=%SCRIPT_DIR%..\.."
set "DISK_NUM="
set "DRIVE_LETTER="
set "COPY_ONLY=false"
set "TEMP_DISKPART=%TEMP%\jarvis_diskpart.txt"

REM --------------------------------------------------------------------------
REM Parse arguments
REM --------------------------------------------------------------------------
:parse_args
if "%~1"=="" goto :start
if /I "%~1"=="--help" goto :show_help
if /I "%~1"=="-h" goto :show_help
if /I "%~1"=="--list" goto :list_disks
if /I "%~1"=="--disk" (
    set "DISK_NUM=%~2"
    shift
    shift
    goto :parse_args
)
if /I "%~1"=="--drive" (
    set "DRIVE_LETTER=%~2"
    set "COPY_ONLY=true"
    shift
    shift
    goto :parse_args
)
echo ERROR: Unknown option: %~1
echo Run: %~n0 --help
exit /b 1

REM --------------------------------------------------------------------------
REM Help
REM --------------------------------------------------------------------------
:show_help
echo.
echo ========================================
echo  JARVIS AI-OS Windows Installer v%VERSION%
echo ========================================
echo.
echo Deploy JARVIS seL4 to a Raspberry Pi 4 SD card from Windows.
echo.
echo USAGE:
echo   %~n0                    Interactive mode (prompts for disk)
echo   %~n0 --help             Show this help
echo   %~n0 --list             List available disks
echo   %~n0 --disk N           Format disk N and install
echo   %~n0 --drive X:         Copy-only to existing drive letter
echo.
echo MODES:
echo   Full install (--disk):  Formats the SD card and copies firmware.
echo                           Requires Administrator privileges.
echo   Copy-only (--drive):    Copies firmware to an already-formatted
echo                           FAT32 partition. No admin required.
echo.
echo EXAMPLES:
echo   %~n0 --list             See which disk is your SD card
echo   %~n0 --disk 2           Format disk 2 and install JARVIS
echo   %~n0 --drive E:         Copy firmware to E: (already formatted)
echo.
echo FIRMWARE FILES:
echo   start4.elf              GPU firmware
echo   fixup4.dat              Memory configuration
echo   config.txt              Pi 4 boot configuration
echo   kernel8.img             JARVIS seL4 kernel image
echo   bcm2711-rpi-4-b.dtb    Device tree blob
echo   boot.scr                U-Boot boot script
echo   u-boot.bin              U-Boot bootloader
echo.
echo UART WIRING:
echo   Pi GPIO14 (TXD) --- USB-Serial RXD
echo   Pi GPIO15 (RXD) --- USB-Serial TXD
echo   Pi GND          --- USB-Serial GND
echo   Baud: 115200, Data: 8, Stop: 1, Parity: None, Flow: None
echo.
exit /b 0

REM --------------------------------------------------------------------------
REM List disks
REM --------------------------------------------------------------------------
:list_disks
echo.
echo Available disks:
echo.
echo list disk > "%TEMP_DISKPART%"
diskpart /s "%TEMP_DISKPART%" 2>nul
del "%TEMP_DISKPART%" 2>nul
echo.
echo NOTE: Identify your SD card by its size. Do NOT select your system disk.
echo       SD cards are typically 8GB-128GB.
echo.
exit /b 0

REM --------------------------------------------------------------------------
REM Main entry point
REM --------------------------------------------------------------------------
:start
echo.
echo ========================================
echo  JARVIS AI-OS Windows Installer v%VERSION%
echo ========================================
echo.

REM Check firmware directory
if not exist "%FIRMWARE_DIR%\kernel8.img" (
    echo ERROR: kernel8.img not found in %FIRMWARE_DIR%
    echo Build the kernel first using WSL:
    echo   wsl -e bash -lc "cd .../phase2/scripts ^&^& bash build_and_copy_kernel.sh"
    exit /b 1
)

REM Verify all required firmware files exist
set "MISSING=false"
for %%F in (start4.elf fixup4.dat config.txt kernel8.img bcm2711-rpi-4-b.dtb boot.scr u-boot.bin) do (
    if not exist "%FIRMWARE_DIR%\%%F" (
        echo ERROR: Missing firmware file: %%F
        set "MISSING=true"
    )
)
if "!MISSING!"=="true" (
    echo.
    echo Some firmware files are missing. Build the kernel and ensure all
    echo files are present in: %FIRMWARE_DIR%
    exit /b 1
)

echo Firmware directory: %FIRMWARE_DIR%
echo.

REM Branch: copy-only mode or full install
if "!COPY_ONLY!"=="true" goto :copy_to_drive

REM --------------------------------------------------------------------------
REM Full install mode: format + copy
REM --------------------------------------------------------------------------

REM Check for admin privileges
net session >nul 2>&1
if errorlevel 1 (
    echo ERROR: Administrator privileges required for disk formatting.
    echo.
    echo Right-click this script and select "Run as administrator", or use:
    echo   %~n0 --drive X:
    echo to copy files without formatting (no admin needed).
    exit /b 1
)

REM If no disk specified, show list and prompt
if "%DISK_NUM%"=="" (
    echo Listing available disks...
    echo.
    echo list disk > "%TEMP_DISKPART%"
    diskpart /s "%TEMP_DISKPART%" 2>nul
    del "%TEMP_DISKPART%" 2>nul
    echo.
    echo Identify your SD card by its size (typically 8GB-128GB).
    echo Do NOT select Disk 0 -- that is usually your system drive.
    echo.
    set /p "DISK_NUM=Enter SD card disk number (e.g. 2): "
)

REM Validate disk number is a number
set /a "_test_num=!DISK_NUM!" 2>nul
if "!_test_num!"=="0" if not "!DISK_NUM!"=="0" (
    echo ERROR: Invalid disk number: !DISK_NUM!
    exit /b 1
)

REM Safety: refuse disk 0
if "!DISK_NUM!"=="0" (
    echo ERROR: Refusing to format Disk 0 -- this is typically your system drive!
    echo If you are absolutely sure, use diskpart manually.
    exit /b 1
)

echo.
echo ========================================
echo  WARNING: DESTRUCTIVE OPERATION
echo ========================================
echo.
echo About to FORMAT Disk !DISK_NUM!.
echo ALL DATA ON THIS DISK WILL BE PERMANENTLY LOST.
echo.
set /p "CONFIRM=Type INSTALL to proceed (anything else to cancel): "
if not "!CONFIRM!"=="INSTALL" (
    echo.
    echo Cancelled by user.
    exit /b 0
)
echo.

REM Create diskpart script to format the SD card
echo [STEP 1/3] Formatting SD card (Disk !DISK_NUM!)...
(
    echo select disk !DISK_NUM!
    echo clean
    echo create partition primary
    echo format fs=fat32 label=JARVIS-BOOT quick
    echo assign
    echo active
) > "%TEMP_DISKPART%"

diskpart /s "%TEMP_DISKPART%"
if errorlevel 1 (
    echo.
    echo ERROR: diskpart formatting failed!
    echo Make sure the disk number is correct and the SD card is not write-protected.
    del "%TEMP_DISKPART%" 2>nul
    exit /b 1
)
del "%TEMP_DISKPART%" 2>nul

echo.
echo Formatting complete. Waiting for drive to appear...
timeout /t 3 /nobreak >nul

REM Find which drive letter was assigned
echo [STEP 1.5/3] Detecting assigned drive letter...
set "DRIVE_LETTER="
for /f "tokens=1,2,3" %%a in ('wmic logicaldisk get caption^,volumename^,description ^| findstr /I "JARVIS-BOOT"') do (
    set "DRIVE_LETTER=%%a"
)

if "!DRIVE_LETTER!"=="" (
    REM Try alternative detection via label
    for /f "tokens=1" %%d in ('wmic logicaldisk where "VolumeName='JARVIS-BOOT'" get DeviceID /value 2^>nul ^| findstr "="') do (
        for /f "tokens=2 delims==" %%v in ("%%d") do (
            set "DRIVE_LETTER=%%v"
        )
    )
)

if "!DRIVE_LETTER!"=="" (
    echo WARNING: Could not auto-detect the assigned drive letter.
    echo.
    set /p "DRIVE_LETTER=Enter the drive letter of the SD card (e.g. E:): "
)

if "!DRIVE_LETTER!"=="" (
    echo ERROR: No drive letter specified.
    exit /b 1
)

REM Ensure drive letter ends with colon
if not "!DRIVE_LETTER:~-1!"==":" set "DRIVE_LETTER=!DRIVE_LETTER!:"

echo Drive letter: !DRIVE_LETTER!
echo.

REM --------------------------------------------------------------------------
REM Copy firmware files
REM --------------------------------------------------------------------------
:copy_to_drive

REM Validate drive is accessible
if not exist "!DRIVE_LETTER!\" (
    echo ERROR: Cannot access !DRIVE_LETTER!\
    echo Make sure the SD card is inserted and the drive letter is correct.
    exit /b 1
)

echo [STEP 2/3] Copying firmware files to !DRIVE_LETTER!\
echo.

set "COPY_FAIL=false"
for %%F in (start4.elf fixup4.dat config.txt kernel8.img bcm2711-rpi-4-b.dtb boot.scr u-boot.bin) do (
    echo   Copying %%F...
    copy /Y "%FIRMWARE_DIR%\%%F" "!DRIVE_LETTER!\%%F" >nul
    if errorlevel 1 (
        echo   ERROR: Failed to copy %%F
        set "COPY_FAIL=true"
    )
)

REM Copy overlays directory if present
if exist "%FIRMWARE_DIR%\overlays\" (
    echo   Copying overlays\...
    xcopy /E /I /Y /Q "%FIRMWARE_DIR%\overlays" "!DRIVE_LETTER!\overlays" >nul
    if errorlevel 1 (
        echo   WARNING: Failed to copy overlays directory
    )
)

if "!COPY_FAIL!"=="true" (
    echo.
    echo ERROR: One or more files failed to copy!
    exit /b 1
)

echo.
echo   All firmware files copied successfully.
echo.

REM --------------------------------------------------------------------------
REM Post-install verification
REM --------------------------------------------------------------------------
echo [STEP 3/3] Verifying installation...
echo.

set "VERIFY_PASS=0"
set "VERIFY_FAIL=0"

for %%F in (start4.elf fixup4.dat config.txt kernel8.img bcm2711-rpi-4-b.dtb boot.scr u-boot.bin) do (
    if exist "!DRIVE_LETTER!\%%F" (
        echo   OK: %%F
        set /a "VERIFY_PASS+=1"
    ) else (
        echo   MISSING: %%F
        set /a "VERIFY_FAIL+=1"
    )
)

REM Check config.txt contents
findstr /C:"arm_64bit=1" "!DRIVE_LETTER!\config.txt" >nul 2>&1
if errorlevel 1 (
    echo   WARNING: config.txt may be missing arm_64bit=1
    set /a "VERIFY_FAIL+=1"
) else (
    echo   OK: config.txt arm_64bit=1
    set /a "VERIFY_PASS+=1"
)

findstr /C:"kernel=u-boot.bin" "!DRIVE_LETTER!\config.txt" >nul 2>&1
if errorlevel 1 (
    echo   WARNING: config.txt may be missing kernel=u-boot.bin
    set /a "VERIFY_FAIL+=1"
) else (
    echo   OK: config.txt kernel=u-boot.bin
    set /a "VERIFY_PASS+=1"
)

echo.
if "!VERIFY_FAIL!"=="0" (
    echo   Verification: !VERIFY_PASS!/!VERIFY_PASS! checks passed
) else (
    echo   Verification: !VERIFY_FAIL! check(s) FAILED
)
echo.

REM --------------------------------------------------------------------------
REM Show SD card contents
REM --------------------------------------------------------------------------
echo Files on !DRIVE_LETTER!\:
echo.
dir "!DRIVE_LETTER!\" /A:-D 2>nul
echo.

REM --------------------------------------------------------------------------
REM Completion banner
REM --------------------------------------------------------------------------
echo ========================================
echo  JARVIS INSTALLATION COMPLETE
echo ========================================
echo.
echo Next steps:
echo.
echo   1. Safely eject the SD card:
echo      Right-click the drive in File Explorer and select "Eject".
echo.
echo   2. Insert the SD card into the Raspberry Pi 4.
echo.
echo   3. Connect USB-serial adapter:
echo        Pi GPIO14 (TXD) --- USB-Serial RXD
echo        Pi GPIO15 (RXD) --- USB-Serial TXD
echo        Pi GND          --- USB-Serial GND
echo.
echo   4. Open a serial console (115200 baud, 8N1):
echo.
echo      PuTTY:
echo        Connection type: Serial
echo        Serial line:     COM3  (check Device Manager for correct port)
echo        Speed:           115200
echo        Data bits: 8, Stop bits: 1, Parity: None, Flow control: None
echo.
echo      Tera Term:
echo        File ^> New connection ^> Serial
echo        Port: COM3, Baud: 115200, Data: 8, Stop: 1, Parity: none
echo.
echo      Windows Terminal (PowerShell):
echo        Install: winget install Microsoft.WindowsTerminal
echo        Serial plugin or use: plink -serial COM3 -sercfg 115200,8,n,1,N
echo.
echo   5. Power on the Pi 4.
echo.
echo   6. You should see the JARVIS banner on the serial console.
echo.
echo   7. To start the AI host-side bridge (from WSL or Python):
echo      python phase2\src\ai\uart_ipc_client.py --port COM3
echo.
echo ========================================
echo.

exit /b 0
