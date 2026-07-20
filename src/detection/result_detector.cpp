#include "detection/result_detector.h"
#include "core/logger.h"

#include <opencv2/opencv.hpp>
#include <algorithm>
#include <Windows.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Globalization.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <windows.graphics.imaging.interop.h>

using namespace winrt;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Media::Ocr;
using namespace winrt::Windows::Graphics::Imaging;

namespace csn {

namespace {

PixelRect ToPixelRect(const RationalRect& r, int w, int h) {
    PixelRect pr;
    pr.left = static_cast<int>(r.left * w);
    pr.top = static_cast<int>(r.top * h);
    pr.right = static_cast<int>(r.right * w);
    pr.bottom = static_cast<int>(r.bottom * h);
    return pr;
}

std::string WStringToUtf8(const std::wstring& s) {
    if (s.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                                   nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string out(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                        out.data(), size, nullptr, nullptr);
    return out;
}

std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string RemoveWhitespace(std::string s) {
    s.erase(std::remove_if(s.begin(), s.end(),
                           [](unsigned char c) { return std::isspace(c); }),
            s.end());
    return s;
}

} // namespace

ResultDetector::ResultDetector() = default;
ResultDetector::~ResultDetector() = default;

void ResultDetector::SetRoi(const RationalRect& roi) { roi_ = roi; }

void ResultDetector::SetKeywords(const std::vector<std::string>& keywords) {
    keywords_ = keywords;
}

void ResultDetector::SetConfidenceThreshold(double threshold) {
    threshold_ = threshold;
}

void ResultDetector::SetUpscaleMinHeight(int height) {
    upscale_min_height_ = std::max(1, height);
}

void ResultDetector::DetectImplInit() {
    // Prefer a Chinese engine (the game shows 胜利/失败); fall back to the
    // user-profile language, then disable OCR if no engine is available.
    engine_ = OcrEngine::TryCreateFromLanguage(
        winrt::Windows::Globalization::Language(L"zh-CN"));
    if (!engine_) {
        engine_ = OcrEngine::TryCreateFromUserProfileLanguages();
    }
    if (engine_) {
        auto tag = engine_.RecognizerLanguage().LanguageTag();
        CSN_LOG_INFO("OCR engine ready (language: " + WStringToUtf8(tag.c_str()) + ").");
    } else {
        CSN_LOG_WARN("OCR engine unavailable. Install the Windows OCR language pack "
                     "(Settings > Language > Chinese (Simplified) > OCR) so result "
                     "text can be recognized. Result detection is disabled for now.");
    }
    ocr_initialized_ = true;
}

ResultText ResultDetector::Detect(const cv::Mat& frame) {
    ResultText result;
    if (frame.empty()) return result;

    if (!ocr_initialized_) DetectImplInit();
    if (!engine_) return result;

    PixelRect pr = ToPixelRect(roi_, frame.cols, frame.rows);
    int x = std::clamp(pr.left, 0, frame.cols - 1);
    int y = std::clamp(pr.top, 0, frame.rows - 1);
    int rw = std::clamp(pr.right - pr.left, 1, frame.cols - x);
    int rh = std::clamp(pr.bottom - pr.top, 1, frame.rows - y);
    cv::Mat roi = frame(cv::Rect(x, y, rw, rh));

    // Resolution independence: on low-res windows the result text is only a
    // few pixels tall and OCR misses it. Upscale the ROI so its height reaches
    // at least `upscale_min_height_` before recognition. The ROI position is
    // already resolution-independent (screen percentages), this only fixes
    // the text pixel size.
    if (roi.rows < upscale_min_height_ && roi.rows > 0) {
        const double s = static_cast<double>(upscale_min_height_) / roi.rows;
        cv::Mat up;
        cv::resize(roi, up, cv::Size(), s, s, cv::INTER_LINEAR);
        roi = up;
    }

    // OpenCV BGR -> BGRA8 (the format Windows.Media.Ocr expects).
    cv::Mat bgra;
    if (roi.channels() == 3) {
        cv::cvtColor(roi, bgra, cv::COLOR_BGR2BGRA);
    } else if (roi.channels() == 4) {
        bgra = roi;
    } else {
        return result;  // unsupported layout
    }

    SoftwareBitmap bitmap(BitmapPixelFormat::Bgra8, bgra.cols, bgra.rows);
    auto buffer = bitmap.LockBuffer(BitmapBufferAccessMode::ReadWrite);
    auto reference = buffer.CreateReference();
    // IMemoryBufferReference exposes data()/Capacity(), backed by IMemoryBufferByteAccess.
    uint8_t* dst = reference.data();
    const uint32_t capacity = reference.Capacity();
    const size_t need = static_cast<size_t>(bgra.total()) * bgra.channels();
    if (capacity < need) return result;
    memcpy(dst, bgra.data, need);
    // `reference`/`buffer` go out of scope here and unlock the bitmap.

    auto ocrResult = engine_.RecognizeAsync(bitmap).get();

    std::wstring wtext;
    for (auto const& line : ocrResult.Lines()) {
        wtext += line.Text().c_str();
        wtext += L'\n';
    }
    std::string text = RemoveWhitespace(ToLower(WStringToUtf8(wtext)));

    for (auto const& kw : keywords_) {
        if (text.find(ToLower(kw)) != std::string::npos) {
            result.found = true;
            result.matched_keyword = kw;
            result.confidence = std::max(threshold_, 1.0);  // OCR API has no per-match score
            break;
        }
    }
    return result;
}

} // namespace csn
