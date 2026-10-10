@echo off
echo Close the Unreal editor first.
echo.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Update.ps1"
echo.
echo Building Jeebsland...
call "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" JeebslandEditor Win64 Development "C:\Users\johnl\Documents\Unreal Projects\Jeebsland\Jeebsland.uproject" -waitmutex > "%~dp0build_log.txt" 2>&1
type "%~dp0build_log.txt"
echo.
echo Done. Look above for "Result: Succeeded".
pause
