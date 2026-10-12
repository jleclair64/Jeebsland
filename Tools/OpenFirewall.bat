@echo off
rem Lets friends reach the worlds on this PC. Run once: right-click > "Run as administrator".
rem Opens only the game ports (UDP 7777-7778) and the player-count ports (TCP 7877-7878).
netsh advfirewall firewall add rule name="Jeebsland worlds" dir=in action=allow protocol=UDP localport=7777-7778
netsh advfirewall firewall add rule name="Jeebsland player counts" dir=in action=allow protocol=TCP localport=7877-7878
pause
