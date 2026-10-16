#include "ui/darkmode.h"

#include <dwmapi.h>
#include <uxtheme.h>

namespace srp::ui {
namespace {

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

// Ordinales de uxtheme utilizados por el modo oscuro de los controles comunes.
constexpr int kOrdinalAllowDarkModeForWindow = 133;
constexpr int kOrdinalSetPreferredAppMode = 135;
constexpr int kOrdinalFlushMenuThemes = 136;

enum class PreferredAppMode {
    Default = 0,
    AllowDark = 1,
    ForceDark = 2,
    ForceLight = 3,
    Max = 4
};

using SetPreferredAppModeFn = PreferredAppMode(WINAPI*)(PreferredAppMode);
using AllowDarkModeForWindowFn = BOOL(WINAPI*)(HWND, BOOL);
using FlushMenuThemesFn = void(WINAPI*)();

HMODULE UxThemeModule() {
    static HMODULE module = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    return module;
}

template <typename Fn>
Fn Resolve(HMODULE module, int ordinal) {
    if (module == nullptr) return nullptr;
    return reinterpret_cast<Fn>(GetProcAddress(module, reinterpret_cast<LPCSTR>(static_cast<ULONG_PTR>(ordinal))));
}

}  // namespace

void EnableDarkModeForApp() {
    HMODULE ux = UxThemeModule();
    if (auto setMode = Resolve<SetPreferredAppModeFn>(ux, kOrdinalSetPreferredAppMode)) {
        setMode(PreferredAppMode::AllowDark);
    }
    if (auto flush = Resolve<FlushMenuThemesFn>(ux, kOrdinalFlushMenuThemes)) {
        flush();
    }
}

void ApplyDarkModeToWindow(HWND hwnd) {
    if (hwnd == nullptr) return;

    BOOL dark = TRUE;
    // 20 en versiones recientes de Windows; 19 como reserva para compilaciones anteriores.
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    DwmSetWindowAttribute(hwnd, 19, &dark, sizeof(dark));

    HMODULE ux = UxThemeModule();
    if (auto allow = Resolve<AllowDarkModeForWindowFn>(ux, kOrdinalAllowDarkModeForWindow)) {
        allow(hwnd, TRUE);
    }
    SetWindowTheme(hwnd, L"DarkMode_Explorer", nullptr);
    SendMessageW(hwnd, WM_THEMECHANGED, 0, 0);
}

}  // namespace srp::ui
