@echo off
setlocal EnableExtensions
title 125A E-MU Emulator III - TEST installieren
set "SOURCE=%~dp0EIII125A.vst3"
set "TARGET=%CommonProgramFiles%\VST3\125A E-MU Emulator III.vst3"
if not exist "%SOURCE%\Contents\x86_64-win\EIII125A.vst3" (
  echo FEHLER: EIII125A.vst3 fehlt neben INSTALLIEREN.cmd.
  pause
  exit /b 2
)
fltmc >nul 2>&1
if errorlevel 1 (
  echo Administratorrechte werden angefordert...
  powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
  exit /b %ERRORLEVEL%
)
echo Studio One muss vor der Installation geschlossen sein.
echo Ziel: "%TARGET%"
robocopy "%SOURCE%" "%TARGET%" /E /R:1 /W:1 >nul
set "RC=%ERRORLEVEL%"
if %RC% GEQ 8 (
  echo FEHLER: Kopieren fehlgeschlagen. Robocopy-Code %RC%.
  pause
  exit /b 1
)
if not exist "%TARGET%\Contents\x86_64-win\EIII125A.vst3" (
  echo FEHLER: Ziel-VST3 fehlt nach Kopieren.
  pause
  exit /b 1
)
echo Installation abgeschlossen. Studio One jetzt starten.
echo Original-ROMs, Festplatten-Images und Nutzerdaten wurden nicht veraendert.
pause
exit /b 0
