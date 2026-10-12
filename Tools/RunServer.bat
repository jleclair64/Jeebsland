@echo off
rem Starts a local dedicated server (a console window with the log). Close the window to stop it.
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "%~dp0..\Jeebsland.uproject" -server -log
