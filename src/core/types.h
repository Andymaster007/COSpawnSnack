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

// OCR detection result. Used for both round/match-end keywords (胜利/战败)
// and the spectator respawn hint ("你将在下一回合重生").
struct ResultText {
    bool found = false;
    std::string matched_keyword;
    double confidence = 0.0;
    std::string raw_text;  // normalized OCR text (for diagnostics / debugging)
    // When true, this frame's detection is noise (e.g. an in-round banner such
    // as "炸弹已被安装" that occupies the respawn-hint area) and the state
    // machine must NOT let it drive any state change -- the current state is
    // preserved. Only used for the respawn hint, never for the result text.
    bool ignored = false;
};

using RespawnText = ResultText;

struct FrameInfo {
    int width = 0;
    int height = 0;
    int dpi = 96;
};

} // namespace csn
