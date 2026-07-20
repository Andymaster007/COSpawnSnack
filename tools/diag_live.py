#!/usr/bin/env python3
# Live detection diagnostic for CODMSpawnSnack.
# Mirrors the C++ pipeline: HUD (alive/dead) via OpenCV template matching with
# resolution-independent normalization, and result text via Windows.Media.Ocr.
# Does NOT switch focus or launch video. Logs every state change to diag_live.log
# and prints to console.
import ctypes
import os
import sys
import time
import asyncio
import argparse
from ctypes import wintypes

import numpy as np
from PIL import Image
import cv2

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TEMPLATE_FULL = os.path.join(ROOT, "assets", "templates", "hud_bar_full.png")
TEMPLATE_LOW = os.path.join(ROOT, "assets", "templates", "hud_bar_low.png")
LOG_PATH = os.path.join(ROOT, "diag_live.log")

# ROI ratios (same as config defaults)
HUD_ROI = (0.0, 0.85, 0.25, 1.0)
RESULT_ROI = (0.30, 0.22, 0.70, 0.52)
CANON_W, CANON_H = 480, 162
HUD_THRESHOLD = 0.65
UPSCALE_MIN_H = 360
KEYWORDS = ["胜利", "战败", "失败", "VICTORY", "DEFEAT"]

# ---- window enumeration (mirrors FocusController.FindWindowByTitle) ----
user32 = ctypes.windll.user32
user32.IsWindowVisible.argtypes = [wintypes.HWND]; user32.IsWindowVisible.restype = ctypes.c_bool
user32.GetWindowTextLengthW.argtypes = [wintypes.HWND]; user32.GetWindowTextLengthW.restype = ctypes.c_int
user32.GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]; user32.GetWindowTextW.restype = ctypes.c_int
user32.EnumWindows.argtypes = [ctypes.c_void_p, wintypes.LPARAM]; user32.EnumWindows.restype = ctypes.c_bool


def find_game_window(substr="使命召唤手游"):
    found = []
    EnumProc = ctypes.WINFUNCTYPE(ctypes.c_bool, wintypes.HWND, wintypes.LPARAM)
    def cb(hwnd, lparam):
        if not user32.IsWindowVisible(hwnd):
            return True
        L = user32.GetWindowTextLengthW(hwnd)
        if L == 0:
            return True
        buf = ctypes.create_unicode_buffer(L + 1)
        user32.GetWindowTextW(hwnd, buf, L + 1)
        t = buf.value
        if substr in t:
            found.append((hwnd, t))
        return True
    user32.EnumWindows(EnumProc(cb), 0)
    exact = [(h, t) for h, t in found if t == substr]
    if exact:
        return exact[0][0]
    if found:
        # choose shortest title among substring matches (avoids emulator shell)
        found.sort(key=lambda x: len(x[1]))
        return found[0][0]
    return None


def capture_window(hwnd):
    r = wintypes.RECT()
    user32.GetWindowRect(hwnd, ctypes.byref(r))
    w = r.right - r.left
    h = r.bottom - r.top
    if w <= 0 or h <= 0:
        return None
    hwndDC = user32.GetWindowDC(hwnd)
    mdc = ctypes.windll.gdi32.CreateCompatibleDC(hwndDC)
    bmp = ctypes.windll.gdi32.CreateCompatibleBitmap(hwndDC, w, h)
    ctypes.windll.gdi32.SelectObject(mdc, bmp)
    ok = ctypes.windll.gdi32.BitBlt(mdc, 0, 0, w, h, hwndDC, 0, 0, 0x00CC0020)
    buf = ctypes.create_string_buffer(w * h * 4)
    class BITMAPINFOHEADER(ctypes.Structure):
        _fields_ = [("biSize", ctypes.c_uint), ("biWidth", ctypes.c_int), ("biHeight", ctypes.c_int),
                    ("biPlanes", ctypes.c_short), ("biBitCount", ctypes.c_short), ("biCompression", ctypes.c_uint),
                    ("biSizeImage", ctypes.c_uint), ("biXPelsPerMeter", ctypes.c_int), ("biYPelsPerMeter", ctypes.c_int),
                    ("biClrUsed", ctypes.c_uint), ("biClrImportant", ctypes.c_uint)]
    bih = BITMAPINFOHEADER()
    bih.biSize = ctypes.sizeof(BITMAPINFOHEADER)
    bih.biWidth = w; bih.biHeight = -h; bih.biPlanes = 1; bih.biBitCount = 32; bih.biCompression = 0
    ctypes.windll.gdi32.GetDIBits(mdc, bmp, 0, h, buf, ctypes.byref(bih), 0)
    ctypes.windll.gdi32.DeleteObject(bmp)
    ctypes.windll.gdi32.DeleteDC(mdc)
    user32.ReleaseDC(hwnd, hwndDC)
    if not ok:
        return None
    arr = np.frombuffer(buf, dtype=np.uint8).reshape((h, w, 4))
    bgr = arr[:, :, [2, 1, 0]].copy()
    return bgr


def load_template(path):
    im = Image.open(path).convert("L")
    if im.size != (CANON_W, CANON_H):
        im = im.resize((CANON_W, CANON_H), Image.LANCZOS)
    return np.asarray(im)


def hud_present(frame, templates):
    h, w = frame.shape[:2]
    x1, y1 = int(HUD_ROI[0] * w), int(HUD_ROI[1] * h)
    x2, y2 = int(HUD_ROI[2] * w), int(HUD_ROI[3] * h)
    if x2 <= x1 or y2 <= y1:
        return 0.0, False
    roi = frame[y1:y2, x1:x2]
    gray = cv2.cvtColor(roi, cv2.COLOR_BGR2GRAY)
    gray = cv2.resize(gray, (CANON_W, CANON_H), interpolation=cv2.INTER_LINEAR)
    best = 0.0
    for t in templates:
        if t.shape[0] > gray.shape[0] or t.shape[1] > gray.shape[1]:
            continue
        res = cv2.matchTemplate(gray, t, cv2.TM_CCOEFF_NORMED)
        _, mv, _, _ = cv2.minMaxLoc(res)
        best = max(best, mv)
    return best, best >= HUD_THRESHOLD


# ---- optional OCR via Windows.Media.Ocr ----
OCR = None
async def ocr_result(frame, engine):
    h, w = frame.shape[:2]
    x1, y1 = int(RESULT_ROI[0] * w), int(RESULT_ROI[1] * h)
    x2, y2 = int(RESULT_ROI[2] * w), int(RESULT_ROI[3] * h)
    if x2 <= x1 or y2 <= y1:
        return None
    roi = frame[y1:y2, x1:x2]
    # upscale so height >= UPSCALE_MIN_H
    rh = y2 - y1
    if rh < UPSCALE_MIN_H:
        scale = UPSCALE_MIN_H / max(1, rh)
        roi = cv2.resize(roi, None, fx=scale, fy=scale, interpolation=cv2.INTER_LINEAR)
    # BGR -> BGRA
    bgra = cv2.cvtColor(roi, cv2.COLOR_BGR2BGRA)
    # encode to PNG bytes
    ok, png = cv2.imencode(".png", bgra)
    if not ok:
        return None
    import winrt.windows.storage.streams as streams
    from winrt.windows.graphics.imaging import BitmapDecoder
    ras = streams.InMemoryRandomAccessStream()
    dw = streams.DataWriter(ras)
    dw.write_bytes(bytes(png.tobytes()))
    await dw.store_async()
    await dw.flush_async()
    decoder = await BitmapDecoder.create_async(ras)
    bmp = await decoder.get_software_bitmap_async()
    result = await engine.recognize_async(bmp)
    text = "".join(result.text.split())
    for kw in KEYWORDS:
        if kw.lower() in text.lower():
            return kw
    return None


def try_init_ocr():
    try:
        import winrt.windows.media.ocr as ocr_mod
        from winrt.windows.globalization import Language
        eng = ocr_mod.OcrEngine.try_create_from_language(Language("zh-CN"))
        if eng is None:
            eng = ocr_mod.OcrEngine.try_create_from_user_profile_language()
        if eng is None:
            return None
        return eng
    except Exception as e:
        print(f"[OCR] unavailable: {e}")
        return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--seconds", type=int, default=600)
    ap.add_argument("--title", default="使命召唤手游")
    args = ap.parse_args()

    templates = []
    for p in (TEMPLATE_FULL, TEMPLATE_LOW):
        if os.path.exists(p):
            templates.append(load_template(p))
        else:
            print(f"[warn] template missing: {p}")
    if not templates:
        print("No templates loaded; exiting.")
        return

    engine = try_init_ocr()
    ocr_ok = engine is not None
    print(f"OCR: {'enabled' if ocr_ok else 'disabled (will validate HUD only)'}")

    hwnd = find_game_window(args.title)
    if not hwnd:
        print("Game window not found. Make sure 使命召唤手游 is running.")
        return
    print(f"Found game window hwnd=0x{hwnd:X}")

    log = open(LOG_PATH, "w", encoding="utf-8")
    log.write("t,hud_state,hud_conf,result_keyword\n")
    start = time.time()
    last_hb = 0
    init = False
    hud_present_state = True
    have_result = False

    def stamp():
        return time.strftime("%H:%M:%S")

    def emit(line):
        print(line)
        log.write(line + "\n")
        log.flush()

    emit(f"{stamp()} === diag start (window 0x{hwnd:X}, OCR={ocr_ok}) ===")
    frame_i = 0
    try:
        while time.time() - start < args.seconds:
            frame = capture_window(hwnd)
            if frame is None:
                time.sleep(0.2)
                continue
            conf, present = hud_present(frame, templates)
            if not init or present != hud_present_state:
                emit(f"{stamp()} HUD {'Present' if hud_present_state else 'Absent'} -> "
                     f"{'Present' if present else 'Absent'} conf={conf:.3f}")
                hud_present_state = present
                init = True
            kw = None
            if ocr_ok and (frame_i % 3 == 0):
                try:
                    kw = asyncio.run(ocr_result(frame, engine))
                except Exception as e:
                    kw = None
                if kw:
                    if not have_result:
                        emit(f"{stamp()} RESULT keyword='{kw}'")
                        have_result = True
                else:
                    have_result = False
            now = time.time()
            if now - last_hb > 2:
                last_hb = now
                emit(f"{stamp()} [hb] hud={'Present' if present else 'Absent'} conf={conf:.3f} "
                     f"result={kw if kw else '-'}")
            frame_i += 1
            time.sleep(0.3)
    except KeyboardInterrupt:
        pass
    emit(f"{stamp()} === diag stop ===")
    log.close()


if __name__ == "__main__":
    main()
