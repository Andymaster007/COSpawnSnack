#!/usr/bin/env python3
"""Extract the segmented health bar from the existing HUD templates.

The full and low templates are bottom-left 16:9 crops that include the player
name, WiFi icon, CoD logo and ground background. Only the horizontal segmented
bar is stable across health states; the rest changes every game or every round.
This script finds the bar's vertical position by looking at where the two
health states differ most, crops both templates to that strip, and saves clean
bar templates for HUD presence detection.
"""
import argparse
import cv2
import numpy as np
from pathlib import Path


def find_bar_y_range(gray_full: np.ndarray, gray_low: np.ndarray,
                     min_height: int = 20, pad: int = 4) -> tuple[int, int]:
    diff = cv2.absdiff(gray_full, gray_low).astype(np.float32)
    row_sum = diff.sum(axis=1)

    # Smooth row profile to suppress single-row noise.
    k = max(3, min_height // 4 | 1)  # odd kernel
    smoothed = cv2.GaussianBlur(row_sum.reshape((1, -1)).astype(np.float32),
                                (k, 1), 0).flatten()

    # Find the strongest contiguous band of at least `min_height` rows.
    best_start, best_score = 0, 0.0
    for y in range(0, len(smoothed) - min_height + 1):
        score = float(smoothed[y:y + min_height].sum())
        if score > best_score:
            best_score = score
            best_start = y

    y1 = max(0, best_start - pad)
    y2 = min(gray_full.shape[0], best_start + min_height + pad)
    return y1, y2


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--full", type=Path, default=Path("assets/templates/hud_bar_full.png"))
    parser.add_argument("--low", type=Path, default=Path("assets/templates/hud_bar_low.png"))
    parser.add_argument("--out-dir", type=Path, default=Path("assets/templates"))
    parser.add_argument("--min-height", type=int, default=24)
    parser.add_argument("--pad", type=int, default=4)
    args = parser.parse_args()

    full = cv2.imread(str(args.full), cv2.IMREAD_UNCHANGED)
    low = cv2.imread(str(args.low), cv2.IMREAD_UNCHANGED)
    if full is None or low is None:
        raise RuntimeError("Failed to load one of the input templates.")
    if full.shape[:2] != low.shape[:2]:
        raise RuntimeError(f"Template sizes differ: {full.shape} vs {low.shape}")

    gray_full = cv2.cvtColor(full, cv2.COLOR_BGRA2GRAY) if full.shape[2] == 4 else cv2.cvtColor(full, cv2.COLOR_BGR2GRAY)
    gray_low = cv2.cvtColor(low, cv2.COLOR_BGRA2GRAY) if low.shape[2] == 4 else cv2.cvtColor(low, cv2.COLOR_BGR2GRAY)

    y1, y2 = find_bar_y_range(gray_full, gray_low, args.min_height, args.pad)
    print(f"Detected bar band: y={y1}..{y2} (template height {full.shape[0]})")

    args.out_dir.mkdir(parents=True, exist_ok=True)
    out_full = args.out_dir / "hud_bar_segments_full.png"
    out_low = args.out_dir / "hud_bar_segments_low.png"
    cv2.imwrite(str(out_full), full[y1:y2, :])
    cv2.imwrite(str(out_low), low[y1:y2, :])
    print(f"Wrote {out_full} ({y2 - y1}x{full.shape[1]})")
    print(f"Wrote {out_low} ({y2 - y1}x{low.shape[1]})")


if __name__ == "__main__":
    main()
