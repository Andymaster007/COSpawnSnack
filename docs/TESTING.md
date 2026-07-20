# Testing / diagnostic guide

You can test the detection logic **without letting the program switch focus or launch Chrome**. Both the C++ tool and the Python harness have a diagnostic mode that only captures the game window, runs the HUD/result detectors, and logs what they see.

## Option A: C++ diagnostic mode (the real program)

Build in CLion, then run the executable from the **project root** (`D:\Github\CODMSpawnSnack`) so relative paths like `assets/templates/...` resolve:

```powershell
cd D:\Github\CODMSpawnSnack
.\cmake-build-debug-visual-studio\bin\CODMSpawnSnack.exe --diagnose
# or with auto-stop after 180 seconds:
.\cmake-build-debug-visual-studio\bin\CODMSpawnSnack.exe --diagnose --timeout 180
```

Or set `"diagnostic_mode": true` in `config.json`.

What happens:
- A console window pops up (or attaches to the terminal you launched from).
- The program finds the game window, starts WGC capture, and prints every state change:
  - `HUD Present -> Absent conf=0.27` — you died.
  - `HUD Absent -> Present conf=0.98` — you respawned.
  - `RESULT keyword='胜利' conf=1.0` — a round or match ended with a win.
  - `RESULT keyword='战败' conf=1.0` — defeat.
- It also writes a log file `csn-diagnose.log` with a `raw_text` column showing the exact OCR output before keyword matching.
- It periodically saves `diag_crops/hud_*.png` and `diag_crops/result_*.png` so you can see exactly what the detectors are looking at.
- **No focus switching, no Chrome launch.**

Stop it with `Ctrl+C` or press `q` (and `Enter` if a new console was allocated). If you pass `--timeout <sec>` it stops automatically.

> **Note:** On some managed PCs, unsigned C++ executables are blocked by Windows Defender Application Control / Device Guard. If you get a "blocked by your organization" message, use Option B.

## Option B: Python diagnostic harness (no compilation needed)

Use this if the C++ `.exe` is blocked, or if you just want a quick capture-and-detect test.

Requires the same Python venv used during calibration (`C:\Users\Andy\.workbuddy\binaries\python\envs\calib`):

```powershell
cd D:\Github\CODMSpawnSnack
C:\Users\Andy\.workbuddy\binaries\python\envs\calib\Scripts\python.exe tools\diag_live.py --seconds 120
```

It does the same thing as the C++ diagnostic:
- finds the `使命召唤手游` window,
- captures it with BitBlt,
- runs the same resolution-independent HUD template matching,
- runs `Windows.Media.Ocr` on the same result ROI,
- logs transitions and heartbeats to `diag_live.log`.

Stop with `Ctrl+C`.

## Recommended match flow

To exercise every detection path in one test, try to produce:

1. **Alive** — run around with the HUD visible.
2. **Death** — let yourself get killed. Log should show `HUD Present -> Absent`.
3. **Revive** — next round starts, HUD reappears: `HUD Absent -> Present`.
4. **Round win** — see the blue/yellow "胜利" banner: `RESULT keyword='胜利'`.
5. **Round lose** — see "战败": `RESULT keyword='战败'`.
6. **Match end** — final big "胜利" or "战败" should also trigger a result event.

A single public match usually covers most of these automatically.

## Interpreting the log

Example of a good test:

```text
19:17:50 HUD Present -> Absent conf=0.27     # died
19:17:54 RESULT keyword='胜利' conf=1.0       # round won while dead
19:18:01 HUD Absent -> Present conf=0.99     # respawned
19:18:45 HUD Present -> Absent conf=0.31     # died again
19:18:48 RESULT keyword='战败' conf=1.0       # round lost
19:19:10 HUD Absent -> Present conf=1.00     # next round
```

If the log never shows `HUD Present -> Absent` when you die, the HUD template is not matching — check `assets/templates/hud_bar_segments_full.png` and `hud_bar_segments_low.png` or lower `hud.match_threshold`. You can also inspect `diag_crops/hud_*.png` to see what the detector sees at the moment it failed.

If the log never shows `RESULT ...` when the match/round ends, the OCR is missing the text — check the `result` ROI or lower `result.confidence_threshold`.

## Static OCR check

To verify the OCR engine recognizes your screenshots without running a live match:

```powershell
C:\Users\Andy\.workbuddy\binaries\python\envs\calib\Scripts\python.exe tools\test_ocr_static.py
```

It will OCR the result-text ROI from `失败结算.png`, `对局胜利（大胜利）.png`, and `回合胜利（小胜利）.png` if those files are still in `C:\Users\Andy\Pictures\Screenshots`.
