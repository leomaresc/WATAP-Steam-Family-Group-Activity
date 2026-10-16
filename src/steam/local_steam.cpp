#include "steam/local_steam.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <vector>

#include "common/format.h"
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

// --- Analizador VDF en arbol (necesario para localconfig.vdf, muy anidado) ---
struct VdfNode {
    std::string scalar;                                    // valor si el nodo es una hoja
    std::vector<std::pair<std::string, VdfNode>> children;  // hijos si el nodo es un bloque
    bool block = false;

    // Busca un hijo por clave (sin distinguir mayusculas, como tolera Steam).
    const VdfNode* Find(const std::string& key) const {
        const std::string target = LowerAscii(key);
        for (const auto& entry : children) {
            if (LowerAscii(entry.first) == target) return &entry.second;
        }
        return nullptr;
    }

    // Busca una clave en todo el subarbol (por si Valve reubica el bloque).
    const VdfNode* FindRecursive(const std::string& key) const {
        if (const VdfNode* direct = Find(key)) return direct;
        for (const auto& entry : children) {
            if (!entry.second.block) continue;
            if (const VdfNode* nested = entry.second.FindRecursive(key)) return nested;
        }
        return nullptr;
    }
};

// Lector recursivo de VDF: cadenas entre comillas, llaves y comentarios de linea.
class VdfReader {
public:
    explicit VdfReader(const std::string& text) : text_(text) {}

    // Lee el documento: una clave raiz seguida de un bloque.
    bool ParseDocument(VdfNode& root) {
        std::string key;
        if (!ReadString(key)) return false;
        if (!Consume('{')) return false;
        return ParseBlock(root);
    }

private:
    const std::string& text_;
    size_t pos_ = 0;

    void SkipSpace() {
        while (pos_ < text_.size()) {
            const char c = text_[pos_];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                ++pos_;
                continue;
            }
            if (c == '/' && pos_ + 1 < text_.size() && text_[pos_ + 1] == '/') {
                while (pos_ < text_.size() && text_[pos_] != '\n') ++pos_;
                continue;
            }
            break;
        }
    }

    bool Consume(char c) {
        SkipSpace();
        if (pos_ < text_.size() && text_[pos_] == c) {
            ++pos_;
            return true;
        }
        return false;
    }

    bool ReadString(std::string& out) {
        SkipSpace();
        if (pos_ >= text_.size() || text_[pos_] != '"') return false;
        ++pos_;
        out.clear();
        while (pos_ < text_.size() && text_[pos_] != '"') {
            if (text_[pos_] == '\\' && pos_ + 1 < text_.size()) ++pos_;  // desescapa el siguiente
            out.push_back(text_[pos_]);
            ++pos_;
        }
        if (pos_ >= text_.size()) return false;
        ++pos_;  // comilla de cierre
        return true;
    }

    bool ParseBlock(VdfNode& node) {
        node.block = true;
        for (;;) {
            SkipSpace();
            if (pos_ >= text_.size()) return true;  // fin de archivo
            if (text_[pos_] == '}') {
                ++pos_;
                return true;
            }
            std::string key;
            if (!ReadString(key)) return false;
            SkipSpace();
            VdfNode child;
            if (pos_ < text_.size() && text_[pos_] == '{') {
                ++pos_;
                if (!ParseBlock(child)) return false;
            } else {
                if (!ReadString(child.scalar)) return false;
            }
            node.children.emplace_back(std::move(key), std::move(child));
        }
    }
};

// Base para convertir un accountid (32 bits) en SteamID64.
constexpr uint64_t kSteamId64Base = 76561197960265728ULL;

std::string AccountIdToSteamId64(const std::string& accountId) {
    const uint64_t id = std::strtoull(accountId.c_str(), nullptr, 10);
    if (id == 0) return {};
    return std::to_string(kSteamId64Base + id);
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

std::filesystem::path SteamLocalConfigPath(const std::filesystem::path& steamDir,
                                           const std::string& accountId) {
    return steamDir / L"userdata" / std::filesystem::path(accountId) / L"config" /
           L"localconfig.vdf";
}

bool ParseLocalFamilyGroupVdf(const std::string& content, LocalFamilyGroup& out) {
    out = LocalFamilyGroup{};
    VdfReader reader(content);
    VdfNode root;
    if (!reader.ParseDocument(root)) return false;

    const VdfNode* family = root.FindRecursive("FamilyGroup");
    if (family == nullptr || !family->block) return false;

    if (const VdfNode* id = family->Find("groupid")) out.groupId = id->scalar;
    if (const VdfNode* name = family->Find("name")) out.name = name->scalar;

    if (const VdfNode* members = family->Find("members"); members != nullptr && members->block) {
        for (const auto& entry : members->children) {
            if (!entry.second.block) continue;
            const VdfNode* account = entry.second.Find("accountid");
            if (account == nullptr) continue;
            const std::string steamid = AccountIdToSteamId64(account->scalar);
            if (!IsValidSteamId64(steamid)) continue;
            if (std::find(out.memberSteamIds.begin(), out.memberSteamIds.end(), steamid) ==
                out.memberSteamIds.end()) {
                out.memberSteamIds.push_back(steamid);
            }
        }
    }

    // Un grupo valido tiene identificador o al menos un miembro reconocible.
    return !out.groupId.empty() || !out.memberSteamIds.empty();
}

bool DetectLocalFamilyGroup(const std::string& ownSteamId64, LocalFamilyGroup& out,
                            std::string& errorOut) {
    errorOut.clear();
    const std::filesystem::path dir = SteamInstallDir();
    if (dir.empty()) {
        errorOut = "No se encontro la instalacion de Steam en el registro de Windows.";
        return false;
    }

    // Carpeta de la cuenta propia (para preferirla si hay varias cuentas).
    std::string ownAccountId;
    if (IsValidSteamId64(ownSteamId64)) {
        const uint64_t id = std::strtoull(ownSteamId64.c_str(), nullptr, 10);
        if (id > kSteamId64Base) ownAccountId = std::to_string(id - kSteamId64Base);
    }

    std::error_code ec;
    const std::filesystem::path userdata = dir / L"userdata";
    if (!std::filesystem::is_directory(userdata, ec)) {
        errorOut =
            "No se encontro la carpeta userdata de Steam; abre el cliente una vez e intentalo de nuevo.";
        return false;
    }

    bool found = false;
    for (std::filesystem::directory_iterator it(userdata, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_directory(ec)) continue;
        const std::filesystem::path path = it->path() / L"config" / L"localconfig.vdf";
        std::string content;
        if (!ReadFileBytes(path, content)) continue;
        LocalFamilyGroup group;
        if (!ParseLocalFamilyGroupVdf(content, group)) continue;

        const bool isOwn =
            !ownAccountId.empty() && it->path().filename().string() == ownAccountId;
        if (!found || isOwn) {
            out = std::move(group);
            found = true;
            if (isOwn) break;
        }
    }

    if (!found) {
        errorOut =
            "Esta cuenta no pertenece a ningun grupo familiar de Steam, o el cliente aun no lo ha "
            "descargado. Abre Steam, entra en Familia y vuelve a intentarlo.";
        return false;
    }
    return true;
}

}  // namespace srp::steam
