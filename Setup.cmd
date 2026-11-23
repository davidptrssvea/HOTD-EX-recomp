@echo off
setlocal
set "SETUP_PS=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
if defined PROCESSOR_ARCHITEW6432 set "SETUP_PS=%SystemRoot%\Sysnative\WindowsPowerShell\v1.0\powershell.exe"
"%SETUP_PS%" -NoProfile -ExecutionPolicy Bypass -STA -File "%~dp0setup.ps1" %*
set "SETUP_RESULT=%ERRORLEVEL%"
if not "%SETUP_RESULT%"=="0" echo Setup failed. Please read the error above.
pause
exit /b %SETUP_RESULT%
