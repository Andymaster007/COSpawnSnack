# Calibration guide

> These numbers are **screen ratios**, not pixels. That lets the same config work across different monitors and resolutions.

## Before you start

You need three screenshots from a real CODM Bomb Mode match:

1. **Alive HUD** — bottom-left bar visible (full health or damaged; damaged is better because it proves the template is stable).
2. **Death / no HUD** — bottom-left bar gone, but the CODM logo may still be there.
3. **Result screen** — either round-end or match-end, with `胜利/失败` (or VICTORY/DEFEAT) in the center.

If you do not have these yet, play a match and take them with `Win + Shift + S` or `PrtSc`. Then come back to this guide.

## 1. Capture the HUD template

The HUD template must **NOT** contain the CODM logo, because the logo stays after death. The template should contain only the **segmented health bar** (borders + dividers).

Steps:

1. Open your alive HUD screenshot in an image editor (Paint, ShareX, etc.).
2. Draw a tight rectangle around the segmented health bar, **excluding** the CODM logo and the player name/WiFi icon.
3. Save it as `assets/templates/hud_bar.png` (create the folder if needed).
4. Update `config.json`:

```json
{
  "hud": {
    "roi_left": 0.0,
    "roi_top": 0.72,
    "roi_right": 0.30,
    "roi_bottom": 1.0,
    "template_paths": ["assets/templates/hud_bar.png"],
    "match_threshold": 0.65
  }
}
```

- `roi_*` defines where the detector looks for the template. The bottom-left 30% × 28% region is a safe starting point for most 16:9 emulators.
- `match_threshold` is the OpenCV template-match similarity. If you get false positives, raise it to `0.70`–`0.80`. If you get false negatives when injured, lower it to `0.55`–`0.60`.

## 2. Define the result-text ROI

In Bomb Mode, the round-end text is smaller and higher; the match-end text is larger and lower. They are close but do not overlap. Draw one rectangle that covers both:

- Left: ~35% of screen width
- Right: ~65% of screen width
- Top: ~30% of screen height
- Bottom: ~70% of screen height

This big vertical band is guaranteed to catch both texts.

Update `config.json`:

```json
{
  "result": {
    "roi_left": 0.35,
    "roi_top": 0.30,
    "roi_right": 0.65,
    "roi_bottom": 0.70,
    "keywords": ["胜利", "失败", "VICTORY", "DEFEAT"],
    "confidence_threshold": 0.6
  }
}
```

`confidence_threshold` is the OCR confidence. If the tool misses the result text, lower it to `0.5` or `0.4`. If it fires too early, raise it to `0.7`.

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
