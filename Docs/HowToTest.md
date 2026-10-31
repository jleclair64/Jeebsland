# How to test Jeebsland

**Rule of thumb: the editor's Play button never needs the worlds. The .bat files are the real thing.**

| I want to... | Do this |
|---|---|
| Quickly try a change, alone | Editor: green Play ▶ (any mode). Net Mode "Play Offline". |
| Try it online, quickly (2 players, no setup) | Editor: ⋮ next to Play > Net Mode "Play As Client", Number of Players 2 > ▶. Logs in as Tester 1 / Tester 2. |
| The full game: starting screen, accounts, both worlds | Close the editor. `Tools\HostWorlds.bat`, wait for "Jeebsland world up", then `Tools\RunClient.bat` (twice for 2 players). Address `127.0.0.1`. |
| See exactly what friends get | Same as above, but open `Packaged\Windows\Jeebsland.exe` instead of RunClient (only right after repackaging). |

In the editor's preview window (New Editor Window / Selected Viewport), Escape stops Play and it can't go full screen: use **O**
for the map / settings screen there. For full screen with a working Escape, pick **Standalone Game** (⋮ next to Play > Modes):
it plays offline straight away. (Alt is the roll key, so Alt+Enter just rolls.) To change the editor's Stop key: Editor
Preferences > Keyboard Shortcuts > search "Stop" > set it to Shift+Escape.

## Chat and the radar potion (2026-10-30)

- **Chat:** press **Enter**, type, Enter again to send (Escape cancels). Players within 35 m hear it and see it over your head. `/w message` tells the whole world. `/mute Name`, `/unmute Name`, `/mutelist` hide players on your machine. Bad words turn into asterisks (the server does it). Try it with "Play As Client", 2 players.
- **Owner tools:** make `Saved/Moderation.txt` on the server's machine with lines like `mute Sir Jeeb` (they can play but nobody hears them) or `ban Sir Jeeb` (cannot log in). It is re-read every few seconds, no restart. Lines starting with `#` are comments.
- **Radar potion:** the practice pack has three (the store sells it for 60 ingots). Put it in the potion slot (drag it there) and press Q: enemy knights and other players near you show as red dots on the minimap for 2 minutes. Ready again after 3 minutes.

## MoveLab: a small test area for moves (2026-10-31)

For working on attacks and movement: in the editor's Content Browser double-click **MoveLab**, then press Play. It loads in seconds: a flat floor with a line every metre (a brighter one every 5 m) and two straw training dummies that stand still (3 m ahead and 3 m to your right). It is the same knight as in the game, so anything tuned here is already in the real game. To go back, double-click **Field**.
