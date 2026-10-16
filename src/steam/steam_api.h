// Integracion con las interfaces publicas documentadas de Steam Web API.
//
// Endpoints utilizados (solo interfaces oficiales publicas):
//   * IPlayerService/GetRecentlyPlayedGames/v1  -> actividad reciente por perfil
//   * ISteamUser/GetPlayerSummaries/v2          -> nombre publico y avatar
//   * ISteamUser/ResolveVanityURL/v1            -> resolucion de URL/nombre de perfil
//
// No se usa scraping, cookies ni endpoints internos. La API entrega minutos
// agregados de las ultimas dos semanas, nunca un historial de sesiones con fechas.
#pragma once

#include <cstdint>
#include <string>

#include "common/models.h"
#include "steam/http.h"

namespace srp::steam {

// --- Construccion de URLs (pura, comprobable) ---
std::string RecentlyPlayedUrl(const std::string& apiKey, const std::string& steamid64, int count = 0);
std::string PlayerSummariesUrl(const std::string& apiKey, const std::string& steamid64);
std::string ResolveVanityUrl(const std::string& apiKey, const std::string& vanity);

// Portada del juego (cabecera 460x215) alojada en el CDN de Steam. No requiere clave.
std::string CoverUrl(uint32_t appid);
// Version pequena (capsule 231x87).
std::string CoverUrlSmall(uint32_t appid);
// Pagina publica del juego en la tienda de Steam.
std::string StorePageUrl(uint32_t appid);

// --- Analisis de respuestas (puro, comprobable con datos ficticios) ---
// `message` recibe una descripcion del estado cuando algo no es correcto.
FetchStatus ParseRecentlyPlayedJson(const std::string& body, ProfileActivity& out,
                                    std::string& message);
FetchStatus ParsePlayerSummariesJson(const std::string& body, Profile& out, std::string& message);
FetchStatus ParseResolveVanityJson(const std::string& body, std::string& steamidOut,
                                   std::string& message);

// --- Operaciones de red ---
struct FetchOutcome {
    FetchStatus status = FetchStatus::NotFetched;
    std::string message;
};

// Descarga nombre/avatar y actividad reciente de un perfil.
// `out` conserva los datos previos si la consulta falla.
FetchOutcome FetchProfileActivity(HttpClient& http, const std::string& apiKey,
                                  const Profile& profile, ProfileActivity& out);

// Resuelve un nombre vanity a SteamID64.
FetchOutcome ResolveVanity(HttpClient& http, const std::string& apiKey,
                           const std::string& vanity, std::string& steamidOut);

// Comprueba si la clave de API es aceptada por Steam.
FetchOutcome TestApiKey(HttpClient& http, const std::string& apiKey);

}  // namespace srp::steam
