#include "storage/cache.h"

#include <nlohmann/json.hpp>

#include "common/format.h"
#include "common/paths.h"

namespace srp::cache {
namespace {

using Json = nlohmann::json;

const char* StatusToString(FetchStatus status) {
    switch (status) {
        case FetchStatus::Ok: return "ok";
        case FetchStatus::NoRecentGames: return "no_recent_games";
        case FetchStatus::PrivateOrUnavailable: return "private";
        case FetchStatus::NetworkError: return "network_error";
        case FetchStatus::InvalidApiKey: return "invalid_api_key";
        case FetchStatus::InvalidProfile: return "invalid_profile";
        case FetchStatus::RateLimited: return "rate_limited";
        case FetchStatus::NotFetched:
        default: return "not_fetched";
    }
}

FetchStatus StatusFromString(const std::string& value) {
    if (value == "ok") return FetchStatus::Ok;
    if (value == "no_recent_games") return FetchStatus::NoRecentGames;
    if (value == "private") return FetchStatus::PrivateOrUnavailable;
    if (value == "network_error") return FetchStatus::NetworkError;
    if (value == "invalid_api_key") return FetchStatus::InvalidApiKey;
    if (value == "invalid_profile") return FetchStatus::InvalidProfile;
    if (value == "rate_limited") return FetchStatus::RateLimited;
    return FetchStatus::NotFetched;
}

}  // namespace

std::filesystem::path ActivityPath(const std::string& steamid64) {
    return ActivityCacheDir() / (HashKey("activity:" + steamid64) + ".json");
}

std::string ActivityToJson(const ProfileActivity& activity) {
    Json root;
    root["steamid64"] = activity.steamid64;
    root["personaName"] = activity.personaName;
    root["avatarUrl"] = activity.avatarUrl;
    root["status"] = StatusToString(activity.status);
    root["statusMessage"] = activity.statusMessage;
    root["fetchedAt"] = activity.fetchedAtUnix;

    Json games = Json::array();
    for (const Game& game : activity.games) {
        Json item;
        item["appid"] = game.appid;
        item["name"] = game.name;
        item["playtime2Weeks"] = game.playtime2Weeks;
        item["playtimeForever"] = game.playtimeForever;
        item["hasPlaytimeForever"] = game.hasPlaytimeForever;
        games.push_back(std::move(item));
    }
    root["games"] = std::move(games);
    return root.dump(2);
}

bool ActivityFromJson(const std::string& json, ProfileActivity& out, std::string& errorOut) {
    errorOut.clear();
    const Json root = Json::parse(json, nullptr, /*allow_exceptions=*/false);
    if (root.is_discarded() || !root.is_object()) {
        errorOut = "Cache no valida.";
        return false;
    }

    ProfileActivity activity;
    if (root.contains("steamid64") && root["steamid64"].is_string()) {
        activity.steamid64 = root["steamid64"].get<std::string>();
    }
    if (activity.steamid64.empty()) {
        errorOut = "Cache sin identificador de perfil.";
        return false;
    }
    if (root.contains("personaName") && root["personaName"].is_string()) {
        activity.personaName = root["personaName"].get<std::string>();
    }
    if (root.contains("avatarUrl") && root["avatarUrl"].is_string()) {
        activity.avatarUrl = root["avatarUrl"].get<std::string>();
    }
    if (root.contains("status") && root["status"].is_string()) {
        activity.status = StatusFromString(root["status"].get<std::string>());
    }
    if (root.contains("statusMessage") && root["statusMessage"].is_string()) {
        activity.statusMessage = root["statusMessage"].get<std::string>();
    }
    if (root.contains("fetchedAt") && root["fetchedAt"].is_number()) {
        activity.fetchedAtUnix = root["fetchedAt"].get<int64_t>();
    }
    if (root.contains("games") && root["games"].is_array()) {
        for (const Json& item : root["games"]) {
            if (!item.is_object()) continue;
            Game game;
            if (item.contains("appid") && item["appid"].is_number()) {
                game.appid = static_cast<uint32_t>(item["appid"].get<int64_t>());
            }
            if (item.contains("name") && item["name"].is_string()) {
                game.name = item["name"].get<std::string>();
            }
            if (item.contains("playtime2Weeks") && item["playtime2Weeks"].is_number()) {
                game.playtime2Weeks = static_cast<int>(item["playtime2Weeks"].get<int64_t>());
            }
            if (item.contains("playtimeForever") && item["playtimeForever"].is_number()) {
                game.playtimeForever = static_cast<int>(item["playtimeForever"].get<int64_t>());
            }
            if (item.contains("hasPlaytimeForever") && item["hasPlaytimeForever"].is_boolean()) {
                game.hasPlaytimeForever = item["hasPlaytimeForever"].get<bool>();
            }
            activity.games.push_back(std::move(game));
        }
    }

    out = std::move(activity);
    return true;
}

bool SaveActivity(const ProfileActivity& activity) {
    if (activity.steamid64.empty()) return false;
    if (!EnsureDataDirs()) return false;
    return WriteFileBytesAtomic(ActivityPath(activity.steamid64), ActivityToJson(activity));
}

bool LoadActivity(const std::string& steamid64, ProfileActivity& out) {
    std::string data;
    if (!ReadFileBytes(ActivityPath(steamid64), data)) return false;
    std::string error;
    if (!ActivityFromJson(data, out, error)) return false;
    out.fromCache = true;
    return true;
}

void ClearAll() {
    std::error_code ec;
    std::filesystem::remove_all(ActivityCacheDir(), ec);
}

}  // namespace srp::cache
