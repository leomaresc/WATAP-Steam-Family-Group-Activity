#include "storage/credentials.h"

#include <windows.h>
#include <wincred.h>

#include <vector>

#include "common/paths.h"

namespace srp::creds {
namespace {

constexpr const wchar_t* kTargetName = L"SteamRecentPlay/SteamWebApiKey";

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
    apiKeyOut.clear();
    PCREDENTIALW cred = nullptr;
    if (!CredReadW(kTargetName, CRED_TYPE_GENERIC, 0, &cred) || cred == nullptr) {
        return false;
    }
    const std::wstring key(reinterpret_cast<const wchar_t*>(cred->CredentialBlob),
                           cred->CredentialBlobSize / sizeof(wchar_t));
    CredFree(cred);
    apiKeyOut = WideToUtf8(key);
    return !apiKeyOut.empty();
}

bool DeleteApiKey(std::string& errorOut) {
    errorOut.clear();
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

}  // namespace srp::creds
