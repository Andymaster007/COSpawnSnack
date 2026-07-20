#include "detection/hud_detector.h"
#include "core/logger.h"
#include <opencv2/opencv.hpp>
#include <opencv2/imgproc.hpp>
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

void HudDetector::SetTemplates(const std::vector<std::string>& paths) {
    std::lock_guard<std::mutex> lock(mutex_);
    templates_.clear();
    for (const auto& p : paths) {
        cv::Mat t = cv::imread(p, cv::IMREAD_GRAYSCALE | cv::IMREAD_UNCHANGED);
        if (t.channels() == 4) {
            // Drop alpha; convert BGRA -> gray.
            cv::cvtColor(t, t, cv::COLOR_BGRA2GRAY);
        } else if (t.channels() == 3) {
            cv::cvtColor(t, t, cv::COLOR_BGR2GRAY);
        }
        if (t.empty()) {
            CSN_LOG_ERROR("Failed to load HUD template: " + p);
        } else {
            templates_.push_back(t);
            CSN_LOG_INFO("Loaded HUD template: " + p + " " + std::to_string(t.cols) + "x" + std::to_string(t.rows));
        }
    }
}

void HudDetector::SetThreshold(double threshold) {
    threshold_ = threshold;
}

void HudDetector::SetAbsentThreshold(double threshold) {
    absent_threshold_ = threshold;
}

void HudDetector::SetRoi(const RationalRect& roi) {
    roi_ = roi;
    canonical_w_ = static_cast<int>(std::max(1.0, (roi.right - roi.left) * kRefWidth));
    canonical_h_ = static_cast<int>(std::max(1.0, (roi.bottom - roi.top) * kRefHeight));
}

void HudDetector::SetEquipmentTemplate(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    cv::Mat t = cv::imread(path, cv::IMREAD_GRAYSCALE | cv::IMREAD_UNCHANGED);
    if (t.channels() == 4) {
        cv::cvtColor(t, t, cv::COLOR_BGRA2GRAY);
    } else if (t.channels() == 3) {
        cv::cvtColor(t, t, cv::COLOR_BGR2GRAY);
    }
    if (t.empty()) {
        CSN_LOG_ERROR("Failed to load equipment template: " + path);
    } else {
        equipment_template_ = t;
        CSN_LOG_INFO("Loaded equipment template: " + path + " " + std::to_string(t.cols) + "x" + std::to_string(t.rows));
    }
}

void HudDetector::SetEquipmentRoi(const RationalRect& roi) {
    std::lock_guard<std::mutex> lock(mutex_);
    equipment_roi_ = roi;
    equipment_canonical_w_ = static_cast<int>(std::max(1.0, (roi.right - roi.left) * kRefWidth));
    equipment_canonical_h_ = static_cast<int>(std::max(1.0, (roi.bottom - roi.top) * kRefHeight));
}

void HudDetector::SetEquipmentThreshold(double threshold) {
    std::lock_guard<std::mutex> lock(mutex_);
    equipment_threshold_ = threshold;
}

HudResult HudDetector::Detect(const cv::Mat& frame) {
    try {
        std::lock_guard<std::mutex> lock(mutex_);
        HudResult result;
        if (templates_.empty() || frame.empty()) {
            result.presence = HudResult::Presence::Unknown;
            return result;
        }

        PixelRect pr = ToPixelRect(roi_, frame.cols, frame.rows);
        int x = std::clamp(pr.left, 0, frame.cols - 1);
        int y = std::clamp(pr.top, 0, frame.rows - 1);
        int rw = std::clamp(pr.right - pr.left, 1, frame.cols - x);
        int rh = std::clamp(pr.bottom - pr.top, 1, frame.rows - y);
        cv::Rect roi(x, y, rw, rh);

        cv::Mat gray;
        cv::cvtColor(frame(roi), gray, cv::COLOR_BGR2GRAY);

        // Resolution independence: resample the live ROI crop to the canonical
        // canvas (which corresponds to the resolution at which the templates
        // were extracted). The templates are kept at their extracted sizes and
        // matched inside this canvas.
        if (canonical_w_ > 0 && canonical_h_ > 0) {
            cv::resize(gray, gray, cv::Size(canonical_w_, canonical_h_), 0, 0, cv::INTER_LINEAR);
        }

        double best = 0.0;
        for (const auto& t : templates_) {
            if (t.cols > gray.cols || t.rows > gray.rows) continue;
            cv::Mat res;
            cv::matchTemplate(gray, t, res, cv::TM_CCOEFF_NORMED);
            double minVal = 0.0, maxVal = 0.0;
            cv::Point minLoc, maxLoc;
            cv::minMaxLoc(res, &minVal, &maxVal, &minLoc, &maxLoc);
            if (maxVal > best) best = maxVal;
        }

        // Hysteresis: require a confident score to *enter* Present, but only
        // drop to Absent once the score falls well below that. This stops the
        // "damage darkening" effect (low health crushes the bar's contrast) from
        // flickering HUD -> Absent while the player is still alive (which would
        // falsely trigger the death/video switch). Genuine disappearance (death
        // cam / round-end screen, score ~0.1-0.3) still crosses below
        // absent_threshold_ and is detected.
        bool present;
        if (present_state_) {
            present = (best >= absent_threshold_);
        } else {
            present = (best >= threshold_);
        }
        present_state_ = present;

        result.confidence = best;
        result.presence = present ? HudResult::Presence::Present : HudResult::Presence::Absent;
        return result;
    } catch (const std::exception& e) {
        CSN_LOG_ERROR("HUD detection exception: " + std::string(e.what()));
        HudResult r;
        r.presence = HudResult::Presence::Unknown;
        return r;
    } catch (...) {
        CSN_LOG_ERROR("HUD detection unknown exception.");
        HudResult r;
        r.presence = HudResult::Presence::Unknown;
        return r;
    }
}

EquipmentResult HudDetector::DetectEquipment(const cv::Mat& frame) {
    try {
        std::lock_guard<std::mutex> lock(mutex_);
        EquipmentResult result;
        if (equipment_template_.empty() || frame.empty()) {
            return result;
        }

        PixelRect pr = ToPixelRect(equipment_roi_, frame.cols, frame.rows);
        int x = std::clamp(pr.left, 0, frame.cols - 1);
        int y = std::clamp(pr.top, 0, frame.rows - 1);
        int rw = std::clamp(pr.right - pr.left, 1, frame.cols - x);
        int rh = std::clamp(pr.bottom - pr.top, 1, frame.rows - y);
        cv::Rect roi(x, y, rw, rh);

        cv::Mat gray;
        cv::cvtColor(frame(roi), gray, cv::COLOR_BGR2GRAY);

        if (equipment_canonical_w_ > 0 && equipment_canonical_h_ > 0) {
            cv::resize(gray, gray, cv::Size(equipment_canonical_w_, equipment_canonical_h_), 0, 0, cv::INTER_LINEAR);
        }

        if (equipment_template_.cols > gray.cols || equipment_template_.rows > gray.rows) {
            return result;
        }

        cv::Mat res;
        cv::matchTemplate(gray, equipment_template_, res, cv::TM_CCOEFF_NORMED);
        double minVal = 0.0, maxVal = 0.0;
        cv::Point minLoc, maxLoc;
        cv::minMaxLoc(res, &minVal, &maxVal, &minLoc, &maxLoc);

        result.confidence = maxVal;
        result.found = (maxVal >= equipment_threshold_);
        return result;
    } catch (const std::exception& e) {
        CSN_LOG_ERROR("Equipment detection exception: " + std::string(e.what()));
        return EquipmentResult();
    } catch (...) {
        CSN_LOG_ERROR("Equipment detection unknown exception.");
        return EquipmentResult();
    }
}

} // namespace csn
