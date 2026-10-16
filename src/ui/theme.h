// Tema oscuro: paleta de colores y tipografias escaladas segun el DPI de la ventana.
#pragma once

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>

#include <memory>

namespace srp::ui {

struct Theme {
    float dpi = 96.0f;
    float scale = 1.0f;

    Gdiplus::Color bg{255, 20, 21, 26};        // fondo general
    Gdiplus::Color panel{255, 27, 29, 36};     // paneles laterales
    Gdiplus::Color panelAlt{255, 33, 35, 44};  // cabeceras y barras
    Gdiplus::Color card{255, 35, 37, 46};      // tarjetas
    Gdiplus::Color cardHover{255, 43, 46, 57};
    Gdiplus::Color cardSelected{255, 46, 49, 64};
    Gdiplus::Color border{255, 49, 52, 63};
    Gdiplus::Color borderLight{255, 60, 64, 78};

    Gdiplus::Color text{255, 231, 233, 238};
    Gdiplus::Color textDim{255, 155, 161, 176};
    Gdiplus::Color textFaint{255, 110, 116, 132};

    Gdiplus::Color accent{255, 102, 192, 244};   // azul Steam
    Gdiplus::Color accentDim{255, 42, 110, 150};
    Gdiplus::Color accentSoft{255, 30, 62, 82};

    Gdiplus::Color levelHigh{255, 224, 108, 117};
    Gdiplus::Color levelModerate{255, 224, 179, 65};
    Gdiplus::Color levelLow{255, 123, 201, 111};
    Gdiplus::Color levelNone{255, 110, 116, 132};

    Gdiplus::Color good{255, 123, 201, 111};
    Gdiplus::Color warn{255, 224, 179, 65};
    Gdiplus::Color bad{255, 224, 108, 117};

    std::unique_ptr<Gdiplus::Font> fontSmall;
    std::unique_ptr<Gdiplus::Font> fontBody;
    std::unique_ptr<Gdiplus::Font> fontBodyBold;
    std::unique_ptr<Gdiplus::Font> fontTitle;
    std::unique_ptr<Gdiplus::Font> fontBig;
    std::unique_ptr<Gdiplus::Font> fontHuge;

    // Inicializa tipografias para el DPI indicado (se recrean si el DPI cambia).
    void Init(float dpiValue);

    int Px(float design) const { return static_cast<int>(design * scale + 0.5f); }
    float Pxf(float design) const { return design * scale; }

    Gdiplus::Color LevelColor(int level) const;
};

}  // namespace srp::ui
