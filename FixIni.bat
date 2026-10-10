@echo off
echo Close the Unreal editor first.
powershell -NoProfile -Command "$p='C:\Users\johnl\Documents\Unreal Projects\Jeebsland\Config\DefaultEngine.ini'; Copy-Item $p ($p + '.bak') -Force; (Get-Content $p) -replace '^GlobalDefaultGameMode=.*','GlobalDefaultGameMode=/Script/Jeebsland.KnightGameMode' | Set-Content $p -Encoding ASCII; Select-String -Path $p -Pattern 'GlobalDefaultGameMode'"
echo.
echo Done. The line above should end with KnightGameMode. A backup was saved as DefaultEngine.ini.bak
pause
