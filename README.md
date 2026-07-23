# CODMSpawnSnack

> Windows utility that auto-switches me to short videos while I'm waiting to respawn in CODM Bomb Mode.

## Why I built this

In CODM Bomb Mode, when I die, I stare at the killcam or wait for the next round. I wanted the PC to automatically switch to a short-video app (Douyin, Bilibili, Kuaishou...) and keep the same video paused at the same spot. When I respawn, or the round/match ends, it switches back. No injection, no memory reading - just visual capture and a small state machine.

## How it works

1. **Capture** the emulator window in the background using **Windows Graphics Capture (WGC)** (currently using a BitBlt fallback until the WGC/WinRT integration is finalized).
2. **Detect the bottom-left HUD** (health bar + name) with **OpenCV template matching**.
3. **Detect the center result text** "VICTORY/DEFEAT" / "胜利/失败" with **Windows.Media.Ocr** (currently stubbed; the next milestone is to integrate the real WinRT OCR).
4. A small **state machine** decides when to switch to the video app and when to switch back, pausing and resuming the video so I always land on the same clip at the same progress.

## Architecture

- `src/capture` - window capture (BitBlt fallback; WGC migration planned)
- `src/detection` - HUD detector (OpenCV) + result detector (OCR, currently stubbed)
- `src/state` - state machine with `hudSeen` gating
- `src/focus` - foreground-window switching
- `src/video` - pluggable short-video targets (default: Chrome -> Douyin)
- `src/ui` - settings dialog + tray icon (placeholder)
- `docs/` - architecture, build, and calibration guides

See `docs/ARCHITECTURE.md` for design details, `docs/BUILD.md` for compiling, and `docs/CALIBRATION.md` for tuning the HUD/ROI to your own setup.

## Current status

MVP in progress. **Only Bomb Mode is implemented.** Mission Battle Royale (Battle Royale mode) is explicitly out of scope for this version.

Known gaps in this commit:
- **Window capture** is currently a BitBlt fallback; the real **WGC** integration is the next technical milestone.
- **Result-text OCR** is stubbed; the next milestone is to wire in **Windows.Media.Ocr**.
- The UI is limited to config.json + a console log; a tray menu and settings dialog are planned.

These gaps require the MSVC + Windows SDK toolchain to be installed on the build machine, which is not yet available here.

## License

MIT - see `LICENSE`.

---

## 中文简介

**CODMSpawnSnack** 是一个 Windows 小工具。在《使命召唤手游》模拟器的爆破模式里，我死亡后会自动切到抖音 / B站 / 快手等短视频网页，复活或整局结束后再切回游戏。视频始终停在同一进度，不会丢失当前刷到的那条内容。目前使用 BitBlt 做临时窗口捕获，后续会迁移到 WGC，并接入 Windows 离线 OCR。不读内存、不注入游戏，避免反作弊风险。

当前版本限定爆破模式；使命战场模式不在本版本范围内。Windows Media OCR 和托盘 UI 为后续里程碑。

---

## 免责声明 / Disclaimer

本软件 CO时间管理器（COSpawnSnack）是一款基于 Windows OCR 图像识别技术的个人学习与效率辅助工具，非《使命召唤手游》（CODM）官方出品，亦无任何商业授权或合作关系。本软件采用纯外部视觉检测逻辑，不读取、不修改游戏内存，亦不破坏游戏平衡。但由于各游戏反作弊系统（如腾讯 ACE）的检测机制不断更新，作者无法对"绝对不封号"做出法律担保。请用户自行承担使用本软件带来的潜在风险。软件内提及的所有游戏名称、商标及版权，均归其各自的合法拥有者（动视/腾讯等）所有。
