// Almacenamiento de la clave de Steam Web API en el Administrador de credenciales
// de Windows (CredWriteW/CredReadW/CredDeleteW). Nunca se escribe en disco ni en registros.
#pragma once

#include <string>

namespace srp::creds {

// Guarda (o reemplaza) la clave. Devuelve false y describe el error si falla.
bool SaveApiKey(const std::string& apiKey, std::string& errorOut);

// Recupera la clave. Devuelve false si no hay ninguna guardada.
bool LoadApiKey(std::string& apiKeyOut);

// Elimina la clave. Devuelve false si no existia o hubo un error.
bool DeleteApiKey(std::string& errorOut);

// Indica si hay una clave almacenada.
bool HasApiKey();

// Migra la clave guardada con el nombre antiguo (SteamRecentPlay/...) al nombre
// actual (WATAP/...). Se llama una vez al arrancar; no hace nada si ya existe.
void MigrateLegacyCredentials();

}  // namespace srp::creds
