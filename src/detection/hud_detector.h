#pragma once
#include "core/types.h"
#include <opencv2/core.hpp>
#include <string>
#include <vector>
#include <mutex>

// Forward declare to avoid pulling all OpenCV headers here.
namespace cv { class Mat; }

namespace csn {

// Reference resolution used to derive the resolution-independent matching
// canvas. The live ROI crop is resampled to this canvas before comparison,
// so detection works on any device resolution (CODM is locked to 16:9). The
// canvas size is `roi_fraction * kRefSize`. Templates should be extracted at
// this reference resolution (or cropped from such an extraction) and are NOT
// resampled further.
constexpr int kRefWidth = 1920;
constexpr int kRefHeight = 1080;

class HudDetector {
public:
    void SetTemplates(const std::vector<std::string>& paths);
    void SetThreshold(double threshold);
    void SetRoi(const RationalRect& roi);

    HudResult Detect(const cv::Mat& frame);
    void SetAbsentThreshold(double threshold);

    // Equipment icon (e.g. "F 装备") detection. Used to distinguish "HUD bar
    // hidden because the loadout/backpack screen is open" from actual death.
    void SetEquipmentTemplate(const std::string& path);
    void SetEquipmentRoi(const RationalRect& roi);
    void SetEquipmentThreshold(double threshold);
    EquipmentResult DetectEquipment(const cv::Mat& frame);

private:
    double threshold_ = 0.65;       // "become Present" gate (rising edge)
    double absent_threshold_ = 0.35; // "stay Present" floor (falling edge)
    bool present_state_ = true;      // hysteresis memory; start optimistic so a
                                     // low-health first frame still reads Present
    RationalRect roi_;
    int canonical_w_ = 0;
    int canonical_h_ = 0;
    std::vector<cv::Mat> templates_;  // grayscale, at canonical resolution
    std::mutex mutex_;

    // Equipment icon detection state.
    RationalRect equipment_roi_;
    int equipment_canonical_w_ = 0;
    int equipment_canonical_h_ = 0;
    cv::Mat equipment_template_;      // grayscale
    double equipment_threshold_ = 0.65;
};

} // namespace csn
