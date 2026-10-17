#include "storage/credentials.h"

#include <windows.h>
#include <wincred.h>

#include <vector>

#include "common/paths.h"

namespace srp::creds {
namespace {

constexpr const wchar_t* kTargetName = L"WATAP/SteamWebApiKey";
// Nombre usado por la version anterior de la aplicacion; se migra al arrancar.
constexpr const wchar_t* kLegacyTargetName = L"SteamRecentPlay/SteamWebApiKey";

// Lee la clave de un destino concreto del Administrador de credenciales.
bool ReadKeyForTarget(const wchar_t* target, std::string& apiKeyOut) {
    apiKeyOut.clear();
    PCREDENTIALW cred = nullptr;
    if (!CredReadW(target, CRED_TYPE_GENERIC, 0, &cred) || cred == nullptr) {
        return false;
    }
    const std::wstring key(reinterpret_cast<const wchar_t*>(cred->CredentialBlob),
                           cred->CredentialBlobSize / sizeof(wchar_t));
    CredFree(cred);
    apiKeyOut = WideToUtf8(key);
    return !apiKeyOut.empty();
}

}  // namespace

bool SaveApiKey(const std::string& apiKey, std::string& errorOut) {
    errorOut.clear();
    if (apiKey.empty()) {
        errorOut = "La clave esta vacia.";
        return false;
    }
    const std::wstring key = Utf8ToWide(apiKey);

    CREDENTIALW cred{};
    cred.Type = CRED_TYPE_GENERIC;
    cred.TargetName = const_cast<wchar_t*>(kTargetName);
    cred.CredentialBlobSize = static_cast<DWORD>(key.size() * sizeof(wchar_t));
    cred.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<wchar_t*>(key.c_str()));
    cred.Persist = CRED_PERSIST_LOCAL_MACHINE;
    cred.UserName = const_cast<wchar_t*>(L"Steam Web API");

    if (!CredWriteW(&cred, 0)) {
        errorOut = "No se pudo guardar la clave en el Administrador de credenciales (" +
                   std::to_string(GetLastError()) + ").";
        return false;
    }
    return true;
}

bool LoadApiKey(std::string& apiKeyOut) {
    return ReadKeyForTarget(kTargetName, apiKeyOut);
}

bool DeleteApiKey(std::string& errorOut) {
    errorOut.clear();
    // Elimina tambien el nombre antiguo por si quedara de una version previa.
    CredDeleteW(kLegacyTargetName, CRED_TYPE_GENERIC, 0);
    if (!CredDeleteW(kTargetName, CRED_TYPE_GENERIC, 0)) {
        const DWORD error = GetLastError();
        if (error == ERROR_NOT_FOUND) return true;  // ya no existia
        errorOut = "No se pudo eliminar la clave (" + std::to_string(error) + ").";
        return false;
    }
    return true;
}

bool HasApiKey() {
    std::string key;
    return LoadApiKey(key);
}

void MigrateLegacyCredentials() {
    std::string key;
    if (ReadKeyForTarget(kTargetName, key)) return;        // ya hay clave con el nombre nuevo
    if (!ReadKeyForTarget(kLegacyTargetName, key)) return;  // nada que migrar
    std::string error;
    if (SaveApiKey(key, error)) {
        CredDeleteW(kLegacyTargetName, CRED_TYPE_GENERIC, 0);
    }
}

}  // namespace srp::creds
