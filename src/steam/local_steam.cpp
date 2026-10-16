#include "steam/local_steam.h"

#include <windows.h>

#include <cctype>
#include <cstdlib>
#include <vector>

#include "common/paths.h"

namespace srp::steam {
namespace {

// Convierte el texto de un VDF en una lista de tokens: cadenas entre comillas,
// llaves sueltas y valores sin comillas. Ignora comentarios de linea (//).
std::vector<std::string> TokenizeVdf(const std::string& text) {
    std::vector<std::string> tokens;
    const size_t n = text.size();
    size_t i = 0;
    while (i < n) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == ',') {
            ++i;
            continue;
        }
        if (c == '/' && i + 1 < n && text[i + 1] == '/') {
            while (i < n && text[i] != '\n') ++i;
            continue;
        }
        if (c == '{' || c == '}') {
            tokens.emplace_back(1, c);
            ++i;
            continue;
        }
        if (c == '"') {
            ++i;
            std::string value;
            while (i < n && text[i] != '"') {
                if (text[i] == '\\' && i + 1 < n) {
                    ++i;
                }
                value.push_back(text[i]);
                ++i;
            }
            if (i < n) ++i;  // comilla de cierre
            tokens.push_back(std::move(value));
            continue;
        }
        // Valor sin comillas (numero o palabra).
        std::string value;
        while (i < n && !std::isspace(static_cast<unsigned char>(text[i])) && text[i] != '{' &&
               text[i] != '}' && text[i] != '"') {
            value.push_back(text[i]);
            ++i;
        }
        if (!value.empty()) tokens.push_back(std::move(value));
    }
    return tokens;
}

bool IsSteamId64Token(const std::string& value) {
    if (value.size() != 17) return false;
    for (char c : value) {
        if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    }
    return true;
}

std::string LowerAscii(std::string value) {
    for (char& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}

std::filesystem::path ReadRegString(HKEY root, const wchar_t* subkey, const wchar_t* value) {
    wchar_t buffer[1024] = {};
    DWORD size = sizeof(buffer);
    DWORD type = 0;
    const LSTATUS status =
        RegGetValueW(root, subkey, value, RRF_RT_REG_SZ, &type, buffer, &size);
    if (status != ERROR_SUCCESS) return std::filesystem::path();
    return std::filesystem::path(buffer);
}

}  // namespace

std::filesystem::path SteamInstallDir() {
    std::filesystem::path dir =
        ReadRegString(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath");
    if (dir.empty()) {
        dir = ReadRegString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Valve\\Steam",
                            L"InstallPath");
    }
    if (dir.empty()) {
        dir = ReadRegString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Valve\\Steam", L"InstallPath");
    }
    return dir;
}

std::filesystem::path SteamLoginUsersPath(const std::filesystem::path& steamDir) {
    return steamDir / L"config" / L"loginusers.vdf";
}

bool ParseLoginUsersVdf(const std::string& content, LocalAccount& out) {
    const std::vector<std::string> tokens = TokenizeVdf(content);

    LocalAccount best;
    bool have = false;

    for (size_t i = 0; i + 1 < tokens.size(); ++i) {
        if (!IsSteamId64Token(tokens[i]) || tokens[i + 1] != "{") continue;

        LocalAccount account;
        account.steamid64 = tokens[i];

        int depth = 0;
        for (size_t j = i + 1; j < tokens.size(); ++j) {
            if (tokens[j] == "{") {
                ++depth;
                continue;
            }
            if (tokens[j] == "}") {
                --depth;
                if (depth == 0) break;
                continue;
            }
            if (depth == 1 && j + 1 < tokens.size() && tokens[j + 1] != "{" &&
                tokens[j + 1] != "}") {
                const std::string key = LowerAscii(tokens[j]);
                const std::string& value = tokens[j + 1];
                if (key == "mostrecent") {
                    account.mostRecent = value == "1";
                } else if (key == "timestamp") {
                    account.timestamp = std::strtoll(value.c_str(), nullptr, 10);
                } else if (key == "personaname") {
                    account.personaName = value;
                } else if (key == "accountname") {
                    account.accountName = value;
                }
                ++j;  // salta el valor ya leido
            }
        }

        if (!have) {
            best = account;
            have = true;
        } else {
            const bool betterRecent = account.mostRecent && !best.mostRecent;
            const bool sameRecent = account.mostRecent == best.mostRecent;
            if (betterRecent || (sameRecent && account.timestamp > best.timestamp)) {
                best = account;
            }
        }
    }

    if (!have) return false;
    out = best;
    return true;
}

bool DetectRecentSteamAccount(LocalAccount& out, std::string& errorOut) {
    errorOut.clear();
    const std::filesystem::path dir = SteamInstallDir();
    if (dir.empty()) {
        errorOut = "No se encontro la instalacion de Steam en el registro de Windows.";
        return false;
    }
    const std::filesystem::path path = SteamLoginUsersPath(dir);
    std::string content;
    if (!ReadFileBytes(path, content)) {
        errorOut = "No se pudo leer " + WideToUtf8(path.wstring()) + ".";
        return false;
    }
    if (!ParseLoginUsersVdf(content, out)) {
        errorOut = "No se encontro ninguna cuenta de Steam en loginusers.vdf.";
        return false;
    }
    return true;
}

}  // namespace srp::steam
