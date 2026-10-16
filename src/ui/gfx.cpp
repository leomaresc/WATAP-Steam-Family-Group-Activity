#include "ui/gfx.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "common/paths.h"

namespace srp::ui {
namespace {

void RoundPath(Gdiplus::GraphicsPath& path, const Gdiplus::RectF& rect, float radius) {
    const float r = std::min(radius, std::min(rect.Width, rect.Height) / 2.0f);
    if (r <= 0.5f) {
        path.AddRectangle(rect);
        return;
    }
    const float d = r * 2.0f;
    path.AddArc(rect.X, rect.Y, d, d, 180.0f, 90.0f);
    path.AddArc(rect.GetRight() - d, rect.Y, d, d, 270.0f, 90.0f);
    path.AddArc(rect.GetRight() - d, rect.GetBottom() - d, d, d, 0.0f, 90.0f);
    path.AddArc(rect.X, rect.GetBottom() - d, d, d, 90.0f, 90.0f);
    path.CloseFigure();
}

}  // namespace

std::wstring Widen(const std::string& utf8) { return Utf8ToWide(utf8); }

Gdiplus::RectF RectFOf(const RECT& r) {
    return Gdiplus::RectF(static_cast<float>(r.left), static_cast<float>(r.top),
                          static_cast<float>(r.right - r.left), static_cast<float>(r.bottom - r.top));
}

void FillRect(Gdiplus::Graphics& g, const Gdiplus::RectF& rect, const Gdiplus::Color& color) {
    Gdiplus::SolidBrush brush(color);
    g.FillRectangle(&brush, rect);
}

void FillRoundRect(Gdiplus::Graphics& g, const Gdiplus::RectF& rect, float radius,
                   const Gdiplus::Color& color) {
    Gdiplus::SolidBrush brush(color);
    Gdiplus::GraphicsPath path;
    RoundPath(path, rect, radius);
    g.FillPath(&brush, &path);
}

void StrokeRoundRect(Gdiplus::Graphics& g, const Gdiplus::RectF& rect, float radius,
                     const Gdiplus::Color& color, float width) {
    Gdiplus::Pen pen(color, width);
    Gdiplus::GraphicsPath path;
    RoundPath(path, rect, radius);
    g.DrawPath(&pen, &path);
}

void DrawTextIn(Gdiplus::Graphics& g, const std::wstring& text, Gdiplus::Font& font,
                const Gdiplus::RectF& rect, const Gdiplus::Color& color,
                Gdiplus::StringAlignment align, bool verticalCenter, bool ellipsis) {
    if (text.empty()) return;
    Gdiplus::SolidBrush brush(color);
    Gdiplus::StringFormat format(Gdiplus::StringFormat::GenericTypographic());
    format.SetAlignment(align);
    format.SetLineAlignment(verticalCenter ? Gdiplus::StringAlignmentCenter
                                           : Gdiplus::StringAlignmentNear);
    format.SetFormatFlags(format.GetFormatFlags() | Gdiplus::StringFormatFlagsNoWrap);
    format.SetTrimming(ellipsis ? Gdiplus::StringTrimmingEllipsisCharacter
                                : Gdiplus::StringTrimmingNone);
    g.DrawString(text.c_str(), static_cast<INT>(text.size()), &font, rect, &format, &brush);
}

float MeasureTextWidth(Gdiplus::Graphics& g, const std::wstring& text, Gdiplus::Font& font) {
    if (text.empty()) return 0.0f;
    Gdiplus::RectF bounds;
    Gdiplus::StringFormat format(Gdiplus::StringFormat::GenericTypographic());
    format.SetFormatFlags(format.GetFormatFlags() | Gdiplus::StringFormatFlagsNoWrap);
    g.MeasureString(text.c_str(), static_cast<INT>(text.size()), &font,
                    Gdiplus::PointF(0.0f, 0.0f), &format, &bounds);
    return bounds.Width;
}

void DrawImageCover(Gdiplus::Graphics& g, Gdiplus::Bitmap* bitmap, const Gdiplus::RectF& dest,
                    float radius) {
    if (bitmap == nullptr || bitmap->GetLastStatus() != Gdiplus::Ok) return;
    const float iw = static_cast<float>(bitmap->GetWidth());
    const float ih = static_cast<float>(bitmap->GetHeight());
    if (iw <= 0.0f || ih <= 0.0f) return;

    const float scale = std::max(dest.Width / iw, dest.Height / ih);
    const float dw = iw * scale;
    const float dh = ih * scale;
    const float dx = dest.X + (dest.Width - dw) / 2.0f;
    const float dy = dest.Y + (dest.Height - dh) / 2.0f;

    const Gdiplus::RectF target(dx, dy, dw, dh);
    if (radius <= 0.5f) {
        g.DrawImage(bitmap, target);
        return;
    }
    // Recorta la imagen a un rectangulo redondeado.
    Gdiplus::GraphicsPath clip;
    RoundPath(clip, dest, radius);
    Gdiplus::GraphicsState state = g.Save();
    g.SetClip(&clip);
    Gdiplus::InterpolationMode oldMode = g.GetInterpolationMode();
    g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    g.DrawImage(bitmap, target);
    g.SetInterpolationMode(oldMode);
    g.Restore(state);
}

void DrawImagePlaceholder(Gdiplus::Graphics& g, const Gdiplus::RectF& dest, const std::string& utf8Text,
                          Gdiplus::Font& font, const Gdiplus::Color& bgColor,
                          const Gdiplus::Color& textColor, float radius) {
    FillRoundRect(g, dest, radius, bgColor);
    if (!utf8Text.empty()) {
        const std::wstring text = Widen(utf8Text);
        DrawTextIn(g, text, font, dest, textColor, Gdiplus::StringAlignmentCenter, true, false);
    }
}

}  // namespace srp::ui
