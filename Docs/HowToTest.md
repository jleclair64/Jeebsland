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
