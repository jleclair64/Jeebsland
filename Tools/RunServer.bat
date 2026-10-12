@echo off
rem Starts a world server (a console window with the log). Close the window to stop it.
rem   RunServer.bat      World 1 (port 7777)
rem   RunServer.bat 2    World 2 (port 7778)
rem Each world also answers "how many players?" on its port + 100 (7877, 7878) for the starting screen.
rem Accounts and characters are kept in Saved\Accounts and Saved\Characters (shared by both worlds on this PC).
set WORLD=%1
if "%WORLD%"=="" set WORLD=1
set /a PORT=7776+%WORLD%
title Jeebsland World %WORLD% (port %PORT%)
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "%~dp0..\Jeebsland.uproject" -server -log -port=%PORT%
