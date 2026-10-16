// Punto de entrada de Steam Recent Play.
//
// Inicializa GDI+ y los controles comunes, activa el modo oscuro de la interfaz
// nativa y ejecuta el bucle de mensajes de la ventana principal.
#include <windows.h>

#include <objidl.h>
#include <commctrl.h>
#include <gdiplus.h>

#include "common/paths.h"
#include "ui/app.h"
#include "ui/darkmode.h"

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE /*previous*/, PWSTR /*commandLine*/,
                      int showCommand) {
    Gdiplus::GdiplusStartupInput gdiplusInput;
    ULONG_PTR gdiplusToken = 0;
    if (Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusInput, nullptr) != Gdiplus::Ok) {
        MessageBoxW(nullptr, L"No se pudo iniciar GDI+.", L"Steam Recent Play", MB_ICONERROR);
        return 1;
    }

    INITCOMMONCONTROLSEX commonControls{};
    commonControls.dwSize = sizeof(commonControls);
    commonControls.dwICC = ICC_STANDARD_CLASSES | ICC_BAR_CLASSES;
    InitCommonControlsEx(&commonControls);

    // El manifiesto ya declara la consciencia de DPI; esta llamada refuerza el modo
    // por monitor cuando el sistema lo permite (resultado ignorado si ya esta fijado).
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    srp::ui::EnableDarkModeForApp();
    srp::EnsureDataDirs();

    int exitCode = 0;
    if (!srp::ui::MainWindow::RegisterWindowClass(instance)) {
        exitCode = 1;
    } else {
        srp::ui::MainWindow window;
        if (!window.Create(instance, showCommand)) {
            exitCode = 1;
        } else {
            MSG message;
            while (GetMessageW(&message, nullptr, 0, 0) > 0) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            exitCode = static_cast<int>(message.wParam);
        }
    }

    Gdiplus::GdiplusShutdown(gdiplusToken);
    return exitCode;
}
