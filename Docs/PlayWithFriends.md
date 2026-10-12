# Playing Jeebsland with friends (Tailscale)

Your PC hosts the worlds. Tailscale makes a private, encrypted link between your PC and your friends' PCs, so
nothing is opened to the open internet (it also keeps passwords safe on the way to the server).

## Once, on your PC (the host)
1. Install Tailscale from tailscale.com and sign in.
2. Right-click `Tools\OpenFirewall.bat` > Run as administrator (lets the game ports through Windows Firewall).
3. Package the game (see CLAUDE.md, "Packaging"), then zip `Packaged\Windows` **without the .pdb file** and send
   the zip to your friends.
4. Share your PC with each friend: Tailscale admin console (login.tailscale.com) > Machines > your PC > "..." >
   Share, and send them the invite link. (They only see your PC, nothing else of yours.)

## Once, on each friend's PC
1. Install Tailscale, sign in, and accept your share invite.
2. Unzip the game.

## Every time you play
1. You: double-click `Tools\HostWorlds.bat`. It starts World 1 and World 2 and shows your Tailscale address
   (it starts with `100.`). Wait until both windows say "Jeebsland world up".
2. Friends: open `Jeebsland.exe`. On the starting screen, type your `100.x.x.x` address in **Server address**
   (it is remembered), choose **New account** the first time, pick a world and press **Play**.
   The world list shows player counts once it can reach your PC; "Offline" means it can't (check Tailscale is on
   and the worlds are running).
3. You can play too: run the packaged game (or `Tools\RunClient.bat`) with address `127.0.0.1`.

Accounts and characters live on your PC in `Saved\Accounts` and `Saved\Characters`. Closing a world window saves
everyone (they are also saved every minute).
