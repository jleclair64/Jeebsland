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

For working on attacks and movement: in the editor's Content Browser double-click **MoveLab**, then press Play. It loads in seconds: a flat floor with a line every metre (a brighter one every 5 m) a still sparring knight 3 m ahead (he never fights back or falls; his health fills again 3 s after your last hit), a second one 3.5 m to his right (as you look from the start) who always holds his shield up, and a straw dummy 3 m to each side. Both knights face where you start, so walk round them to try other angles. Hit boxes and hurt boxes show there, Smash style: yellow capsules are where a body can be hit, red spheres are the sword's hit boxes during the frames a swing can land (each drawn from where it was the frame before). Press **H** to hide or show them (in the real game they start hidden; H shows them there too). It is the same knight as in the game, so anything tuned here is already in the real game. To go back, double-click **Field**.

**Sword smash attack:** hold the left mouse button (a quick or slow click is still the normal jab). After about a quarter of a second the knight stops at hit 1's pull-back and charges, walking at half speed and trembling more as it builds. It is full after 1 s, stays full for 2 s more, then goes by itself. Let go any time: hit 1's swipe, from about 1x (uncharged) to 2.5x a normal sword hit (full). After the swipe he always pauses for the same moment (end lag): he can't move, block, roll or jump. A click in the last moment of the pause comes out as soon as it ends. Try a full charge on the sparring knight, then an early release, and compare the numbers.

**Shields:** a raised shield guards a thin curved band in front of the knight, drawn see-through blue, about as far out as the shield itself (60-80 cm from his centre), reaching 45 degrees to either side of straight ahead. From the front it is an arch: straight sides from his feet to his belt, rounding up to the top of his helm in the middle (lower when he crouches). Whatever your sword's red boxes meet first along their path wins: the wedge first is a block; his body first (from the side or behind, the blade reaching his yellow boxes before it crosses the wedge) is a clean hit that lands in full, with no bounce and no end lag, even if the blade goes on through the wedge afterwards. A blocked blow never hurts him: the shield takes all of it (the blue number, the same as the hit would have done) out of its guard. The blow that empties the guard breaks it: his shield drops and stays down until the guard is full again (3 s after your last hit here; 6 s for the pen knight). Yours works the same way. Hitting a shield bounces your sword back: the swing is over, you are knocked back a step and stuck (no moving, swinging, blocking, rolling or jumping) for 0.45 s after a jab hit, 0.8 s after a smash attack. A click right at the end comes out as soon as you can move.

**Kick (scroll the wheel up):** rock, paper, scissors: the sword is stopped by a raised shield, the kick goes through it. The knight turns his whole body to his left and drives his right leg straight out at the target, level at hip height, toes turned to his left (like Ken's forward smash kick), fists up; it comes out fast and its hit box is just ahead of the sole; then the leg folds back and sets down. From the moment the kick starts until the leg is down he can't move, turn, roll or jump (end lag). It does a little damage. On a raised shield it knocks the knight down onto his back. The guarding sparring knight gets up a different way each time (straight up, then rolling to his left, then to his right) and walks back to his spot. Knocked down yourself (by another player's kick in MoveLab): once you land, A or D rolls you away that way, and W, S, Space or a click gets you straight up; after 2 s you get up by yourself, and getting up or rolling away can't be hit for a moment. Strafe mode is now on from the start (wheel click or X still turns it off).

**Weapon bar:** keys 1 to 5 take the weapon in that slot (the number shows in each slot's corner); R steps to the next one, left to right, round to the first again (empty slots are skipped). With the cursor out (Tab), clicking a slot takes it in hand too. The mouse wheel no longer changes weapons (wheel up kicks).
