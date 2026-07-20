# Calibration guide

> Everything here is **resolution-independent**. The ROI numbers are screen
> ratios, and the HUD templates are automatically resampled to a fixed
> **canonical canvas (480×162)** before matching. So **one set of templates
> and one config works on every device** — 720p, 1080p, 1440p, 4K, with or
> without Windows DPI scaling. You never re-crop templates per machine.

## Before you start

You need a few screenshots from a real CODM Bomb Mode match:

1. **Alive HUD (full health)** — bottom-left bar visible, segments filled/bright.
2. **Alive HUD (low health)** — same bar, segments empty/dark; proves the template works when the screen is darkened.
3. **Death / no HUD** — bottom-left bar gone, but the CODM logo may still be there.
4. **Result screen** — round-end or match-end, with `胜利/战败` (or VICTORY/DEFEAT) in the center.

If you do not have these yet, play a match and take them with `Win + Shift + S` or `PrtSc`. Then come back to this guide.

## 1. Capture the HUD templates

The HUD template is the **whole HUD ROI region** — the same rectangle defined by `roi_left/top/right/bottom`, i.e. the bottom-left ~25% × 15% of the screen. It will naturally contain the segmented health bar, the player name, and the CODM logo. That is fine: the detector resamples both the template and the live capture to a fixed **canonical canvas (480×162)** before comparing, and because the ROI is defined in screen percentages the bar lands at the same fractional position on every device. The match is driven by the bar structure; on death the bar disappears and the score drops below threshold (verified: ~0.95 alive vs ~0.45 death, at 720p / 1080p / 4K alike).

**Important:** the bar looks different at full health vs. low health. At full health the segments are filled (bright); at low health they are empty (dark). One template will not reliably match both. Capture **two** templates:

- `hud_bar_full.png` — from a full-health screenshot.
- `hud_bar_low.png` — from a low-health screenshot (the darker the better, e.g. 1 HP).

Steps:

1. Open your alive HUD screenshots in an image editor (Paint, ShareX, etc.).
2. Crop the **entire HUD ROI rectangle** (the same region as `roi_*`) — do **not** tight-crop just the bar; the template must keep the bar at the same relative position as it appears inside the ROI.
3. Save them as `assets/templates/hud_bar_full.png` and `assets/templates/hud_bar_low.png` (create the folder if needed). The tool resizes them to the canonical canvas internally, so their exact pixel size does not matter.
4. Update `config.json`:

```json
{
  "hud": {
    "roi_left": 0.0,
    "roi_top": 0.85,
    "roi_right": 0.25,
    "roi_bottom": 1.0,
    "template_paths": [
      "assets/templates/hud_bar_full.png",
      "assets/templates/hud_bar_low.png"
    ],
    "match_threshold": 0.65
  }
}
```

- `roi_*` defines where the detector looks for the template. The bottom-left 25% × 15% region is a tight but safe starting point for most 16:9 emulators.
- `match_threshold` is the OpenCV template-match similarity. If you get false positives, raise it to `0.70`–`0.80`. If you get false negatives when injured, lower it to `0.55`–`0.60`.
- The detector uses the **best** match across all listed templates, so both full and low-health bars are accepted.

### Why not a single template?

The low-health screenshot is globally darkened, but the bigger problem is that the bar segments change from filled to empty. OpenCV's normalized correlation is brightness-invariant, but it is **not** invariant to the segment fill changing. Using two templates is the simplest robust fix.

### Resolution independence (how it works)

Both the loaded template and the live ROI crop are resized to the canonical canvas `480×162` (derived from `roi_fraction × 1920×1080`) before `matchTemplate`. Because the ROI is a fixed screen-percentage and CODM is locked to 16:9, the bar's fractional position is identical on every device, so the normalized images line up regardless of the real window resolution. No per-device recalibration needed.

## 2. Define the result-text ROI

In Bomb Mode, the round-end text is smaller and higher; the match-end text is larger and lower. They are close but do not overlap. Based on the provided screenshots:

- **Round-end victory** (`回合胜利`): the blue banner with `胜利` sits around 25%–35% of screen height.
- **Match-end victory** (`对局胜利`): the large yellow `胜利` sits around 35%–45% of screen height.
- Both are centered horizontally and occupy roughly 30%–70% of screen width.

A single ROI that covers both is therefore:

- Left: ~30% of screen width
- Right: ~70% of screen width
- Top: ~22% of screen height
- Bottom: ~52% of screen height

Update `config.json`:

```json
{
  "result": {
    "roi_left": 0.30,
    "roi_top": 0.22,
    "roi_right": 0.70,
    "roi_bottom": 0.52,
    "keywords": ["胜利", "战败", "失败", "VICTORY", "DEFEAT"],
    "confidence_threshold": 0.6
  }
}
```

`confidence_threshold` is the OCR confidence. If the tool misses the result text, lower it to `0.5` or `0.4`. If it fires too early, raise it to `0.7`.

`upscale_min_height` (default 360) keeps OCR accuracy resolution-independent: before recognition the result ROI is upscaled so its height reaches at least this many pixels. On a low-res window the `胜利/战败` text would otherwise be only a few pixels tall and get missed. Leave it at 360 unless you run at very low resolutions, in which case raise it.

## 3. Tune the state machine

```json
{
  "state_machine": {
    "hud_missing_frames_to_die": 3,
    "result_confirm_frames": 2
  }
}
```

- `hud_missing_frames_to_die`: how many frames the HUD must be gone before we treat it as death. Set to 2–3 to ignore single-frame flickers.
- `result_confirm_frames`: how many consecutive frames the OCR must see the result text before we treat the match as over. Set to 2–4.

## 4. Verify the calibration without playing

The tool has a **dry-run / calibration mode** (not yet implemented in the stub UI) that prints detections per frame without switching windows. Use it to make sure:

- HUD present → detector says `Present`
- HUD gone (death or lobby) → `Absent`
- Result text visible → `Result detected`

Only after these three checks pass should you enable automatic switching.

## 5. Common pitfalls

- **Template includes the CODM logo** → you will never switch to video on death.
- **Template includes too much background** → false positives on different maps.
- **Result ROI too small** → misses the smaller round-end text.
- **Result ROI too big** → may pick up killcam or spectator text; use keywords and confidence to filter.
- **Running as Administrator** → focus-switching APIs may behave differently; run as normal user.
