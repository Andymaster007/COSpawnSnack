> 🌐 [中文](README.md) · English

# COSpawnSnack (CO摸鱼管理器)

> A small Windows utility: the moment I die in-game, it automatically switches to a preset webpage, and switches back when the round ends.

## The Idea

A friend and I were playing together. After he died, he'd switch away on his computer to watch videos and got so caught up in it that he forgot to switch back — by the time he snapped out of it, the match was over and he'd been kicked for idling.

Since people can't help but slack off during the spectating downtime anyway, why not let the computer manage that slack time: **switch over to slack off when it's time to, switch back when it's time to return** — so you never miss the videos, and never get kicked for forgetting to switch back. That's how COSpawnSnack was born.

## How It Works

The whole process reads no memory and injects nothing into the game — it only "watches the screen":

1. **Capture** — Uses **Windows Graphics Capture (WGC)** to capture the game window in the background (automatically falls back to BitBlt on older machines).
2. **Detect death** — Runs **Windows.Media.Ocr** on the respawn prompt bar at the lower-center of the screen; reading "你将在下一回合重生" (You will respawn next round) is treated as death.
3. **Detect round end** — Runs OCR on the center of the screen; reading "胜利 / 战败" (Victory / Defeat) is treated as round or match end.
4. **Switch + resume** — On death it switches to the short-video page and keeps playing; on respawn / match end it switches back to the game and pauses the video at its exact position. Pause / resume reads the real playback state via system media control (GSMTC) and operates precisely, so it never fights with your manual pause.

## Key Features

- **Pure visual detection** — Only external screen recognition; reads and modifies no game memory, avoiding injection-style anti-cheat risks.
- **No lost video progress** — When switching back to the game the video is merely paused, so when you return it's the same clip at the same timestamp.
- **Browser adaptation** — Prefers the browser you select, and automatically falls back to another installed supported browser if that one isn't present (Chrome first, then Edge).
- **Dual hotkeys** — `F8` to start / stop monitoring at any time; if `F8` is taken by another program, `Ctrl+F8` is available as a backup.
- **Startup self-check** — On launch it checks the Windows version and available browsers, and shows a Chinese prompt if requirements aren't met.
- **Single-file exe** — Statically linked with an embedded WebView2 UI; just double-click to run.

## Tech Stack

C++20 · MSVC · CMake · vcpkg · OpenCV · Win32 · Windows Graphics Capture · Windows.Media.Ocr · WebView2

## Requirements

- Windows 10 1803 (build 17134) or later / Windows 11.
- Microsoft Edge WebView2 Runtime required (usually preinstalled on Windows 11 and newer Windows 10).
- Currently only Chrome or Edge browsers are supported.

## Usage Notes

- Detection is OCR-based, so please set the **in-game control opacity to 100%**, otherwise recognition may fail.
- Currently **only Search & Destroy mode** is supported; Battle Royale is out of scope for this version.
- Press `F8` or `Ctrl+F8` to start / stop monitoring.

## Version

**v1.2** — Added a prominent warning with install guidance when the Chinese OCR language pack is missing; added a toast when the browser fails to launch; removed dead code.

Full changelog: [Releases](https://github.com/Andymaster007/COSpawnSnack/releases).

## License

MIT — see `LICENSE`.

---

## Disclaimer

COSpawnSnack (CO摸鱼管理器) is a personal learning and productivity aid based on Windows OCR image recognition. It is not an official product of Call of Duty: Mobile (CODM), and has no commercial authorization or partnership. The software uses purely external visual detection logic — it reads and modifies no game memory and does not affect game balance. However, because anti-cheat systems (e.g. Tencent ACE) keep evolving, the author cannot legally guarantee "absolute no-ban". Users assume all potential risks of using this software. All game names, trademarks, and copyrights mentioned herein belong to their respective owners (Activision / Tencent, etc.).
