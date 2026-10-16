// Modelos de datos compartidos entre la interfaz y las capas de red/almacenamiento.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace srp {

// Nivel de actividad reciente derivado de los minutos de las ultimas dos semanas.
enum class ActivityLevel {
    None = 0,      // sin actividad reciente registrada
    Low,           // actividad reciente baja
    Moderate,      // actividad reciente moderada
    High           // actividad reciente alta
};

// Estado de una consulta a la API de Steam.
enum class FetchStatus {
    NotFetched = 0,        // nunca consultado
    Ok,                    // consulta correcta con juegos recientes
    NoRecentGames,         // consulta correcta sin juegos recientes (o perfil privado)
    PrivateOrUnavailable,  // datos no disponibles / restringidos por privacidad
    NetworkError,          // no se pudo contactar con el servicio
    InvalidApiKey,         // clave de API ausente o rechazada
    InvalidProfile,        // perfil no valido (SteamID inexistente)
    RateLimited            // demasiadas solicitudes
};

// Perfil configurado localmente por el usuario.
struct Profile {
    std::string steamid64;    // identificador de 17 digitos
    std::string personaName;  // nombre publico mostrado por Steam
    std::string avatarUrl;    // URL del avatar (puede estar vacia)
    std::string alias;        // nombre local opcional definido por el usuario
};

// Juego con actividad reciente.
struct Game {
    uint32_t appid = 0;
    std::string name;
    int playtime2Weeks = 0;    // minutos en las ultimas dos semanas
    int playtimeForever = 0;   // minutos acumulados
    bool hasPlaytimeForever = false;
};

// Resultado de una consulta de actividad para un perfil.
struct ProfileActivity {
    std::string steamid64;
    std::string personaName;
    std::string avatarUrl;
    std::vector<Game> games;                       // ordenados por minutos recientes desc.
    FetchStatus status = FetchStatus::NotFetched;
    std::string statusMessage;                     // detalle legible del estado
    int64_t fetchedAtUnix = 0;                     // fecha de la ultima consulta correcta
    bool fromCache = false;                        // los datos provienen de la cache local
};

// Preferencias de presentacion y de umbrales.
struct Preferences {
    bool autoRefreshEnabled = false;
    int autoRefreshMinutes = 30;

    bool hideLowActivity = false;
    int lowActivityFilterMinutes = 30;   // ocultar juegos por debajo de este umbral

    int moderateActivityMinutes = 120;   // umbral de actividad moderada
    int highActivityMinutes = 480;       // umbral de actividad alta

    bool showCovers = true;
    std::string lastSelectedProfile;     // steamid64 seleccionado ("" = todos)
};

// Configuracion completa persistida en disco.
struct AppConfig {
    int version = 1;
    std::vector<Profile> profiles;
    Preferences prefs;
};

}  // namespace srp
