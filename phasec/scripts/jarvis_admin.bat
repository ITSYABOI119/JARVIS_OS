@echo off
rem One-click wrapper for jarvis_admin.ps1 -- keep this in the same folder as the .ps1
rem Bare invocation opens the chooser; explicit flags (-Check/-Rekey/-Rollback) pass straight through.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0jarvis_admin.ps1" %*
rem Preserve the script's exit code ACROSS the pause. `pause` resets ERRORLEVEL to 0,
rem so the plain `if errorlevel 1 pause` shape reports SUCCESS for every failure it
rem paused on -- measured: the .ps1 exited 4 on the redirected-stdin refusal and the
rem wrapper still returned 0. The pause-on-failure behaviour is kept; only the
rem code-swallowing is fixed.
set "JADMIN_RC=%ERRORLEVEL%"
if not "%JADMIN_RC%"=="0" pause
exit /b %JADMIN_RC%
