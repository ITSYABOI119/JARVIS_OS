@echo off
rem One-click wrapper for start_receiver.ps1 -- keep this in the same folder as the .ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0start_receiver.ps1" %*
if errorlevel 1 pause
