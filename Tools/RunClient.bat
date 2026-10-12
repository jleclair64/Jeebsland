@echo off
rem Opens the game in a window, on the starting screen (log in or make an account, then pick a world).
rem Run it twice for two players. Optional: RunClient.bat <server address>  (default: this PC)
set ADDR=%1
if "%ADDR%"=="" set ADDR=127.0.0.1
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "%~dp0..\Jeebsland.uproject" -game -windowed -ResX=1280 -ResY=720 -JeebServer=%ADDR%
