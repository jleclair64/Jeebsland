# Window fix: brief for ChatGPT / Codex

Paste this to Codex, with Codex started in `C:\Users\johnl\Documents\Unreal Projects\JeebsAgents\codex`.

---

Work only in `C:\Users\johnl\Documents\Unreal Projects\JeebsAgents\codex` (a git worktree on branch `agent/codex`). Commit there and push only that branch (`git push origin agent/codex`). Never touch `main` or the folder `C:\Users\johnl\Documents\Unreal Projects\Jeebsland`, and never touch the other folders in `JeebsAgents`: other agents are working there. Read `AGENTS.md` / `CLAUDE.md` first. Never open a visible Unreal window: every test run uses `-RenderOffScreen -Multiprocess -LiveCoding=false` and a time limit. Wrap every Unreal build or run in `bash Tools/uqueue.sh build|run codex` ... `release codex`. Builds use Build.bat with `-NoHotReloadFromIDE`.

**Goal:** every window on the TownLab buildings (Oak Hill's houses, store, town hall, potion shop, church, bank, smith, stalls) is fully dark by day and evenly lit amber at dusk, with no smudges. The art style is "Breath of the Wild meets Overwatch" medieval town.

**What's there now:** `Tools/window_glass.py` lays one flat glass card per window 0.5 cm in front of everything in the model around it, with bars painted on and a vertex-colour glass mark that `M_TL_Building` lights. Read `Docs/TownLab/Buildings.md`, the window lines in `Docs/ReviewLessons.md`, `Tools/window_glass.py`, `Tools/townlab_panes.py` and `Tools/townlab_import.py`.

**The owner's complaints** (pictures in `Saved/OwnerWindows/`, 19 to 23 = 1 to 5):
1, 2. The panes are not flush against the window and seem to float in front of it.
3. A window should be one piece with dividers, not a bunch of shapes.
4. A window needs dividers so it isn't a big plain lit square.
5. The glass doesn't follow the frame's shape (an arched top), and it isn't divided properly.

The owner also wants something better than cards sitting in front of the windows. Find your own best solution: windows that look like real glass sitting in the frame, fitted to each opening's shape, flush in the recess, with even dividers on every window. Keep the one building material (no extra draw calls).

At the end, save half-size before/after pictures of the five spots and two whole buildings at dusk and noon in `Saved/WindowReviewCodex/`, and write `Saved/codex_report.md` saying what you did and why.
