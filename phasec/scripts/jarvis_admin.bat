@echo off
rem One-click wrapper for jarvis_admin.ps1 -- keep this in the same folder as the .ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0jarvis_admin.ps1" %*
if errorlevel 1 pause
