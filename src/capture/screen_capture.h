#pragma once
#include <opencv2/opencv.hpp>
#include <functional>
#include <atomic>
#include <thread>
#include <Windows.h>

namespace csn {

class ScreenCapture {
public:
    using FrameCallback = std::function<void(const cv::Mat& frame, int width, int height, int dpi)>;

    ScreenCapture();
    ~ScreenCapture();

    bool Start(HWND hwnd, int fps, FrameCallback cb);
    void Stop();
    bool IsRunning() const;

private:
    void CaptureLoop();

    HWND hwnd_ = nullptr;
    int fps_ = 10;
    FrameCallback callback_;
    std::atomic<bool> running_{false};
    std::thread thread_;
};

} // namespace csn
