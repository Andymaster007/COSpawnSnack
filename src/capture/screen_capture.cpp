#include "capture/screen_capture.h"
#include "core/logger.h"

#include <opencv2/opencv.hpp>
#include <Windows.h>
#include <thread>
#include <chrono>
#include <algorithm>

// WGC interop helpers (free functions CreateForWindow / GetInterfaceFromObject).
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>

using namespace winrt;
using namespace winrt::Windows::Graphics::Capture;
using namespace winrt::Windows::Graphics::DirectX;
using namespace winrt::Windows::Foundation;

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
    thread_ = std::thread(&ScreenCapture::ThreadMain, this);
    return true;
}

void ScreenCapture::Stop() {
    running_ = false;
    frameCv_.notify_all();
    if (thread_.joinable()) thread_.join();
}

bool ScreenCapture::IsRunning() const { return running_; }

void ScreenCapture::ThreadMain() {
    // Initialize a multi-threaded WinRT apartment for this thread. Both the WGC
    // callback (OCR runs on the capture thread) and Windows.Media.Ocr need it.
    init_apartment(apartment_type::multi_threaded);

    if (InitWgc()) {
        CSN_LOG_INFO("Screen capture started (WGC / Windows Graphics Capture).");
        WgcLoop();
        CleanupWgc();
    } else {
        CSN_LOG_WARN("WGC unavailable; falling back to BitBlt capture.");
        BitBltLoop();
    }
    CSN_LOG_INFO("Screen capture stopped.");
}

bool ScreenCapture::InitWgc() {
    // 1) D3D11 device with BGRA support (required by the WGC frame pool).
    HRESULT hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        nullptr, 0,
        D3D11_SDK_VERSION,
        d3dDevice_.put(),
        nullptr,
        nullptr);
    if (FAILED(hr)) {
        CSN_LOG_WARN("D3D11CreateDevice failed (0x" + std::to_string(hr) + ").");
        return false;
    }
    d3dDevice_.as(dxgiDevice_);

    // 2) Wrap the DXGI device as a winrt IDirect3DDevice (what the frame pool needs).
    winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice device{ nullptr };
    {
        winrt::com_ptr<::IInspectable> inspectable;
        winrt::check_hresult(
            CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice_.get(), inspectable.put()));
        device = inspectable.as<winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice>();
    }
    // Immediate context for staging copy / map (CopyResource/Map/Unmap live on the context).
    d3dDevice_->GetImmediateContext(d3dContext_.put());

    // 3) Capture item from the game window HWND, via the interop activation factory.
    winrt::Windows::Graphics::Capture::GraphicsCaptureItem item{ nullptr };
    auto interop = winrt::get_activation_factory<
        winrt::Windows::Graphics::Capture::GraphicsCaptureItem,
        IGraphicsCaptureItemInterop>();
    hr = interop->CreateForWindow(
        hwnd_,
        winrt::guid_of<winrt::Windows::Graphics::Capture::GraphicsCaptureItem>(),
        winrt::put_abi(item));
    if (FAILED(hr) || !item) {
        CSN_LOG_WARN("CreateForWindow failed (0x" + std::to_string(hr) + ").");
        return false;
    }

    // 4) Free-threaded frame pool (no DispatcherQueue required for a background app).
    framePool_ = Direct3D11CaptureFramePool::CreateFreeThreaded(
        device,
        DirectXPixelFormat::B8G8R8A8UIntNormalized,
        2,
        item.Size());
    session_ = framePool_.CreateCaptureSession(item);
    frameArrivedToken_ = framePool_.FrameArrived(
        {this, &ScreenCapture::OnFrameArrived});
    session_.StartCapture();
    return true;
}

void ScreenCapture::CleanupWgc() {
    if (framePool_ && frameArrivedToken_.value != 0) {
        framePool_.FrameArrived(frameArrivedToken_);
        frameArrivedToken_ = {};
    }
    if (session_) {
        session_.Close();
        session_ = nullptr;
    }
    if (framePool_) {
        framePool_.Close();
        framePool_ = nullptr;
    }
    dxgiDevice_ = nullptr;
    d3dDevice_ = nullptr;
}

void ScreenCapture::OnFrameArrived(const Direct3D11CaptureFramePool& sender,
                                   const winrt::Windows::Foundation::IInspectable&) {
    auto frame = sender.TryGetNextFrame();
    if (!frame) return;
    auto surface = frame.Surface();
    if (!surface) return;

    // The winrt IDirect3DSurface exposes the underlying DXGI texture through the
    // raw COM interface IDirect3DDxgiInterfaceAccess (global namespace, declared
    // in the SDK's windows.graphics.directx.direct3d11.interop.h).
    winrt::com_ptr<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess> access;
    winrt::check_hresult(
        reinterpret_cast<::IUnknown*>(winrt::get_abi(surface))
            ->QueryInterface(
                __uuidof(::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess),
                access.put_void()));
    if (!access) return;

    winrt::com_ptr<ID3D11Texture2D> texture;
    winrt::check_hresult(access->GetInterface(__uuidof(ID3D11Texture2D), texture.put_void()));
    if (!texture) return;

    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);
    if (desc.Width == 0 || desc.Height == 0) return;

    D3D11_TEXTURE2D_DESC staged = desc;
    staged.Usage = D3D11_USAGE_STAGING;
    staged.BindFlags = 0;
    staged.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    staged.MiscFlags = 0;
    winrt::com_ptr<ID3D11Texture2D> staging;
    HRESULT hr = d3dDevice_->CreateTexture2D(&staged, nullptr, staging.put());
    if (FAILED(hr)) return;
    d3dContext_->CopyResource(staging.get(), texture.get());

    D3D11_MAPPED_SUBRESOURCE mapped{};
    hr = d3dContext_->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) return;

    const int w = static_cast<int>(desc.Width);
    const int h = static_cast<int>(desc.Height);
    cv::Mat bgra(h, w, CV_8UC4, mapped.pData, mapped.RowPitch);
    cv::Mat bgr;
    cv::cvtColor(bgra, bgr, cv::COLOR_BGRA2BGR);

    {
        std::lock_guard<std::mutex> lk(frameMutex_);
        currentFrame_ = bgr;  // deep copy (bgra references mapped memory)
        newFrame_ = true;
    }
    d3dContext_->Unmap(staging.get(), 0);
    frameCv_.notify_one();
}

void ScreenCapture::WgcLoop() {
    const auto interval = std::chrono::milliseconds(1000 / std::max(fps_, 1));
    while (running_) {
        cv::Mat frame;
        {
            std::unique_lock<std::mutex> lk(frameMutex_);
            if (!frameCv_.wait_for(lk, interval, [&] { return !running_ || newFrame_; })) {
                continue;  // no new frame within the interval
            }
            if (!running_) break;
            newFrame_ = false;
            currentFrame_.copyTo(frame);
        }
        if (!frame.empty()) {
            callback_(frame, frame.cols, frame.rows, dpi_);
        }
    }
}

void ScreenCapture::BitBltLoop() {
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
        bmi.bmiHeader.biHeight = -h;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        cv::Mat mat(h, w, CV_8UC4);
        GetDIBits(hdcMem, hbm, 0, h, mat.data, &bmi, DIB_RGB_COLORS);
        cv::Mat bgr;
        cv::cvtColor(mat, bgr, cv::COLOR_BGRA2BGR);
        callback_(bgr, w, h, dpi_);

        std::this_thread::sleep_for(std::chrono::milliseconds(1000 / std::max(fps_, 1)));
    }

    if (oldBmp) SelectObject(hdcMem, oldBmp);
    if (hbm) DeleteObject(hbm);
    if (hdcMem) DeleteDC(hdcMem);
    if (hdcWindow) ReleaseDC(hwnd_, hdcWindow);
}

} // namespace csn
