#pragma once
#include "core/types.h"
#include <string>
#include <vector>
#include <mutex>

// Forward declare to avoid pulling all OpenCV headers here.
namespace cv { class Mat; }

namespace csn {

// Reference resolution used to derive the resolution-independent matching
// canvas. The HUD template and the live ROI crop are both resampled to this
// canvas before comparison, so detection works on any device resolution
// (CODM is locked to 16:9). The canvas size is `roi_fraction * kRefSize`.
constexpr int kRefWidth = 1920;
constexpr int kRefHeight = 1080;

class HudDetector {
public:
    void SetTemplates(const std::vector<std::string>& paths);
    void SetThreshold(double threshold);
    void SetRoi(const RationalRect& roi);

    HudResult Detect(const cv::Mat& frame);

private:
    // Resample every loaded template to the current canonical canvas size.
    void RebuildScaledTemplates();

    double threshold_ = 0.65;
    RationalRect roi_;
    int canonical_w_ = 0;
    int canonical_h_ = 0;
    bool canonical_valid_ = false;
    std::vector<cv::Mat> templates_;       // raw templates as loaded from disk
    std::vector<cv::Mat> scaled_templates_; // resampled to the canonical canvas
    std::mutex mutex_;
};

} // namespace csn
