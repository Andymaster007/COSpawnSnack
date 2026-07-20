#pragma once
#include <string>
#include <vector>

namespace csn {

// Screen-relative rectangle (0..1).
struct RationalRect {
    double left = 0.0;
    double top = 0.0;
    double right = 1.0;
    double bottom = 1.0;
};

struct PixelRect {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
};

struct HudResult {
    enum class Presence { Unknown, Absent, Present };
    Presence presence = Presence::Unknown;
    double confidence = 0.0;
};

struct ResultText {
    bool found = false;
    std::string matched_keyword;
    double confidence = 0.0;
    std::string raw_text;  // normalized OCR text (for diagnostics / debugging)
};

struct FrameInfo {
    int width = 0;
    int height = 0;
    int dpi = 96;
};

} // namespace csn
