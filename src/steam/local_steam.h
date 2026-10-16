// Deteccion local del perfil de Steam del usuario (sin red, sin scraping).
//
// La clave de API de Steam NO revela a que cuenta pertenece, y no existe un
// endpoint publico documentado que lo haga. Por eso, cuando Steam esta
// instalado, localizamos el directorio por el registro de Windows y leemos la
// cuenta marcada como reciente en loginusers.vdf.
//
// Del mismo modo, el grupo familiar (Steam Families) se lee del propio cliente:
// el servicio IFamilyGroupsService de la Web API no acepta una clave de API
// normal (necesita un token de sesion), pero Steam guarda una copia en
// userdata/<cuenta>/config/localconfig.vdf. Leemos ese archivo local, que es la
// misma fuente que usa el propio cliente.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace srp::steam {

// Cuenta de Steam detectada en el cliente local.
struct LocalAccount {
    std::string steamid64;
    std::string accountName;
    std::string personaName;
    int64_t timestamp = 0;
    bool mostRecent = false;
};

// Directorio de instalacion de Steam segun el registro de Windows. Vacio si no se encuentra.
std::filesystem::path SteamInstallDir();

// Ruta esperada de loginusers.vdf dentro del directorio de Steam.
std::filesystem::path SteamLoginUsersPath(const std::filesystem::path& steamDir);

// Analiza el contenido de loginusers.vdf y elige la cuenta reciente.
// Es puro (no toca el disco ni el registro) y por tanto comprobable en las pruebas.
bool ParseLoginUsersVdf(const std::string& content, LocalAccount& out);

// Deteccion completa (registro + archivo). Devuelve false con `errorOut` descriptivo si falla.
bool DetectRecentSteamAccount(LocalAccount& out, std::string& errorOut);

// Grupo familiar de Steam detectado en el cliente local.
struct LocalFamilyGroup {
    std::string groupId;
    std::string name;
    std::vector<std::string> memberSteamIds;  // SteamID64; puede incluir la propia cuenta
};

// Ruta esperada de localconfig.vdf de una cuenta dentro del directorio de Steam.
std::filesystem::path SteamLocalConfigPath(const std::filesystem::path& steamDir,
                                           const std::string& accountId);

// Analiza el contenido de localconfig.vdf y extrae el bloque "FamilyGroup".
// Es puro (no toca el disco) y por tanto comprobable en las pruebas.
bool ParseLocalFamilyGroupVdf(const std::string& content, LocalFamilyGroup& out);

// Deteccion completa del grupo familiar (registro + disco). Prefiere la carpeta
// de la cuenta indicada en `ownSteamId64`. Devuelve false con `errorOut` si no
// se encuentra ningun grupo familiar.
bool DetectLocalFamilyGroup(const std::string& ownSteamId64, LocalFamilyGroup& out,
                            std::string& errorOut);

}  // namespace srp::steam
