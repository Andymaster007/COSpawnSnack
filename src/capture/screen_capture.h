#pragma once
#include "core/types.h"

#include <opencv2/opencv.hpp>
#include <functional>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <Windows.h>

// Windows Graphics Capture (WGC) + D3D11 prerequisites.
#include <d3d11.h>
#include <dxgi.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>

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
    bool InitWgc();
    void CleanupWgc();
    void ThreadMain();
    void WgcLoop();
    void OnFrameArrived(const winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool& sender,
                        const winrt::Windows::Foundation::IInspectable& args);
    void BitBltLoop();

    HWND hwnd_ = nullptr;
    int fps_ = 10;
    int dpi_ = 96;
    FrameCallback callback_;
    std::atomic<bool> running_{false};
    std::thread thread_;

    // WGC state (created/consumed on the capture thread).
    winrt::com_ptr<ID3D11Device> d3dDevice_;
    winrt::com_ptr<ID3D11DeviceContext> d3dContext_;
    winrt::com_ptr<IDXGIDevice> dxgiDevice_;
    winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool framePool_{nullptr};
    winrt::Windows::Graphics::Capture::GraphicsCaptureSession session_{nullptr};
    winrt::event_token frameArrivedToken_{};

    // Frame hand-off from the WGC callback thread to the capture loop thread.
    std::mutex frameMutex_;
    std::condition_variable frameCv_;
    cv::Mat currentFrame_;
    bool newFrame_ = false;
};

} // namespace csn
