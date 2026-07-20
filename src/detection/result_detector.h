#pragma once
#include "core/types.h"
#include <string>
#include <vector>
#include <winrt/Windows.Media.Ocr.h>

namespace cv { class Mat; }

namespace csn {

class ResultDetector {
public:
    ResultDetector();
    ~ResultDetector();

    void SetRoi(const RationalRect& roi);
    void SetKeywords(const std::vector<std::string>& keywords);
    void SetConfidenceThreshold(double threshold);

    ResultText Detect(const cv::Mat& frame);

private:
    void DetectImplInit();

private:
    RationalRect roi_;
    std::vector<std::string> keywords_;
    double threshold_ = 0.6;

    winrt::Windows::Media::Ocr::OcrEngine engine_{nullptr};
    bool ocr_initialized_ = false;
};

} // namespace csn
