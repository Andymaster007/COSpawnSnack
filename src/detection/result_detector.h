#pragma once
#include "core/types.h"
#include <string>
#include <vector>

namespace cv { class Mat; }

namespace csn {

class ResultDetector {
public:
    void SetRoi(const RationalRect& roi);
    void SetKeywords(const std::vector<std::string>& keywords);
    void SetConfidenceThreshold(double threshold);

    ResultText Detect(const cv::Mat& frame);

private:
    RationalRect roi_;
    std::vector<std::string> keywords_;
    double threshold_ = 0.6;
};

} // namespace csn
