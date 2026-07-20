#pragma once
#include "core/types.h"
#include <string>
#include <vector>
#include <mutex>

// Forward declare to avoid pulling all OpenCV headers here.
namespace cv { class Mat; }

namespace csn {

class HudDetector {
public:
    void SetTemplates(const std::vector<std::string>& paths);
    void SetThreshold(double threshold);
    void SetRoi(const RationalRect& roi);

    HudResult Detect(const cv::Mat& frame);

private:
    double threshold_ = 0.65;
    RationalRect roi_;
    std::vector<cv::Mat> templates_;
    std::mutex mutex_;
};

} // namespace csn
