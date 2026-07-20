#!/usr/bin/env python3
"""Extract a CODM "F 装备" icon template from a backpack screenshot.

Usage:
    python tools/extract_equipment_template.py \
        --src /c/Users/Andy/Pictures/Screenshots/F装备.png \
        --out assets/templates/equipment_f_icon.png

The script searches the equipment ROI, locates the bright "F 装备" glyphs, and
crops around them with --pad pixels of margin (default 12, a middle ground
between the bare glyphs and the full dark icon box). It then prints the tight
live-detection ROI (template bounds + 2px, i.e. HUD-level 4x4px scan slack) in
screen-relative coordinates for pasting into config.h / config.json.
"""
import argparse
import cv2
import numpy as np
from pathlib import Path


def imread_gray(path: Path) -> np.ndarray:
    raw = np.fromfile(str(path), dtype=np.uint8)
    img = cv2.imdecode(raw, cv2.IMREAD_UNCHANGED)
    if img is None:
        raise RuntimeError(f"Failed to decode image: {path}")
    if len(img.shape) == 3 and img.shape[2] == 4:
        bgr = img[:, :, :3].astype(np.float32)
        alpha = img[:, :, 3:4].astype(np.float32) / 255.0
        white = np.full_like(bgr, 255.0)
        blended = bgr * alpha + white * (1.0 - alpha)
        gray = cv2.cvtColor(blended.astype(np.uint8), cv2.COLOR_BGR2GRAY)
    elif len(img.shape) == 3 and img.shape[2] == 3:
        gray = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)
    else:
        gray = img
    return gray


def crop_roi(gray: np.ndarray, roi: tuple) -> np.ndarray:
    h, w = gray.shape
    x1 = int(w * roi[0]); x2 = int(w * roi[1])
    y1 = int(h * roi[2]); y2 = int(h * roi[3])
    return gray[y1:y2, x1:x2]


def find_bright_text_box(roi_gray: np.ndarray,
                         bright_threshold: int = 200,
                         min_component_area: int = 15) -> tuple:
    """Return the bounding box (x1, y1, x2, y2) of the bright icon glyphs."""
    _, bright = cv2.threshold(roi_gray, bright_threshold, 255, cv2.THRESH_BINARY)
    kernel = cv2.getStructuringElement(cv2.MORPH_RECT, (5, 5))
    bright = cv2.morphologyEx(bright, cv2.MORPH_CLOSE, kernel)

    num_labels, labels, stats, centroids = cv2.connectedComponentsWithStats(
        bright, connectivity=8)

    xs = []
    ys = []
    for label in range(1, num_labels):
        area = stats[label, cv2.CC_STAT_AREA]
        if area < min_component_area:
            continue
        xs.extend([stats[label, cv2.CC_STAT_LEFT],
                   stats[label, cv2.CC_STAT_LEFT] + stats[label, cv2.CC_STAT_WIDTH]])
        ys.extend([stats[label, cv2.CC_STAT_TOP],
                   stats[label, cv2.CC_STAT_TOP] + stats[label, cv2.CC_STAT_HEIGHT]])

    if not xs:
        return (0, 0, roi_gray.shape[1], roi_gray.shape[0])

    return (min(xs), min(ys), max(xs), max(ys))


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Extract a CODM F-equipment icon template")
    parser.add_argument("--src", type=Path, required=True,
                        help="Source screenshot containing the F 装备 icon.")
    parser.add_argument("--out", type=Path,
                        default=Path("assets/templates/equipment_f_icon.png"))
    parser.add_argument("--roi-x1", type=float, default=0.0)
    parser.add_argument("--roi-x2", type=float, default=0.10)
    parser.add_argument("--roi-y1", type=float, default=0.90)
    parser.add_argument("--roi-y2", type=float, default=0.985)
    parser.add_argument("--pad", type=int, default=12,
                        help="Margin (px) around the glyphs for the template "
                             "(default 12 = middle ground).")
    parser.add_argument("--roi-slack", type=int, default=2,
                        help="Each-side slack (px) of the live ROI beyond the "
                             "template (default 2 -> 4x4 scan slack, like HUD).")
    parser.add_argument("--debug-dir", type=Path, default=Path("assets/templates"))
    args = parser.parse_args()

    gray = imread_gray(args.src)
    h, w = gray.shape
    roi = (args.roi_x1, args.roi_x2, args.roi_y1, args.roi_y2)
    roi_gray = crop_roi(gray, roi)
    print(f"Equipment search ROI size: {roi_gray.shape[1]}x{roi_gray.shape[0]}")

    bx1, by1, bx2, by2 = find_bright_text_box(roi_gray)
    print(f"Bright glyph box: x={bx1}..{bx2}, y={by1}..{by2}, size={bx2-bx1}x{by2-by1}")

    # Template = glyphs + --pad of margin (middle ground).
    tx1 = max(0, bx1 - args.pad)
    ty1 = max(0, by1 - args.pad)
    tx2 = min(roi_gray.shape[1], bx2 + args.pad)
    ty2 = min(roi_gray.shape[0], by2 + args.pad)
    print(f"Template box (glyph+{args.pad}px): x={tx1}..{tx2}, y={ty1}..{ty2}, size={tx2-tx1}x{ty2-ty1}")

    template = roi_gray[ty1:ty2, tx1:tx2]
    args.out.parent.mkdir(parents=True, exist_ok=True)
    cv2.imwrite(str(args.out), template)
    print(f"Wrote {args.out} ({template.shape[1]}x{template.shape[0]})")

    res = cv2.matchTemplate(roi_gray, template, cv2.TM_CCOEFF_NORMED)
    _, maxVal, _, maxLoc = cv2.minMaxLoc(res)
    print(f"Match against source ROI: maxVal={maxVal:.4f} at {maxLoc}")

    # Live ROI in absolute source pixels: template bounds + --roi-slack each side.
    rx1 = max(0, tx1 - args.roi_slack)
    ry1 = max(0, ty1 - args.roi_slack)
    rx2 = min(roi_gray.shape[1], tx2 + args.roi_slack)
    ry2 = min(roi_gray.shape[0], ty2 + args.roi_slack)
    # Convert to screen-relative (add the search-ROI origin).
    ax1 = int(w * args.roi_x1) + rx1
    ay1 = int(h * args.roi_y1) + ry1
    ax2 = int(w * args.roi_x1) + rx2
    ay2 = int(h * args.roi_y1) + ry2
    rel = (ax1 / w, ay1 / h, ax2 / w, ay2 / h)
    cw = int((rel[2] - rel[0]) * 1920)
    ch = int((rel[3] - rel[1]) * 1080)
    print(f"Suggested live ROI (rel): left={rel[0]:.4f} top={rel[1]:.4f} "
          f"right={rel[2]:.4f} bottom={rel[3]:.4f}")
    print(f"  canonical canvas: {cw}x{ch}  | template {template.shape[1]}x{template.shape[0]} "
          f"| scan slack {cw-template.shape[1]}x{ch-template.shape[0]}")

    debug = cv2.cvtColor(roi_gray, cv2.COLOR_GRAY2BGR)
    cv2.rectangle(debug, (tx1, ty1), (tx2, ty2), (0, 0, 255), 2)
    args.debug_dir.mkdir(parents=True, exist_ok=True)
    debug_path = args.debug_dir / "debug_equipment_box.png"
    cv2.imwrite(str(debug_path), debug)
    print(f"Wrote debug overlay {debug_path}")


if __name__ == "__main__":
    main()
