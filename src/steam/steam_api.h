// Integracion con las interfaces de Steam Web API.
//
// Endpoints utilizados:
//   * IPlayerService/GetRecentlyPlayedGames/v1  -> actividad reciente por perfil
//   * ISteamUser/GetPlayerSummaries/v2          -> nombre publico y avatar
//   * ISteamUser/ResolveVanityURL/v1            -> resolucion de URL/nombre de perfil
//   * ISteamUser/GetFriendList/v1               -> lista de amigos (importar integrantes)
//
// Nota: el grupo familiar (Steam Families) NO se obtiene de la Web API porque
// IFamilyGroupsService exige un token de sesion, no una clave de API. Sus
// miembros se leen del cliente local (ver steam/local_steam.h) y aqui solo se
// piden sus nombres/avatares con GetPlayerSummaries.
//
// No se usa scraping ni cookies. La API entrega minutos agregados de las ultimas
// dos semanas, nunca un historial de sesiones con fechas.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "common/models.h"
#include "steam/http.h"

namespace srp::steam {

// --- Construccion de URLs (pura, comprobable) ---
std::string RecentlyPlayedUrl(const std::string& apiKey, const std::string& steamid64, int count = 0);
std::string PlayerSummariesUrl(const std::string& apiKey, const std::string& steamid64);
// Variante por lotes (hasta 100 identificadores por llamada).
std::string PlayerSummariesUrl(const std::string& apiKey, const std::vector<std::string>& steamids);
std::string FriendListUrl(const std::string& apiKey, const std::string& steamid64);
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
// Variante por lotes: analiza todos los jugadores devueltos por GetPlayerSummaries.
FetchStatus ParsePlayerSummariesListJson(const std::string& body, std::vector<Profile>& out,
                                         std::string& message);
// Analiza la lista de amigos devuelta por GetFriendList.
FetchStatus ParseFriendListJson(const std::string& body, std::vector<std::string>& out,
                                std::string& message);
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

// Descarga nombre publico y avatar de una lista de SteamID64 (por lotes de 100).
FetchOutcome FetchProfilesByIds(HttpClient& http, const std::string& apiKey,
                                const std::vector<std::string>& steamids, std::vector<Profile>& out);

// Descarga la lista de amigos de `steamid64` con su nombre publico y avatar.
// Requiere que la lista de amigos sea publica; si es privada devuelve
// FetchStatus::PrivateOrUnavailable.
FetchOutcome FetchFriendProfiles(HttpClient& http, const std::string& apiKey,
                                 const std::string& steamid64, std::vector<Profile>& out);

// Comprueba si la clave de API es aceptada por Steam.
FetchOutcome TestApiKey(HttpClient& http, const std::string& apiKey);

}  // namespace srp::steam
