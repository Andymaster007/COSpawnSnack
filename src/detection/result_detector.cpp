#include "detection/result_detector.h"
#include "core/logger.h"
#include <opencv2/opencv.hpp>
#include <algorithm>

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

} // namespace

void ResultDetector::SetRoi(const RationalRect& roi) {
    roi_ = roi;
}

void ResultDetector::SetKeywords(const std::vector<std::string>& keywords) {
    keywords_ = keywords;
}

void ResultDetector::SetConfidenceThreshold(double threshold) {
    threshold_ = threshold;
}

ResultText ResultDetector::Detect(const cv::Mat& frame) {
    ResultText result;
    if (frame.empty()) return result;

    PixelRect pr = ToPixelRect(roi_, frame.cols, frame.rows);
    int x = std::clamp(pr.left, 0, frame.cols - 1);
    int y = std::clamp(pr.top, 0, frame.rows - 1);
    int rw = std::clamp(pr.right - pr.left, 1, frame.cols - x);
    int rh = std::clamp(pr.bottom - pr.top, 1, frame.rows - y);
    cv::Rect roi(x, y, rw, rh);

    // STUB: Windows.Media.Ocr integration is the next step once the MSVC +
    // Windows SDK toolchain is available. For now, this detector never fires,
    // which lets the rest of the app compile and run in "HUD-only" demo mode.
    // Replace this block with a real OCR call (see docs/ARCHITECTURE.md).
    (void)roi;
    (void)threshold_;
    (void)keywords_;

    return result;
}

} // namespace csn
