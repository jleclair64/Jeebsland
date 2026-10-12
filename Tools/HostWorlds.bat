@echo off
rem Hosts both worlds on this PC for friends (each opens its own console window; close a window to stop that world).
rem Friends connect over Tailscale: see Docs\PlayWithFriends.md
start "Jeebsland World 1" "%~dp0RunServer.bat" 1
start "Jeebsland World 2" "%~dp0RunServer.bat" 2
echo Both worlds are starting. Your Tailscale address (give this to friends):
tailscale ip -4 2>nul || echo   (open the Tailscale app to see it: it starts with 100.)
pause
