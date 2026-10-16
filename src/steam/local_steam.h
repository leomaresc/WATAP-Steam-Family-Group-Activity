// Deteccion local del perfil de Steam del usuario (sin red, sin scraping).
//
// La clave de API de Steam NO revela a que cuenta pertenece, y no existe un
// endpoint publico documentado que lo haga. Por eso, cuando Steam esta
// instalado, localizamos el directorio por el registro de Windows y leemos la
// cuenta marcada como reciente en loginusers.vdf.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

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

}  // namespace srp::steam
