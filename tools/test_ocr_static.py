import os, asyncio, sys
import numpy as np
from PIL import Image
import cv2
import winrt.windows.media.ocr as ocr_mod
from winrt.windows.globalization import Language
import winrt.windows.storage.streams as streams
from winrt.windows.graphics.imaging import BitmapDecoder

RESULT_ROI = (0.30, 0.22, 0.70, 0.52)
UPSCALE_MIN_H = 360
KEYWORDS = ["胜利", "战败", "失败", "VICTORY", "DEFEAT"]

eng = ocr_mod.OcrEngine.try_create_from_language(Language("zh-CN"))
print("OCR engine (zh-CN):", "OK" if eng else "NULL")
if eng is None:
    eng = ocr_mod.OcrEngine.try_create_from_user_profile_language()
    print("fallback profile engine:", "OK" if eng else "NULL")
assert eng is not None, "no OCR engine"

async def recognize(np_bgra):
    ok, png = cv2.imencode(".png", np_bgra)
    ras = streams.InMemoryRandomAccessStream()
    dw = streams.DataWriter(ras)
    dw.write_bytes(bytes(png.tobytes()))
    await dw.store_async()
    await dw.flush_async()
    decoder = await BitmapDecoder.create_async(ras)
    bmp = await decoder.get_software_bitmap_async()
    res = await eng.recognize_async(bmp)
    return res.text

def test(path):
    print("\n=== ", os.path.basename(path), "===")
    im = Image.open(path).convert("RGB")
    arr = np.asarray(im)
    bgr = arr[:, :, ::-1].copy()
    h, w = bgr.shape[:2]
    x1, y1 = int(RESULT_ROI[0]*w), int(RESULT_ROI[1]*h)
    x2, y2 = int(RESULT_ROI[2]*w), int(RESULT_ROI[3]*h)
    roi = bgr[y1:y2, x1:x2]
    rh = y2 - y1
    if rh < UPSCALE_MIN_H:
        s = UPSCALE_MIN_H / max(1, rh)
        roi = cv2.resize(roi, None, fx=s, fy=s, interpolation=cv2.INTER_LINEAR)
    bgra = cv2.cvtColor(roi, cv2.COLOR_BGR2BGRA)
    text = asyncio.run(recognize(bgra))
    norm = "".join(text.split())
    print("OCR text:", repr(text), "-> normalized:", repr(norm))
    for kw in KEYWORDS:
        if kw.lower() in norm.lower():
            print("  MATCHED keyword:", kw); break
    else:
        print("  no keyword matched")

for f in ["失败结算.png", "对局胜利（大胜利）.png", "回合胜利（小胜利）.png"]:
    p = os.path.join("C:/Users/Andy/Pictures/Screenshots", f)
    if os.path.exists(p):
        test(p)
    else:
        print("missing", p)
