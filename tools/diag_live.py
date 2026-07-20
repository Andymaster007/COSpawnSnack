#!/usr/bin/env python3
# Live detection diagnostic for CODMSpawnSnack.
#
# Mirrors the C++ pipeline so the detection + switch logic can be validated
# without the unsigned C++ exe (blocked by Device Guard on this machine):
#   * HUD (alive/dead) via five-segment template matching with
#     resolution-independent normalization + hysteresis (raw-vs-raw, no CLAHE).
#   * Equipment ("F 装备") icon via its own tight template + ROI.
#   * Result text via Windows.Media.Ocr (throttled).
#   * A Python port of StateMachine drives the same switch decisions, so the
#     equipment-cancel and video-switch behaviour can be observed live.
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
HUD_TEMPLATE_PATHS = [os.path.join(ROOT, "assets", "templates", f"hud_bar_segments_{v}.png")
                      for v in (100, 75, 50, 25, 10)]
EQUIP_TEMPLATE = os.path.join(ROOT, "assets", "templates", "equipment_f_icon.png")
LOG_PATH = os.path.join(ROOT, "diag_live.log")

KREF_W, KREF_H = 1920, 1080
HUD_ROI = (0.012, 0.954, 0.211, 0.977)
EQUIP_ROI = (0.0063, 0.9147, 0.0766, 0.9722)
HUD_CANON = (int((HUD_ROI[2] - HUD_ROI[0]) * KREF_W), int((HUD_ROI[3] - HUD_ROI[1]) * KREF_H))
EQUIP_CANON = (int((EQUIP_ROI[2] - EQUIP_ROI[0]) * KREF_W), int((EQUIP_ROI[3] - EQUIP_ROI[1]) * KREF_H))
HUD_THRESHOLD = 0.65
HUD_ABSENT_THRESHOLD = 0.35
EQUIP_THRESHOLD = 0.65

RESULT_ROI = (0.30, 0.22, 0.70, 0.52)
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


class HudDetector:
    """Mirrors C++ HudDetector: five HUD templates (hysteresis) + equipment template."""
    def __init__(self):
        self.templates = []
        for p in HUD_TEMPLATE_PATHS:
            if os.path.exists(p):
                self.templates.append(cv2.imread(p, cv2.IMREAD_GRAYSCALE))
            else:
                print(f"[warn] HUD template missing: {p}")
        if os.path.exists(EQUIP_TEMPLATE):
            self.etmpl = cv2.imread(EQUIP_TEMPLATE, cv2.IMREAD_GRAYSCALE)
        else:
            self.etmpl = None
            print(f"[warn] equipment template missing: {EQUIP_TEMPLATE}")
        self.present_state = True  # matches C++ default (assume alive)

    def detect_hud(self, frame):
        h, w = frame.shape[:2]
        x1, y1 = int(HUD_ROI[0] * w), int(HUD_ROI[1] * h)
        x2, y2 = int(HUD_ROI[2] * w), int(HUD_ROI[3] * h)
        if x2 <= x1 or y2 <= y1:
            return 0.0, False
        roi = frame[y1:y2, x1:x2]
        gray = cv2.cvtColor(roi, cv2.COLOR_BGR2GRAY)
        gray = cv2.resize(gray, HUD_CANON, interpolation=cv2.INTER_LINEAR)
        best = 0.0
        for t in self.templates:
            if t.shape[1] > gray.shape[1] or t.shape[0] > gray.shape[0]:
                continue
            res = cv2.matchTemplate(gray, t, cv2.TM_CCOEFF_NORMED)
            _, mv, _, _ = cv2.minMaxLoc(res)
            if mv > best:
                best = mv
        # Hysteresis (same thresholds as C++).
        if self.present_state:
            present = best >= HUD_ABSENT_THRESHOLD
        else:
            present = best >= HUD_THRESHOLD
        self.present_state = present
        return best, present

    def detect_equip(self, frame):
        if self.etmpl is None:
            return 0.0, False
        h, w = frame.shape[:2]
        x1, y1 = int(EQUIP_ROI[0] * w), int(EQUIP_ROI[1] * h)
        x2, y2 = int(EQUIP_ROI[2] * w), int(EQUIP_ROI[3] * h)
        if x2 <= x1 or y2 <= y1:
            return 0.0, False
        roi = frame[y1:y2, x1:x2]
        gray = cv2.cvtColor(roi, cv2.COLOR_BGR2GRAY)
        gray = cv2.resize(gray, EQUIP_CANON, interpolation=cv2.INTER_LINEAR)
        if self.etmpl.shape[1] > gray.shape[1] or self.etmpl.shape[0] > gray.shape[0]:
            return 0.0, False
        res = cv2.matchTemplate(gray, self.etmpl, cv2.TM_CCOEFF_NORMED)
        _, mv, _, _ = cv2.minMaxLoc(res)
        return mv, mv >= EQUIP_THRESHOLD


class StateMachine:
    """Python port of src/state/state_machine.cpp with the same thresholds/logic."""
    def __init__(self, logfn):
        self.log = logfn
        self.hud_missing_threshold = 5
        self.result_confirm_threshold = 2
        self.death_switch_delay_ms = 3000
        self.hud_respawn_threshold = 5
        self.reset()

    def reset(self):
        self.hud_seen = False
        self.hud_missing_frames = 0
        self.hud_present_frames = 0
        self.pending_death_since = 0
        self.result_active = False
        self.result_confirm_frames = 0
        self.result_absent_frames = 0
        self.state = "Idle"  # Idle | InGame | OnVideo

    def _now(self):
        return int(time.time() * 1000)

    def update(self, hud_present, result_found, equip_found, equip_conf):
        # ---- result latch ----
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
                self.hud_seen = False
                self.hud_missing_frames = 0
                self.pending_death_since = 0
                if self.state in ("OnVideo", "InGame"):
                    self.log("[SM] switching back to game (match/round end).")
                self.state = "Idle"
            return

        # Re-arm only after a long sustained absence (safety net).
        if self.result_absent_frames >= 90:
            self.result_active = False

        # ---- HUD branch ----
        if hud_present:
            self.hud_seen = True
            self.hud_missing_frames = 0
            self.pending_death_since = 0
            self.hud_present_frames += 1
            if not result_found:
                self.result_active = False
                self.result_confirm_frames = 0
                self.result_absent_frames = 0
            if self.state == "OnVideo":
                if self.hud_present_frames >= self.hud_respawn_threshold:
                    self.log("[SM] HUD stable after respawn; switching back to game.")
                    self.state = "InGame"
            else:
                self.state = "InGame"
        else:
            self.hud_present_frames = 0  # any gap resets the respawn debounce
            if self.hud_seen:
                self.hud_missing_frames += 1
                # Equipment/backpack icon while HUD bar is gone -> player is
                # alive (changing loadout). Cancel the death countdown.
                if self.state == "InGame" and equip_found:
                    self.log(f"[SM] Equipment icon detected during death countdown; "
                             f"cancelling switch (conf={equip_conf:.3f}).")
                    self.hud_missing_frames = 0
                    self.pending_death_since = 0
                else:
                    if self.hud_missing_frames >= self.hud_missing_threshold:
                        now = self._now()
                        if self.pending_death_since == 0:
                            self.pending_death_since = now
                        if self.state == "InGame" and (now - self.pending_death_since) >= self.death_switch_delay_ms:
                            self.log("[SM] HUD absent after seen; switching to video.")
                            self.state = "OnVideo"
            else:
                self.hud_missing_frames = 0
                self.pending_death_since = 0
                self.state = "Idle"


# ---- optional OCR via Windows.Media.Ocr ----
OCR = None
async def ocr_result(frame, engine):
    h, w = frame.shape[:2]
    x1, y1 = int(RESULT_ROI[0] * w), int(RESULT_ROI[1] * h)
    x2, y2 = int(RESULT_ROI[2] * w), int(RESULT_ROI[3] * h)
    if x2 <= x1 or y2 <= y1:
        return None
    roi = frame[y1:y2, x1:x2]
    rh = y2 - y1
    if rh < UPSCALE_MIN_H:
        scale = UPSCALE_MIN_H / max(1, rh)
        roi = cv2.resize(roi, None, fx=scale, fy=scale, interpolation=cv2.INTER_LINEAR)
    bgra = cv2.cvtColor(roi, cv2.COLOR_BGR2BGRA)
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
    ap.add_argument("--seconds", type=int, default=30)
    ap.add_argument("--title", default="使命召唤手游")
    args = ap.parse_args()

    det = HudDetector()
    if not det.templates:
        print("No HUD templates loaded; exiting.")
        return

    engine = try_init_ocr()
    ocr_ok = engine is not None
    print(f"OCR: {'enabled' if ocr_ok else 'disabled (will validate HUD/equip only)'}")

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
    emit(f"HUD  ROI={HUD_ROI} canonical={HUD_CANON} templates={[t.shape for t in det.templates]}")
    emit(f"EQUIP ROI={EQUIP_ROI} canonical={EQUIP_CANON} template={'loaded' if det.etmpl is not None else 'MISSING'}")
    emit(f"thresholds: hud={HUD_THRESHOLD}/{HUD_ABSENT_THRESHOLD} equip={EQUIP_THRESHOLD}")

    sm = StateMachine(emit)
    start = time.time()
    last_hb = 0
    init_hud = None
    init_equip = None
    result_found = False
    frame_i = 0
    try:
        while time.time() - start < args.seconds:
            frame = capture_window(hwnd)
            if frame is None:
                time.sleep(0.2)
                continue
            hud_conf, hud_present = det.detect_hud(frame)
            equip_conf, equip_found = det.detect_equip(frame)

            kw = None
            if ocr_ok and (frame_i % 2 == 0):
                try:
                    kw = asyncio.run(ocr_result(frame, engine))
                    result_found = kw is not None
                except Exception:
                    kw = None

            sm.update(hud_present, result_found, equip_found, equip_conf)

            if init_hud is None or hud_present != init_hud:
                emit(f"[HUD] {'Present' if hud_present else 'Absent'} conf={hud_conf:.3f}")
                init_hud = hud_present
            if init_equip is None or equip_found != init_equip:
                emit(f"[EQUIP] {'YES' if equip_found else 'no'} conf={equip_conf:.3f}")
                init_equip = equip_found

            now = time.time()
            if now - last_hb > 2:
                last_hb = now
                emit(f"[hb] hud={'P' if hud_present else 'A'} conf={hud_conf:.3f} "
                     f"equip={'YES' if equip_found else 'no'} econf={equip_conf:.3f} "
                     f"result={kw if kw else '-'} sm={sm.state}")

            frame_i += 1
            time.sleep(0.3)
    except KeyboardInterrupt:
        pass
    emit("=== diag stop ===")
    log.close()


if __name__ == "__main__":
    main()
