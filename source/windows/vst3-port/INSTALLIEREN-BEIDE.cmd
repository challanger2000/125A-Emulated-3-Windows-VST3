@echo off
setlocal EnableExtensions
cd /d "%~dp0"
title 125A Emulator III - BEIDE VST3 installieren
echo ======================================================
echo  E-MU Emulator III + 125A Vintage Sampler III
echo  Installer mit Original-ROM und EIII OS
echo ======================================================
echo.
fltmc >nul 2>&1
if errorlevel 1 (
 echo Windows-Administratorrechte werden angefordert...
 powershell.exe -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs -WorkingDirectory '%~dp0'"
 exit /b %ERRORLEVEL%
)
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Install-Both-Complete.ps1"
set "RET=%ERRORLEVEL%"
echo.
if "%RET%"=="0" (
 echo [OK] Beide vollstaendigen Plugins wurden installiert.
) else (
 echo [FEHLER] Installation nicht abgeschlossen. Exitcode %RET%.
 echo Die bisherigen Plugins werden nicht bewusst veraendert, bevor
 echo beide neuen Bundle-Kopien und die Originalmedien geprueft sind.
)
echo.
pause
exit /b %RET%
