@echo off
setlocal
cd /d "%~dp0"
title 125A Emulator III - D0 Disketten und Recall Test
echo ==========================================================
echo   125A EMULATOR III - D0 REAL WD1772 / RECALL-TEST
echo   KEINE ECHTE DISKETTE ODER ROMS NOTWENDIG
echo ==========================================================
echo.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Run-D0-Tests.ps1"
set "CODE=%ERRORLEVEL%"
echo.
if "%CODE%"=="0" (
 echo [PASS] Synthetischer WD1772 IMG/HFE- und Mid-Sector-Recall-Test.
) else (
 echo [FAIL] Pruefung fehlgeschlagen, Exitcode %CODE%. 
)
echo.
echo Die Ergebnisse stehen in D0-Console.txt im selben Ordner.
echo Bitte D0-Console.txt zur unabhaengigen Auswertung teilen.
echo.
pause
exit /b %CODE%
