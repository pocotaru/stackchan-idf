#include "balloon.hpp"

namespace stackchan::avatar::internal {

namespace {

// Geometry of the bottom-of-screen balloon (canvas is 320x240).
constexpr std::int16_t kMargin = 4;
constexpr std::int16_t kPanelH = 40;
constexpr std::int16_t kPanelX = kMargin;
constexpr std::int16_t kPanelY = 240 - kPanelH - kMargin;
constexpr std::int16_t kPanelW = 320 - kMargin * 2;
constexpr std::int16_t kPanelRadius = 10;
constexpr std::uint8_t kTextSize = 3; // 18 x 24 per glyph

} // namespace

void draw_balloon(M5Canvas& canvas, const DrawContext& ctx)
{
    if (!ctx.balloon_text.has_value()) {
        return;
    }
    const auto& text = *ctx.balloon_text;
    if (text.empty()) {
        return;
    }

    const std::uint16_t fg = ctx.palette.balloon_foreground;
    const std::uint16_t bg = ctx.palette.balloon_background;

    canvas.fillRoundRect(kPanelX, kPanelY, kPanelW, kPanelH, kPanelRadius, bg);
    canvas.drawRoundRect(kPanelX, kPanelY, kPanelW, kPanelH, kPanelRadius, fg);

    canvas.setTextColor(fg, bg);
    canvas.setTextSize(kTextSize);
    canvas.setTextDatum(lgfx::textdatum_t::middle_center);
    canvas.drawString(text.c_str(),
                      kPanelX + kPanelW / 2,
                      kPanelY + kPanelH / 2);
}

} // namespace stackchan::avatar::internal
