# Architecture

## Overview

CODMSpawnSnack is a background Windows agent that watches a CODM emulator window, detects the current game state, and switches foreground focus to/from a short-video app.

```
┌─────────────────┐      ┌─────────────────┐      ┌─────────────────┐
│  WGC Capture    │ ───> │  Frame Analysis │ ───> │  State Machine  │
│  (emulator HWND)│      │  HUD + Result   │      │  Decide switch  │
└─────────────────┘      └─────────────────┘      └─────────────────┘
                                                            │
                                                            ▼
                                    ┌─────────────────┐      ┌─────────────────┐
                                    │  FocusController│ <──> │  IVideoTarget   │
                                    │  SetForeground  │      │  Chrome+Douyin  │
                                    └─────────────────┘      └─────────────────┘
```

## Core modules

### 1. Capture (`src/capture/screen_capture`)

Uses **Windows Graphics Capture (WGC)** to capture the emulator window in the background. The current implementation uses a **BitBlt fallback** so the project can be built and tested while the WGC/WinRT integration is finalized. Key design choices (target):

- Captures by `HWND` via `CreateForWindow` (Win32 interop). The emulator must be a window, not full-screen-exclusive, and **must not be minimized** (WGC cannot capture minimized windows).
- Produces `ID3D11Texture2D` frames; copies them to CPU memory, then wraps in an `cv::Mat` for analysis.
- Runs on its own thread; frames are pushed to the analysis queue with a configurable frame rate.

Current code: `src/capture/screen_capture.cpp` implements the BitBlt fallback. WGC migration will live in the same class/interface.

### 2. Detection (`src/detection`)

Two detectors run on every frame.

#### HUD detector (`hud_detector`)

- Looks at the bottom-left ROI for the **segmented health bar** (the stable structural part: borders and dividers, not the filled color).
- Uses OpenCV `cv::matchTemplate` with a small, user-calibrated template. The template is stored in the repo only as an example; each user calibrates on their own HUD.
- Returns `HudPresence::Present` / `Absent` / `Unknown` and a confidence score.

Why match the bar structure instead of the filled color?

- The filled portion changes as the player takes damage; borders and dividers stay the same.
- The CODM logo stays visible when the player is dead, so it cannot be part of the template.

#### Result detector (`result_detector`)

- Looks at the **center vertical band** covering both the small round-end text and the large match-end text.
- Uses **Windows.Media.Ocr** (offline, ships with Windows) to detect `胜利 / 失败 / VICTORY / DEFEAT`.
- Runs at a lower frequency with debouncing (must see the text for N consecutive frames) to avoid false positives.

Current code: the result detector is **stubbed** and returns no matches. It is the next integration target once the MSVC + Windows SDK toolchain is available.

### 3. State machine (`src/state/state_machine`)

The state machine keeps three pieces of memory:

- `hudSeen`: has the bottom-left HUD appeared at least once in the current round? Acts as a gate: only when `hudSeen == true` can "HUD disappeared" be interpreted as "player died".
- `onVideo`: are we currently in the short-video app?
- `roundEnded`: did we just see the result text?

Transitions:

| From state | Trigger | Action | To state |
|---|---|---|---|
| In-game | HUD disappears while `hudSeen==true` | Switch to video app, pause game input | OnVideo |
| OnVideo | HUD reappears | Switch back to game, pause video | In-game |
| OnVideo/In-game | Result text confirmed | Switch back to game, pause video, reset `hudSeen` | In-game (waiting for next round) |
| Any | HUD appears while `hudSeen==false` | Set `hudSeen=true` | In-game |

This design naturally handles:

- Lobby/waiting screens (no HUD → no `hudSeen` → no false switch to video).
- End-of-round moments where HUD is gone but the player is not dead.
- Respawn in Bomb Mode (HUD returns → switch back).

### 4. Focus control (`src/focus/focus_controller`)

Windows `SetForegroundWindow` is famously finicky. The implementation uses the standard workarounds:

- `AllowSetForegroundWindow` if available.
- `AttachThreadInput` dance when the target window belongs to another thread/process.
- A small foreground-grab window or simulated user input as a last resort.

The emulator window is kept in the background (not minimized) so WGC keeps capturing. Only foreground focus is moved.

### 5. Video target (`src/video`)

`IVideoTarget` is a small interface:

```cpp
class IVideoTarget {
public:
    virtual bool Launch() = 0;
    virtual bool Pause() = 0;
    virtual bool Resume() = 0;
    virtual bool IsRunning() const = 0;
    virtual ~IVideoTarget() = default;
};
```

Default implementation: `ChromeDouyinTarget`. It:

1. Finds an existing Chrome window or launches one with `chrome --app=https://www.douyin.com`.
2. Sends `Space` (or uses Chrome DevTools Protocol) to pause/resume the current video.
3. Does not navigate away, so the same video stays loaded at the same progress.

Future targets (Bilibili, Kuaishou, a native app) can be added by implementing the same interface.

## Configuration

Everything is driven by `config.json` (copy from `config.example.json`). All coordinates are **screen ratios**, so the same config works across different resolutions and DPIs.

Key sections:

- `window` — how to find the emulator window and capture settings.
- `hud` — ROI and template for the bottom-left health bar.
- `result` — ROI and keywords for the center result text.
- `state_machine` — debounce frames.
- `video` — which `IVideoTarget` to use.
- `focus` — focus-switching delays.

## Data flow

1. Capture thread pushes frames.
2. Analysis thread picks frames, optionally downscales them, and runs HUD + Result detectors.
3. Detection results are sent to the state machine on the same thread.
4. If a state transition requires focus/video action, the state machine posts a task to the main/UI thread.
5. UI thread performs the foreground switch and video pause/resume.

## Why no Battle Royale in this version

Battle Royale has a different bottom-left HUD (smaller health/armor bars, plus a "parachute" HUD with only teammate numbers). Adding it means either more templates or a trained detector, plus a parachute-phase toggle. The current scope is intentionally limited to Bomb Mode so the MVP is small and verifiable.
