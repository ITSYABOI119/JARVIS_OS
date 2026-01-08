echo
echo ==================================
echo   JARVIS AI-OS Boot Script
echo   U-Boot 2026.01 / BCM2711
echo ==================================
echo
mmc dev 0
mmc rescan
echo Loading kernel8.img...
fatload mmc 0:1 0x00080000 kernel8.img
echo Booting JARVIS seL4...
go 0x00080000
