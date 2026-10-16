// Utilidades de dibujo sobre GDI+ (rectangulos redondeados, texto e imagenes).
#pragma once

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>

#include <string>

namespace srp::ui {

std::wstring Widen(const std::string& utf8);

Gdiplus::RectF RectFOf(const RECT& r);

void FillRect(Gdiplus::Graphics& g, const Gdiplus::RectF& rect, const Gdiplus::Color& color);

void FillRoundRect(Gdiplus::Graphics& g, const Gdiplus::RectF& rect, float radius,
                   const Gdiplus::Color& color);

void StrokeRoundRect(Gdiplus::Graphics& g, const Gdiplus::RectF& rect, float radius,
                     const Gdiplus::Color& color, float width);

// Dibuja texto en un rectangulo. `align` usa Gdiplus::StringAlignment.
void DrawTextIn(Gdiplus::Graphics& g, const std::wstring& text, Gdiplus::Font& font,
                const Gdiplus::RectF& rect, const Gdiplus::Color& color,
                Gdiplus::StringAlignment align, bool verticalCenter = true,
                bool ellipsis = true);

// Ancho de un texto en pixeles.
float MeasureTextWidth(Gdiplus::Graphics& g, const std::wstring& text, Gdiplus::Font& font);

// Dibuja una imagen cubriendo el rectangulo destino y recortando el exceso.
void DrawImageCover(Gdiplus::Graphics& g, Gdiplus::Bitmap* bitmap, const Gdiplus::RectF& dest,
                    float radius = 0.0f);

// Dibuja el marcador de "sin imagen" (cuadrado con iniciales) para portadas ausentes.
void DrawImagePlaceholder(Gdiplus::Graphics& g, const Gdiplus::RectF& dest, const std::string& utf8Text,
                          Gdiplus::Font& font, const Gdiplus::Color& bgColor,
                          const Gdiplus::Color& textColor, float radius = 0.0f);

}  // namespace srp::ui
