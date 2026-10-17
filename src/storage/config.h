// Persistencia de la configuracion en %LOCALAPPDATA%\WATAP\config.json
// La clave de API NUNCA se guarda aqui: vive en el Administrador de credenciales.
#pragma once

#include <string>

#include "common/models.h"

namespace srp {

// Configuracion por defecto (sin perfiles).
AppConfig DefaultConfig();

// Normaliza umbrales y descarta perfiles no validos. Devuelve los perfiles descartados.
int SanitizeConfig(AppConfig& config);

// Serializacion (pura, comprobable sin tocar el disco).
std::string ConfigToJson(const AppConfig& config);
bool ConfigFromJson(const std::string& json, AppConfig& out, std::string& errorOut);

// Acceso a disco. Si el archivo no existe, se devuelve la configuracion por defecto.
bool LoadConfig(AppConfig& out, std::string& errorOut);
bool SaveConfig(const AppConfig& config, std::string& errorOut);

}  // namespace srp
