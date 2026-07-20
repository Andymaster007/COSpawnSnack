#include "detection/hud_detector.h"
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
        }
    }
    RebuildScaledTemplates();
}

void HudDetector::SetThreshold(double threshold) {
    threshold_ = threshold;
}

void HudDetector::SetRoi(const RationalRect& roi) {
    roi_ = roi;
    canonical_w_ = static_cast<int>(std::max(1.0, (roi.right - roi.left) * kRefWidth));
    canonical_h_ = static_cast<int>(std::max(1.0, (roi.bottom - roi.top) * kRefHeight));
    canonical_valid_ = true;
    RebuildScaledTemplates();
}

void HudDetector::RebuildScaledTemplates() {
    scaled_templates_.clear();
    if (!canonical_valid_) return;
    cv::Size canon(canonical_w_, canonical_h_);
    for (const auto& t : templates_) {
        if (t.empty()) continue;
        cv::Mat s;
        cv::resize(t, s, canon, 0, 0, cv::INTER_LINEAR);
        scaled_templates_.push_back(s);
    }
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
        // canvas, and compare against the templates (already resampled to the
        // same canvas in SetRoi/SetTemplates). Because the ROI is defined in
        // screen percentages and CODM is fixed 16:9, the bar lands at the same
        // fractional position on every device -> the normalized images align.
        const auto& candidates = canonical_valid_ ? scaled_templates_ : templates_;
        if (canonical_valid_) {
            cv::resize(gray, gray, cv::Size(canonical_w_, canonical_h_), 0, 0, cv::INTER_LINEAR);
        }

        double best = 0.0;
        for (const auto& t : candidates) {
            if (t.cols > gray.cols || t.rows > gray.rows) continue;
            cv::Mat res;
            cv::matchTemplate(gray, t, res, cv::TM_CCOEFF_NORMED);
            double minVal = 0.0, maxVal = 0.0;
            cv::Point minLoc, maxLoc;
            cv::minMaxLoc(res, &minVal, &maxVal, &minLoc, &maxLoc);
            if (maxVal > best) best = maxVal;
        }

        result.confidence = best;
        result.presence = (best >= threshold_) ? HudResult::Presence::Present : HudResult::Presence::Absent;
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

} // namespace csn
