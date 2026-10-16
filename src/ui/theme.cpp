#include "ui/theme.h"

#include "common/models.h"

namespace srp::ui {
namespace {

std::unique_ptr<Gdiplus::Font> MakeFont(float pixelSize, int style) {
    static const Gdiplus::FontFamily family(L"Segoe UI");
    Gdiplus::FontFamily fallback(L"Arial");
    const Gdiplus::FontFamily* chosen = family.IsAvailable() ? &family : &fallback;
    auto font = std::make_unique<Gdiplus::Font>(chosen, pixelSize, style, Gdiplus::UnitPixel);
    if (font->GetLastStatus() != Gdiplus::Ok) return nullptr;
    return font;
}

}  // namespace

void Theme::Init(float dpiValue) {
    dpi = dpiValue > 0.0f ? dpiValue : 96.0f;
    scale = dpi / 96.0f;

    fontSmall = MakeFont(Pxf(12.0f), Gdiplus::FontStyleRegular);
    fontBody = MakeFont(Pxf(14.0f), Gdiplus::FontStyleRegular);
    fontBodyBold = MakeFont(Pxf(14.0f), Gdiplus::FontStyleBold);
    fontTitle = MakeFont(Pxf(15.0f), Gdiplus::FontStyleBold);
    fontBig = MakeFont(Pxf(21.0f), Gdiplus::FontStyleBold);
    fontHuge = MakeFont(Pxf(26.0f), Gdiplus::FontStyleBold);
}

Gdiplus::Color Theme::LevelColor(int level) const {
    switch (static_cast<srp::ActivityLevel>(level)) {
        case srp::ActivityLevel::High: return levelHigh;
        case srp::ActivityLevel::Moderate: return levelModerate;
        case srp::ActivityLevel::Low: return levelLow;
        case srp::ActivityLevel::None:
        default: return levelNone;
    }
}

}  // namespace srp::ui
