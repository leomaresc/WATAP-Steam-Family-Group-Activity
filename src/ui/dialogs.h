// Dialogos modales nativos (plantillas del archivo de recursos) con tema oscuro.
// Solo gestionan la entrada del usuario; la logica de datos vive en las capas inferiores.
#pragma once

#include <windows.h>

#include <string>

#include "common/models.h"
#include "ui/worker.h"

namespace srp::ui {

struct ProfileDialogResult {
    bool accepted = false;
    std::string input;  // SteamID64 o nombre/URL de perfil introducido
    std::string alias;  // nombre local opcional
};

// Alta o edicion de un integrante. `input` prellenado puede estar vacio.
ProfileDialogResult ShowProfileDialog(HWND parent, const std::string& input,
                                      const std::string& alias);

// Edita las preferencias in situ. Devuelve true si el usuario guardo los cambios.
// `clearCacheRequested` se marca cuando el usuario pidio borrar la cache local.
bool ShowSettingsDialog(HWND parent, Preferences& prefs, bool& clearCacheRequested);

// Gestiona la clave de API (guardar, probar y eliminar) usando el hilo de trabajo.
// `keyChanged` se marca si la clave almacenada cambio.
void ShowApiKeyDialog(HWND parent, Worker& worker, bool& keyChanged);

void ShowAboutDialog(HWND parent);

}  // namespace srp::ui
