@echo off
setlocal
set "HOTDEX_PS=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
if exist "%SystemRoot%\Sysnative\WindowsPowerShell\v1.0\powershell.exe" set "HOTDEX_PS=%SystemRoot%\Sysnative\WindowsPowerShell\v1.0\powershell.exe"
start "" "%HOTDEX_PS%" -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File "%~dp0hotdex-launcher.ps1"
exit /b 0
