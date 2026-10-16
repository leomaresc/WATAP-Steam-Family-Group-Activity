#include "common/paths.h"

#include <windows.h>

#include <cstdio>
#include <vector>

namespace srp {
namespace {

std::filesystem::path LocalAppDataRoot() {
    wchar_t buffer[MAX_PATH * 4] = {};
    const DWORD len = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer,
                                              static_cast<DWORD>(std::size(buffer)));
    if (len > 0 && len < std::size(buffer)) {
        return std::filesystem::path(buffer);
    }
    // Reserva: perfil del usuario actual.
    wchar_t profile[MAX_PATH * 4] = {};
    const DWORD plen = GetEnvironmentVariableW(L"USERPROFILE", profile,
                                               static_cast<DWORD>(std::size(profile)));
    if (plen > 0 && plen < std::size(profile)) {
        return std::filesystem::path(profile) / L"AppData" / L"Local";
    }
    return std::filesystem::temp_directory_path();
}

}  // namespace

std::filesystem::path AppDataDir() {
    static const std::filesystem::path root = LocalAppDataRoot() / L"SteamRecentPlay";
    return root;
}

std::filesystem::path ConfigFilePath() {
    return AppDataDir() / L"config.json";
}

std::filesystem::path ActivityCacheDir() {
    return AppDataDir() / L"cache";
}

std::filesystem::path ImageCacheDir() {
    return AppDataDir() / L"cache" / L"images";
}

bool EnsureDataDirs() {
    std::error_code ec;
    std::filesystem::create_directories(AppDataDir(), ec);
    ec.clear();
    std::filesystem::create_directories(ActivityCacheDir(), ec);
    ec.clear();
    std::filesystem::create_directories(ImageCacheDir(), ec);
    return std::filesystem::is_directory(ActivityCacheDir());
}

std::string HashKey(const std::string& key) {
    uint64_t hash = 1469598103934665603ULL;  // FNV offset basis
    for (unsigned char c : key) {
        hash ^= c;
        hash *= 1099511628211ULL;  // FNV prime
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(hash));
    return buf;
}

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return std::wstring();
    const int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    if (len <= 0) return std::wstring();
    std::wstring out(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), out.data(), len);
    return out;
}

std::string WideToUtf8(const std::wstring& s) {
    if (s.empty()) return std::string();
    const int len = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()),
                                        nullptr, 0, nullptr, nullptr);
    if (len <= 0) return std::string();
    std::string out(static_cast<size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), out.data(), len,
                        nullptr, nullptr);
    return out;
}

bool ReadFileBytes(const std::filesystem::path& path, std::string& out) {
    out.clear();
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.wstring().c_str(), L"rb") != 0 || f == nullptr) return false;
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    if (size < 0) {
        std::fclose(f);
        return false;
    }
    std::fseek(f, 0, SEEK_SET);
    out.resize(static_cast<size_t>(size));
    const size_t read = size > 0 ? std::fread(out.data(), 1, static_cast<size_t>(size), f) : 0;
    std::fclose(f);
    out.resize(read);
    return read == static_cast<size_t>(size);
}

bool WriteFileBytesAtomic(const std::filesystem::path& path, const std::string& data) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);

    const std::filesystem::path tmp = path.wstring() + L".tmp";
    FILE* f = nullptr;
    if (_wfopen_s(&f, tmp.wstring().c_str(), L"wb") != 0 || f == nullptr) return false;
    const size_t written = data.empty() ? 0 : std::fwrite(data.data(), 1, data.size(), f);
    std::fclose(f);
    if (written != data.size()) {
        std::filesystem::remove(tmp, ec);
        return false;
    }
    // Reemplazo atomico: en Windows std::filesystem::rename sobrescribe si el destino no existe;
    // si existe, se elimina primero para garantizar el reemplazo.
    if (std::filesystem::exists(path, ec)) {
        std::filesystem::remove(path, ec);
    }
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::filesystem::remove(tmp, ec);
        return false;
    }
    return true;
}

}  // namespace srp
