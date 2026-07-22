#!/usr/bin/env python3
# Live detection diagnostic for CODMSpawnSnack.
#
# Mirrors the C++ pipeline so the detection + switch logic can be validated
# without the unsigned C++ exe (blocked by Device Guard on this machine):
#   * Respawn hint ("你将在下一回合重生") via OCR in the center-bottom ROI.
#   * Result text (胜利/战败 etc.) via OCR in the center ROI.
#   * A Python port of StateMachine drives the same switch decisions.
#
# Does NOT switch focus or launch video. Logs every state change to
# diag_live.log and prints to console.
import ctypes
import os
import time
import asyncio
import argparse
from ctypes import wintypes

import numpy as np
import cv2

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LOG_PATH = os.path.join(ROOT, "diag_live.log")

KREF_W, KREF_H = 1920, 1080

# Center-bottom respawn hint (e.g. "你将在下一回合重生").
RESPAWN_ROI = (0.30, 0.79, 0.61, 0.89)
RESPAWN_KEYWORDS = ["你将在下一回合重生", "下一回合重生", "重生"]
RESPAWN_UPSCALE_MIN_H = 160

# Center result banner (胜利/战败/VICTORY/DEFEAT).
RESULT_ROI = (0.30, 0.22, 0.70, 0.52)
RESULT_KEYWORDS = ["胜利", "战败", "失败", "VICTORY", "DEFEAT"]
RESULT_UPSCALE_MIN_H = 360

CONFIDENCE_THRESHOLD = 0.6

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


def normalize_text(s):
    return "".join(s.split()).lower()


class OcrTextDetector:
    """Generic OCR detector for a screen region and a keyword list.
    Mirrors C++ ResultDetector."""
    def __init__(self, roi, keywords, upscale_min_h):
        self.roi = roi
        self.keywords = keywords
        self.upscale_min_h = upscale_min_h
        self.found = False
        self.matched_keyword = ""
        self.raw_text = ""

    def detect(self, frame, engine):
        result = {"found": False, "keyword": "", "raw": ""}
        if frame is None or engine is None:
            return result
        h, w = frame.shape[:2]
        x1, y1 = int(self.roi[0] * w), int(self.roi[1] * h)
        x2, y2 = int(self.roi[2] * w), int(self.roi[3] * h)
        if x2 <= x1 or y2 <= y1:
            return result
        roi = frame[y1:y2, x1:x2]
        rh = y2 - y1
        if rh < self.upscale_min_h:
            scale = self.upscale_min_h / max(1, rh)
            roi = cv2.resize(roi, None, fx=scale, fy=scale, interpolation=cv2.INTER_LINEAR)
        bgra = cv2.cvtColor(roi, cv2.COLOR_BGR2BGRA)
        ok, png = cv2.imencode(".png", bgra)
        if not ok:
            return result
        import winsdk.windows.storage.streams as streams
        from winsdk.windows.graphics.imaging import BitmapDecoder
        ras = streams.InMemoryRandomAccessStream()
        dw = streams.DataWriter(ras)
        dw.write_bytes(bytes(png.tobytes()))
        asyncio.run(dw.store_async())
        asyncio.run(dw.flush_async())
        decoder = asyncio.run(BitmapDecoder.create_async(ras))
        bmp = asyncio.run(decoder.get_software_bitmap_async())
        ocr_result = asyncio.run(engine.recognize_async(bmp))
        text = normalize_text(ocr_result.text)
        result["raw"] = text
        for kw in self.keywords:
            if normalize_text(kw) in text:
                result["found"] = True
                result["keyword"] = kw
                break
        return result


class StateMachine:
    """Python port of src/state/state_machine.cpp with respawn-text logic."""
    def __init__(self, logfn):
        self.log = logfn
        self.respawn_confirm_threshold = 5
        self.result_confirm_threshold = 2
        self.respawn_absent_threshold = 5
        self.reset()

    def reset(self):
        self.respawn_confirm_frames = 0
        self.respawn_absent_frames = 0
        self.result_confirm_frames = 0
        self.result_absent_frames = 0
        self.result_active = False
        self.state = "Idle"  # Idle | InGame | OnVideo

    def update(self, respawn_found, respawn_kw, result_found, result_kw):
        # Result text (round/match end)
        if result_found:
            self.result_confirm_frames += 1
            self.result_absent_frames = 0
        else:
            self.result_confirm_frames = 0
            self.result_absent_frames += 1

        if self.result_confirm_frames >= self.result_confirm_threshold:
            if not self.result_active:
                self.result_active = True
                self.log("[SM] Result text confirmed; resetting round state.")
                self.respawn_confirm_frames = 0
                self.respawn_absent_frames = 0
                if self.state in ("OnVideo", "InGame"):
                    self.log("[SM] switching back to game (match/round end).")
                self.state = "Idle"
            return

        if self.result_absent_frames >= 90:
            self.result_active = False

        # Respawn text (death vs alive)
        if respawn_found:
            self.respawn_confirm_frames += 1
            self.respawn_absent_frames = 0
        else:
            self.respawn_confirm_frames = 0
            self.respawn_absent_frames += 1

        if self.respawn_confirm_frames >= self.respawn_confirm_threshold:
            if self.state != "OnVideo":
                self.log("[SM] Respawn text confirmed; switching to video.")
            self.state = "OnVideo"
            return

        if self.respawn_absent_frames >= self.respawn_absent_threshold:
            if self.state == "OnVideo":
                self.log("[SM] Respawn text gone; switching back to game.")
            self.state = "InGame"


def try_init_ocr():
    try:
        import winsdk.windows.media.ocr as ocr_mod
        from winsdk.windows.globalization import Language
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
    ap.add_argument("--seconds", type=int, default=30)
    ap.add_argument("--title", default="使命召唤手游")
    args = ap.parse_args()

    engine = try_init_ocr()
    if engine is None:
        print("OCR engine unavailable. Install the Chinese OCR language pack.")
        return

    respawn_detector = OcrTextDetector(RESPAWN_ROI, RESPAWN_KEYWORDS, RESPAWN_UPSCALE_MIN_H)
    result_detector = OcrTextDetector(RESULT_ROI, RESULT_KEYWORDS, RESULT_UPSCALE_MIN_H)

    hwnd = find_game_window(args.title)
    if not hwnd:
        print("Game window not found. Make sure 使命召唤手游 is running.")
        return
    print(f"Found game window hwnd=0x{hwnd:X}")

    log = open(LOG_PATH, "w", encoding="utf-8")
    def emit(line):
        print(line)
        log.write(line + "\n")
        log.flush()

    emit("=== diag start ===")
    emit(f"RESPAWN ROI={RESPAWN_ROI} keywords={RESPAWN_KEYWORDS}")
    emit(f"RESULT  ROI={RESULT_ROI} keywords={RESULT_KEYWORDS}")

    sm = StateMachine(emit)
    start = time.time()
    last_hb = 0
    init_respawn = None
    init_result = None
    frame_i = 0
    try:
        while time.time() - start < args.seconds:
            frame = capture_window(hwnd)
            if frame is None:
                time.sleep(0.2)
                continue

            respawn = respawn_detector.detect(frame, engine)
            result = result_detector.detect(frame, engine)
            sm.update(respawn["found"], respawn["keyword"], result["found"], result["keyword"])

            if init_respawn is None or respawn["found"] != init_respawn:
                emit(f"[RESPAWN] {'YES ' + respawn['keyword'] if respawn['found'] else 'no'} raw=[{respawn['raw']}]")
                init_respawn = respawn["found"]
            if init_result is None or result["found"] != init_result:
                emit(f"[RESULT] {'YES ' + result['keyword'] if result['found'] else 'no'} raw=[{result['raw']}]")
                init_result = result["found"]

            now = time.time()
            if now - last_hb > 2:
                last_hb = now
                emit(f"[hb] respawn={'YES' if respawn['found'] else 'no'} rraw=[{respawn['raw']}] "
                     f"result={'YES' if result['found'] else 'no'} sraw=[{result['raw']}] sm={sm.state}")

            frame_i += 1
            time.sleep(0.1)
    except KeyboardInterrupt:
        pass
    emit("=== diag stop ===")
    log.close()


if __name__ == "__main__":
    main()
