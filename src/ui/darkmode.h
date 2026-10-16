// Activa el modo oscuro de la interfaz nativa (titulos y controles comunes).
// Usa funciones documentadas cuando existen y, con reserva, rutinas de uxtheme
// para que los controles nativos acompanen el tema oscuro de la aplicacion.
#pragma once

#include <windows.h>

namespace srp::ui {

// Debe llamarse una sola vez, antes de crear ventanas.
void EnableDarkModeForApp();

// Aplica el titulo oscuro y el tema a una ventana o control concreto.
void ApplyDarkModeToWindow(HWND hwnd);

}  // namespace srp::ui
