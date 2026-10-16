#include "storage/config.h"

#include <algorithm>
#include <nlohmann/json.hpp>

#include "common/format.h"
#include "common/paths.h"

namespace srp {
namespace {

using Json = nlohmann::json;

constexpr int kMinAutoRefreshMinutes = 5;
constexpr int kMaxAutoRefreshMinutes = 24 * 60;
constexpr int kMinFilterMinutes = 0;
constexpr int kMaxFilterMinutes = 100000;
constexpr int kMinThresholdMinutes = 1;
constexpr int kMaxThresholdMinutes = 1000000;

int Clamp(int value, int lo, int hi) {
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

std::string GetString(const Json& obj, const char* key, const std::string& fallback) {
    if (obj.contains(key) && obj[key].is_string()) return obj[key].get<std::string>();
    return fallback;
}

int GetInt(const Json& obj, const char* key, int fallback) {
    if (obj.contains(key) && obj[key].is_number_integer()) return obj[key].get<int>();
    if (obj.contains(key) && obj[key].is_number()) return static_cast<int>(obj[key].get<double>());
    return fallback;
}

bool GetBool(const Json& obj, const char* key, bool fallback) {
    if (obj.contains(key) && obj[key].is_boolean()) return obj[key].get<bool>();
    return fallback;
}

}  // namespace

AppConfig DefaultConfig() {
    AppConfig config;
    config.version = 1;
    config.prefs = Preferences{};
    return config;
}

int SanitizeConfig(AppConfig& config) {
    config.version = 1;

    Preferences& p = config.prefs;
    p.autoRefreshMinutes = Clamp(p.autoRefreshMinutes, kMinAutoRefreshMinutes, kMaxAutoRefreshMinutes);
    p.lowActivityFilterMinutes = Clamp(p.lowActivityFilterMinutes, kMinFilterMinutes, kMaxFilterMinutes);
    p.highActivityMinutes = Clamp(p.highActivityMinutes, kMinThresholdMinutes, kMaxThresholdMinutes);
    p.moderateActivityMinutes = Clamp(p.moderateActivityMinutes, kMinThresholdMinutes,
                                      kMaxThresholdMinutes);
    if (p.moderateActivityMinutes > p.highActivityMinutes) {
        std::swap(p.moderateActivityMinutes, p.highActivityMinutes);
    }
    if (p.moderateActivityMinutes == p.highActivityMinutes) {
        p.highActivityMinutes = p.moderateActivityMinutes + 1;
    }

    int dropped = 0;
    std::vector<Profile> valid;
    valid.reserve(config.profiles.size());
    for (Profile& profile : config.profiles) {
        if (!IsValidSteamId64(profile.steamid64)) {
            ++dropped;
            continue;
        }
        // Evita duplicados conservando el primero.
        const bool duplicate = std::any_of(valid.begin(), valid.end(), [&](const Profile& existing) {
            return existing.steamid64 == profile.steamid64;
        });
        if (duplicate) {
            ++dropped;
            continue;
        }
        valid.push_back(std::move(profile));
    }
    config.profiles = std::move(valid);

    if (!IsValidSteamId64(config.ownSteamId64)) {
        config.ownSteamId64.clear();
        config.ownPersonaName.clear();
    }

    if (!config.prefs.lastSelectedProfile.empty() &&
        !std::any_of(config.profiles.begin(), config.profiles.end(), [&](const Profile& profile) {
            return profile.steamid64 == config.prefs.lastSelectedProfile;
        })) {
        config.prefs.lastSelectedProfile.clear();
    }

    return dropped;
}

std::string ConfigToJson(const AppConfig& config) {
    Json root;
    root["version"] = config.version;

    Json profiles = Json::array();
    for (const Profile& profile : config.profiles) {
        Json item;
        item["steamid64"] = profile.steamid64;
        item["personaName"] = profile.personaName;
        item["avatarUrl"] = profile.avatarUrl;
        item["alias"] = profile.alias;
        profiles.push_back(std::move(item));
    }
    root["profiles"] = std::move(profiles);

    root["ownSteamId64"] = config.ownSteamId64;
    root["ownPersonaName"] = config.ownPersonaName;

    Json prefs;
    prefs["autoRefreshEnabled"] = config.prefs.autoRefreshEnabled;
    prefs["autoRefreshMinutes"] = config.prefs.autoRefreshMinutes;
    prefs["hideLowActivity"] = config.prefs.hideLowActivity;
    prefs["lowActivityFilterMinutes"] = config.prefs.lowActivityFilterMinutes;
    prefs["moderateActivityMinutes"] = config.prefs.moderateActivityMinutes;
    prefs["highActivityMinutes"] = config.prefs.highActivityMinutes;
    prefs["showCovers"] = config.prefs.showCovers;
    prefs["lastSelectedProfile"] = config.prefs.lastSelectedProfile;
    root["preferences"] = std::move(prefs);

    return root.dump(2);
}

bool ConfigFromJson(const std::string& json, AppConfig& out, std::string& errorOut) {
    errorOut.clear();
    const Json root = Json::parse(json, nullptr, /*allow_exceptions=*/false);
    if (root.is_discarded() || !root.is_object()) {
        errorOut = "El archivo de configuracion no es un JSON valido.";
        return false;
    }

    AppConfig config = DefaultConfig();
    config.version = GetInt(root, "version", 1);
    config.ownSteamId64 = GetString(root, "ownSteamId64", "");
    config.ownPersonaName = GetString(root, "ownPersonaName", "");

    if (root.contains("profiles") && root["profiles"].is_array()) {
        for (const Json& item : root["profiles"]) {
            if (!item.is_object()) continue;
            Profile profile;
            profile.steamid64 = GetString(item, "steamid64", "");
            profile.personaName = GetString(item, "personaName", "");
            profile.avatarUrl = GetString(item, "avatarUrl", "");
            profile.alias = GetString(item, "alias", "");
            config.profiles.push_back(std::move(profile));
        }
    }

    if (root.contains("preferences") && root["preferences"].is_object()) {
        const Json& prefs = root["preferences"];
        config.prefs.autoRefreshEnabled = GetBool(prefs, "autoRefreshEnabled", false);
        config.prefs.autoRefreshMinutes = GetInt(prefs, "autoRefreshMinutes", 30);
        config.prefs.hideLowActivity = GetBool(prefs, "hideLowActivity", false);
        config.prefs.lowActivityFilterMinutes = GetInt(prefs, "lowActivityFilterMinutes", 30);
        config.prefs.moderateActivityMinutes = GetInt(prefs, "moderateActivityMinutes", 120);
        config.prefs.highActivityMinutes = GetInt(prefs, "highActivityMinutes", 480);
        config.prefs.showCovers = GetBool(prefs, "showCovers", true);
        config.prefs.lastSelectedProfile = GetString(prefs, "lastSelectedProfile", "");
    }

    SanitizeConfig(config);
    out = std::move(config);
    return true;
}

bool LoadConfig(AppConfig& out, std::string& errorOut) {
    errorOut.clear();
    const std::filesystem::path path = ConfigFilePath();
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        out = DefaultConfig();
        return true;
    }

    std::string data;
    if (!ReadFileBytes(path, data)) {
        errorOut = "No se pudo leer el archivo de configuracion.";
        out = DefaultConfig();
        return false;
    }

    if (!ConfigFromJson(data, out, errorOut)) {
        // Copia de seguridad del archivo corrupto y arranque con valores por defecto.
        const std::filesystem::path backup = path.wstring() + L".corrupt";
        std::filesystem::copy_file(path, backup, std::filesystem::copy_options::overwrite_existing, ec);
        out = DefaultConfig();
        return false;
    }
    return true;
}

bool SaveConfig(const AppConfig& config, std::string& errorOut) {
    errorOut.clear();
    if (!EnsureDataDirs()) {
        errorOut = "No se pudo preparar el directorio de datos.";
        return false;
    }
    const std::string json = ConfigToJson(config);
    if (!WriteFileBytesAtomic(ConfigFilePath(), json)) {
        errorOut = "No se pudo escribir la configuracion.";
        return false;
    }
    return true;
}

}  // namespace srp
