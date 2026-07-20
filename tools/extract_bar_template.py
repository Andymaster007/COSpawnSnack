#!/usr/bin/env python3
"""Extract tight segmented health-bar templates from CODM HUD screenshots.

The C++ detector crops the live screen to a tight HUD bar ROI (bottom-left
~2% of screen height, just above the bottom edge) and runs template matching
inside that ROI. To keep the templates consistent with the live path, this
script performs the same ROI crop on the source screenshots before extracting
the bar.

Old templates included the player name / WiFi icon / CoD logo area below the
bar, which caused false matches when the bar was gone but the template's
background region still correlated with the death / round-end screen. This
script tightens the crop to the bar itself.

Usage:
    python tools/extract_bar_template.py \
        --ref-full assets/templates/sources/100血.png \
        --ref-low  assets/templates/sources/10血.png \
        --src 100=assets/templates/sources/100血.png \
        --src 75=assets/templates/sources/75血.png \
        --src 50=assets/templates/sources/50血.png \
        --src 25=assets/templates/sources/25血.png \
        --src 10=assets/templates/sources/10血.png \
        --out-dir assets/templates
"""
import argparse
import cv2
import numpy as np
from pathlib import Path
from typing import Dict, List, Tuple


def imread_unicode(path: Path) -> np.ndarray:
    """Read an image from a possibly non-ASCII path using numpy + imdecode.

    OpenCV's cv2.imread relies on the C runtime and can fail on Windows when
    the path contains Unicode characters. np.fromfile and imdecode bypass that.
    """
    raw = np.fromfile(str(path), dtype=np.uint8)
    img = cv2.imdecode(raw, cv2.IMREAD_UNCHANGED)
    if img is None:
        raise RuntimeError(f"Failed to decode image: {path}")
    return img


def load_gray(path: Path) -> np.ndarray:
    """Load an image and convert to grayscale, blending alpha against white."""
    img = imread_unicode(path)
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


def crop_hud_roi(gray: np.ndarray, roi_x1: float, roi_x2: float,
                 roi_y1: float, roi_y2: float) -> np.ndarray:
    """Crop a screenshot to the same relative ROI used by the C++ detector."""
    h, w = gray.shape
    x1 = int(w * roi_x1)
    x2 = int(w * roi_x2)
    y1 = int(h * roi_y1)
    y2 = int(h * roi_y2)
    return gray[y1:y2, x1:x2]


def find_bar_y_range(gray_full_roi: np.ndarray, gray_low_roi: np.ndarray,
                     min_height: int, pad: int) -> Tuple[int, int]:
    """Locate the vertical band of the segmented bar inside the HUD ROI."""
    diff = cv2.absdiff(gray_full_roi, gray_low_roi).astype(np.float32)
    row_sum = diff.sum(axis=1)

    # Smooth the row profile to suppress single-row noise.
    k = max(3, min_height // 4 | 1)  # odd kernel
    smoothed = cv2.GaussianBlur(row_sum.reshape((1, -1)).astype(np.float32),
                                (k, 1), 0).flatten()

    # Find the contiguous window of `min_height` rows with the most energy.
    best_start, best_score = 0, 0.0
    for y in range(0, len(smoothed) - min_height + 1):
        score = float(smoothed[y:y + min_height].sum())
        if score > best_score:
            best_score = score
            best_start = y

    y1 = max(0, best_start - pad)
    y2 = min(gray_full_roi.shape[0], best_start + min_height + pad)
    return y1, y2


def find_bar_x_range(gray_full_roi: np.ndarray, y1: int, y2: int,
                     xpad: int) -> Tuple[int, int]:
    """Locate the horizontal extent of the segmented bar within the ROI.

    The bar columns have high local variance along the y-axis (bright filled
    segments separated by dark gaps/dividers), while the surrounding sky/ground
    background is smooth. This is robust even against a bright sky background.
    """
    band = gray_full_roi[y1:y2, :].astype(np.float32)
    # Variance along the y-axis for each column.
    col_var = band.var(axis=0)

    # Smooth the profile to suppress single-column noise.
    k = max(3, band.shape[1] // 100 | 1)
    col_var = cv2.GaussianBlur(col_var.reshape((1, -1)).astype(np.float32),
                                (k, 1), 0).flatten()

    threshold = max(0.0, col_var.max() * 0.25)
    cols = np.where(col_var > threshold)[0]

    if len(cols) == 0:
        return 0, band.shape[1]

    x1 = max(0, cols[0] - xpad)
    x2 = min(band.shape[1], cols[-1] + 1 + xpad)
    return int(x1), int(x2)


def parse_src_args(args: List[str]) -> Dict[str, Path]:
    """Parse --src entries like 'full=path/to/full.png' into a dict."""
    out: Dict[str, Path] = {}
    for entry in args:
        if "=" not in entry:
            raise ValueError(f"--src must be in the form name=path, got: {entry}")
        name, path = entry.split("=", 1)
        out[name.strip()] = Path(path.strip())
    return out


def main() -> None:
    parser = argparse.ArgumentParser(description="Extract tight CODM HUD bar templates")
    parser.add_argument("--ref-full", type=Path, required=True,
                        help="Full-health reference image (used to locate bar).")
    parser.add_argument("--ref-low", type=Path, required=True,
                        help="Low-health reference image (used to locate bar).")
    parser.add_argument("--src", type=str, action="append", required=True,
                        help="Blood-state source to crop, e.g. 100=full.png. Repeatable.")
    parser.add_argument("--out-dir", type=Path, default=Path("assets/templates"))
    parser.add_argument("--min-height", type=int, default=16,
                        help="Height of the bar band to search for (default 16).")
    parser.add_argument("--pad", type=int, default=2,
                        help="Extra rows to include above/below the bar band (default 2).")
    parser.add_argument("--xpad", type=int, default=2,
                        help="Extra columns to include left/right of the bar (default 2).")
    parser.add_argument("--roi-x1", type=float, default=0.012,
                        help="Left edge of the HUD ROI as a fraction of width (default 0.012).")
    parser.add_argument("--roi-x2", type=float, default=0.211,
                        help="Right edge of the HUD ROI as a fraction of width (default 0.211).")
    parser.add_argument("--roi-y1", type=float, default=0.954,
                        help="Top edge of the HUD ROI as a fraction of height (default 0.954).")
    parser.add_argument("--roi-y2", type=float, default=0.977,
                        help="Bottom edge of the HUD ROI as a fraction of height (default 0.977).")
    args = parser.parse_args()

    if not (0 <= args.roi_x1 < args.roi_x2 <= 1 and 0 <= args.roi_y1 < args.roi_y2 <= 1):
        raise ValueError("Invalid ROI coordinates")

    srcs = parse_src_args(args.src)

    # Load references and crop to the same HUD ROI used by the live detector.
    ref_full = load_gray(args.ref_full)
    ref_low = load_gray(args.ref_low)
    if ref_full.shape != ref_low.shape:
        raise RuntimeError(f"Reference sizes differ: {ref_full.shape} vs {ref_low.shape}")

    ref_full_roi = crop_hud_roi(ref_full, args.roi_x1, args.roi_x2,
                                args.roi_y1, args.roi_y2)
    ref_low_roi = crop_hud_roi(ref_low, args.roi_x1, args.roi_x2,
                               args.roi_y1, args.roi_y2)

    y1, y2 = find_bar_y_range(ref_full_roi, ref_low_roi, args.min_height, args.pad)
    x1, x2 = find_bar_x_range(ref_full_roi, y1, y2, args.xpad)
    print(f"Detected bar band inside HUD ROI: y={y1}..{y2}, x={x1}..{x2} "
          f"(ROI {ref_full_roi.shape[1]}x{ref_full_roi.shape[0]})")
    print(f"Cropped template size: {x2 - x1}x{y2 - y1}")

    args.out_dir.mkdir(parents=True, exist_ok=True)
    for name, path in srcs.items():
        src = imread_unicode(path)

        # Crop to the same HUD ROI, then apply the same tight bar crop.
        src_gray = cv2.cvtColor(src, cv2.COLOR_BGR2GRAY) if len(src.shape) == 3 else src
        src_roi = crop_hud_roi(src_gray, args.roi_x1, args.roi_x2,
                               args.roi_y1, args.roi_y2)
        cropped = src_roi[y1:y2, x1:x2]

        out_path = args.out_dir / f"hud_bar_segments_{name}.png"
        cv2.imwrite(str(out_path), cropped)
        print(f"Wrote {out_path} ({cropped.shape[1]}x{cropped.shape[0]})")


if __name__ == "__main__":
    main()
