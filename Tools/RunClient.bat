@echo off
rem Joins a server as a player, in a window. Run it twice for two players. Optional: RunClient.bat <server address>
set ADDR=%1
if "%ADDR%"=="" set ADDR=127.0.0.1
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "%~dp0..\Jeebsland.uproject" %ADDR% -game -windowed -ResX=1280 -ResY=720
