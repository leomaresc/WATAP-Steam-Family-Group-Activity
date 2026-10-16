#include "steam/steam_api.h"

#include <algorithm>
#include <nlohmann/json.hpp>

#include "common/format.h"

namespace srp::steam {
namespace {

using Json = nlohmann::json;

constexpr const char* kApiBase = "https://api.steampowered.com";
constexpr const char* kCdnBase = "https://cdn.cloudflare.steamstatic.com/steam/apps";

// Perfil publico usado unicamente para validar que la clave de API es aceptada.
// Un perfil privado devuelve 200 con respuesta vacia; una clave invalida devuelve 401/403.
constexpr const char* kKeyTestProfile = "76561197960435530";

std::string IntToString(int64_t v) { return std::to_string(v); }

std::string UrlEncode(const std::string& value) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(value.size() * 3);
    for (unsigned char c : value) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 0x0F]);
        }
    }
    return out;
}

Json ParseSafe(const std::string& body) {
    return Json::parse(body, nullptr, /*allow_exceptions=*/false);
}

// Traduce el codigo HTTP de Steam a un estado de la aplicacion.
FetchStatus StatusFromHttp(const HttpResponse& response, std::string& message) {
    switch (response.statusCode) {
        case 401:
        case 403:
            message = "Steam ha rechazado la clave de API (HTTP " +
                      std::to_string(response.statusCode) + ").";
            return FetchStatus::InvalidApiKey;
        case 429:
            message = "Demasiadas solicitudes a Steam (HTTP 429).";
            return FetchStatus::RateLimited;
        default:
            break;
    }
    if (response.error != HttpError::None) {
        message = response.errorMessage.empty() ? "Error de conexion" : response.errorMessage;
        return FetchStatus::NetworkError;
    }
    message = "Steam respondio con HTTP " + std::to_string(response.statusCode) + ".";
    return FetchStatus::NetworkError;
}

}  // namespace

std::string RecentlyPlayedUrl(const std::string& apiKey, const std::string& steamid64, int count) {
    std::string url = std::string(kApiBase) + "/IPlayerService/GetRecentlyPlayedGames/v1/?key=" +
                      UrlEncode(apiKey) + "&steamid=" + UrlEncode(steamid64) + "&format=json";
    if (count > 0) url += "&count=" + std::to_string(count);
    return url;
}

std::string PlayerSummariesUrl(const std::string& apiKey, const std::string& steamid64) {
    return std::string(kApiBase) + "/ISteamUser/GetPlayerSummaries/v2/?key=" + UrlEncode(apiKey) +
           "&steamids=" + UrlEncode(steamid64) + "&format=json";
}

std::string ResolveVanityUrl(const std::string& apiKey, const std::string& vanity) {
    return std::string(kApiBase) + "/ISteamUser/ResolveVanityURL/v1/?key=" + UrlEncode(apiKey) +
           "&vanityurl=" + UrlEncode(vanity) + "&format=json";
}

std::string CoverUrl(uint32_t appid) {
    return std::string(kCdnBase) + "/" + std::to_string(appid) + "/header.jpg";
}

std::string CoverUrlSmall(uint32_t appid) {
    return std::string(kCdnBase) + "/" + std::to_string(appid) + "/capsule_231x87.jpg";
}

std::string StorePageUrl(uint32_t appid) {
    return "https://store.steampowered.com/app/" + std::to_string(appid);
}

FetchStatus ParseRecentlyPlayedJson(const std::string& body, ProfileActivity& out,
                                    std::string& message) {
    const Json root = ParseSafe(body);
    if (root.is_discarded() || !root.is_object()) {
        message = "Respuesta JSON no valida de Steam.";
        return FetchStatus::NetworkError;
    }
    if (!root.contains("response") || !root["response"].is_object()) {
        message = "La respuesta de Steam no contiene datos de actividad.";
        return FetchStatus::PrivateOrUnavailable;
    }

    const Json& response = root["response"];
    std::vector<Game> games;

    if (response.contains("games") && response["games"].is_array()) {
        for (const Json& item : response["games"]) {
            if (!item.is_object()) continue;
            Game game;
            if (item.contains("appid") && item["appid"].is_number()) {
                game.appid = static_cast<uint32_t>(item["appid"].get<int64_t>());
            }
            if (game.appid == 0) continue;
            if (item.contains("name") && item["name"].is_string()) {
                game.name = item["name"].get<std::string>();
            }
            if (game.name.empty()) game.name = "App " + std::to_string(game.appid);
            if (item.contains("playtime_2weeks") && item["playtime_2weeks"].is_number()) {
                game.playtime2Weeks = static_cast<int>(item["playtime_2weeks"].get<int64_t>());
            }
            if (item.contains("playtime_forever") && item["playtime_forever"].is_number()) {
                game.playtimeForever = static_cast<int>(item["playtime_forever"].get<int64_t>());
                game.hasPlaytimeForever = true;
            }
            games.push_back(std::move(game));
        }
    }

    // Orden: mas tiempo reciente primero; a igualdad, mas tiempo acumulado.
    std::stable_sort(games.begin(), games.end(), [](const Game& a, const Game& b) {
        if (a.playtime2Weeks != b.playtime2Weeks) return a.playtime2Weeks > b.playtime2Weeks;
        return a.playtimeForever > b.playtimeForever;
    });

    out.games = std::move(games);
    if (out.games.empty()) {
        // Respuesta correcta sin juegos: puede ser ausencia de actividad o perfil privado.
        message = DefaultStatusMessage(FetchStatus::NoRecentGames);
        return FetchStatus::NoRecentGames;
    }
    message = "Consulta correcta: " + std::to_string(out.games.size()) + " juegos recientes.";
    return FetchStatus::Ok;
}

FetchStatus ParsePlayerSummariesJson(const std::string& body, Profile& out, std::string& message) {
    const Json root = ParseSafe(body);
    if (root.is_discarded() || !root.is_object() || !root.contains("response") ||
        !root["response"].is_object()) {
        message = "Respuesta JSON no valida de Steam.";
        return FetchStatus::NetworkError;
    }
    const Json& response = root["response"];
    if (!response.contains("players") || !response["players"].is_array() ||
        response["players"].empty()) {
        message = "Steam no devolvio informacion publica para este perfil.";
        return FetchStatus::InvalidProfile;
    }
    const Json& player = response["players"][0];
    if (player.contains("steamid") && player["steamid"].is_string()) {
        out.steamid64 = player["steamid"].get<std::string>();
    }
    if (player.contains("personaname") && player["personaname"].is_string()) {
        out.personaName = player["personaname"].get<std::string>();
    }
    if (player.contains("avatarfull") && player["avatarfull"].is_string()) {
        out.avatarUrl = player["avatarfull"].get<std::string>();
    } else if (player.contains("avatarmedium") && player["avatarmedium"].is_string()) {
        out.avatarUrl = player["avatarmedium"].get<std::string>();
    }
    message.clear();
    return FetchStatus::Ok;
}

FetchStatus ParseResolveVanityJson(const std::string& body, std::string& steamidOut,
                                   std::string& message) {
    const Json root = ParseSafe(body);
    if (root.is_discarded() || !root.is_object() || !root.contains("response") ||
        !root["response"].is_object()) {
        message = "Respuesta JSON no valida de Steam.";
        return FetchStatus::NetworkError;
    }
    const Json& response = root["response"];
    const int success = response.contains("success") && response["success"].is_number()
                            ? response["success"].get<int>()
                            : 0;
    if (success != 1) {
        message = "Steam no encontro ningun perfil con ese nombre o URL.";
        return FetchStatus::InvalidProfile;
    }
    if (!response.contains("steamid") || !response["steamid"].is_string()) {
        message = "Steam no devolvio un identificador para ese perfil.";
        return FetchStatus::InvalidProfile;
    }
    steamidOut = response["steamid"].get<std::string>();
    if (!IsValidSteamId64(steamidOut)) {
        message = "El identificador devuelto por Steam no es valido.";
        return FetchStatus::InvalidProfile;
    }
    message.clear();
    return FetchStatus::Ok;
}

FetchOutcome FetchProfileActivity(HttpClient& http, const std::string& apiKey,
                                  const Profile& profile, ProfileActivity& out) {
    FetchOutcome outcome;

    out.steamid64 = profile.steamid64;

    if (apiKey.empty()) {
        outcome.status = FetchStatus::InvalidApiKey;
        outcome.message = DefaultStatusMessage(FetchStatus::InvalidApiKey);
        return outcome;
    }

    // 1) Nombre publico y avatar.
    const HttpResponse summary = http.Get(PlayerSummariesUrl(apiKey, profile.steamid64));
    if (!summary.Ok()) {
        outcome.status = StatusFromHttp(summary, outcome.message);
        return outcome;
    }
    Profile refreshed = profile;
    std::string summaryMessage;
    const FetchStatus summaryStatus = ParsePlayerSummariesJson(summary.body, refreshed, summaryMessage);
    if (summaryStatus == FetchStatus::InvalidProfile) {
        outcome.status = FetchStatus::InvalidProfile;
        outcome.message = summaryMessage;
        return outcome;
    }
    if (summaryStatus == FetchStatus::Ok) {
        out.personaName = refreshed.personaName;
        out.avatarUrl = refreshed.avatarUrl;
    } else {
        out.personaName = profile.personaName;
        out.avatarUrl = profile.avatarUrl;
    }

    // 2) Actividad reciente.
    const HttpResponse recent = http.Get(RecentlyPlayedUrl(apiKey, profile.steamid64));
    if (!recent.Ok()) {
        outcome.status = StatusFromHttp(recent, outcome.message);
        return outcome;
    }
    const FetchStatus parsed = ParseRecentlyPlayedJson(recent.body, out, outcome.message);
    outcome.status = parsed;
    if (parsed == FetchStatus::Ok || parsed == FetchStatus::NoRecentGames) {
        out.fetchedAtUnix = UnixNow();
        out.fromCache = false;
    }
    return outcome;
}

FetchOutcome ResolveVanity(HttpClient& http, const std::string& apiKey,
                           const std::string& vanity, std::string& steamidOut) {
    FetchOutcome outcome;
    if (apiKey.empty()) {
        outcome.status = FetchStatus::InvalidApiKey;
        outcome.message = DefaultStatusMessage(FetchStatus::InvalidApiKey);
        return outcome;
    }
    const HttpResponse response = http.Get(ResolveVanityUrl(apiKey, vanity));
    if (!response.Ok()) {
        outcome.status = StatusFromHttp(response, outcome.message);
        return outcome;
    }
    outcome.status = ParseResolveVanityJson(response.body, steamidOut, outcome.message);
    if (outcome.status == FetchStatus::Ok) {
        outcome.message = "Perfil resuelto correctamente.";
    }
    return outcome;
}

FetchOutcome TestApiKey(HttpClient& http, const std::string& apiKey) {
    FetchOutcome outcome;
    if (apiKey.empty()) {
        outcome.status = FetchStatus::InvalidApiKey;
        outcome.message = "No hay ninguna clave guardada.";
        return outcome;
    }
    const HttpResponse response = http.Get(RecentlyPlayedUrl(apiKey, kKeyTestProfile));
    if (response.Ok()) {
        outcome.status = FetchStatus::Ok;
        outcome.message = "La clave de Steam Web API es valida.";
        return outcome;
    }
    outcome.status = StatusFromHttp(response, outcome.message);
    return outcome;
}

}  // namespace srp::steam
