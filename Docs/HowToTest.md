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

For working on attacks and movement: in the editor's Content Browser double-click **MoveLab**, then press Play. It loads in seconds: a flat floor with a line every metre (a brighter one every 5 m) a still sparring knight 3 m ahead (he never fights back or falls; his health fills again 3 s after your last hit) and a straw dummy 3 m to each side. Hit boxes and hurt boxes show there, Smash style: yellow capsules are where a body can be hit, red spheres are the sword's hit boxes during the frames a swing can land (each drawn from where it was the frame before). Press **H** to hide or show them (in the real game they start hidden; H shows them there too). It is the same knight as in the game, so anything tuned here is already in the real game. To go back, double-click **Field**.

**Sword smash attack:** hold the left mouse button (a quick or slow click is still the normal jab). After about a quarter of a second the knight stops at hit 1's pull-back and charges, walking at half speed and trembling more as it builds. It is full after 1 s, stays full for 2 s more, then goes by itself. Let go any time: hit 1's swipe, from about 1x (uncharged) to 2.5x a normal sword hit (full). After the swipe he always pauses for the same moment (end lag): he can't move, block, roll or jump. A click in the last moment of the pause comes out as soon as it ends. Try a full charge on the sparring knight, then an early release, and compare the numbers.
