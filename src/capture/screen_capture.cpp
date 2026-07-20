#include "capture/screen_capture.h"
#include "core/logger.h"

#include <opencv2/opencv.hpp>
#include <Windows.h>
#include <thread>
#include <chrono>

namespace csn {

ScreenCapture::ScreenCapture() = default;
ScreenCapture::~ScreenCapture() { Stop(); }

bool ScreenCapture::Start(HWND hwnd, int fps, FrameCallback cb) {
    if (running_) return false;
    if (!IsWindow(hwnd)) {
        CSN_LOG_ERROR("Capture target is not a valid window.");
        return false;
    }
    hwnd_ = hwnd;
    fps_ = fps;
    callback_ = std::move(cb);
    running_ = true;
    thread_ = std::thread(&ScreenCapture::CaptureLoop, this);
    CSN_LOG_INFO("Screen capture started (BitBlt fallback).");
    return true;
}

void ScreenCapture::Stop() {
    running_ = false;
    if (thread_.joinable()) thread_.join();
}

bool ScreenCapture::IsRunning() const { return running_; }

void ScreenCapture::CaptureLoop() {
    HDC hdcWindow = nullptr;
    HDC hdcMem = nullptr;
    HBITMAP hbm = nullptr;
    HBITMAP oldBmp = nullptr;
    int last_w = 0, last_h = 0;

    while (running_) {
        RECT rc;
        if (!GetWindowRect(hwnd_, &rc)) break;

        int w = rc.right - rc.left;
        int h = rc.bottom - rc.top;
        if (w <= 0 || h <= 0) break;

        if (w != last_w || h != last_h || !hdcWindow) {
            if (oldBmp) SelectObject(hdcMem, oldBmp);
            if (hbm) DeleteObject(hbm);
            if (hdcMem) DeleteDC(hdcMem);
            if (hdcWindow) ReleaseDC(hwnd_, hdcWindow);

            hdcWindow = GetWindowDC(hwnd_);
            hdcMem = CreateCompatibleDC(hdcWindow);
            hbm = CreateCompatibleBitmap(hdcWindow, w, h);
            oldBmp = (HBITMAP)SelectObject(hdcMem, hbm);
            last_w = w;
            last_h = h;
        }

        if (!BitBlt(hdcMem, 0, 0, w, h, hdcWindow, 0, 0, SRCCOPY)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        BITMAPINFO bmi = {};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = w;
        bmi.bmiHeader.biHeight = -h; // top-down
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        cv::Mat mat(h, w, CV_8UC4);
        GetDIBits(hdcMem, hbm, 0, h, mat.data, &bmi, DIB_RGB_COLORS);

        cv::Mat bgr;
        cv::cvtColor(mat, bgr, cv::COLOR_BGRA2BGR);

        int dpi = 96; // TODO: GetDpiForWindow on Windows 10 1607+
        if (callback_) callback_(bgr, w, h, dpi);

        auto sleep_ms = std::chrono::milliseconds(1000 / std::max(fps_, 1));
        std::this_thread::sleep_for(sleep_ms);
    }

    if (oldBmp) SelectObject(hdcMem, oldBmp);
    if (hbm) DeleteObject(hbm);
    if (hdcMem) DeleteDC(hdcMem);
    if (hdcWindow) ReleaseDC(hwnd_, hdcWindow);
    CSN_LOG_INFO("Screen capture stopped.");
}

} // namespace csn
