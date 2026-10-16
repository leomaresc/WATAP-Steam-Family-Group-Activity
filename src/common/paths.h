// Rutas de almacenamiento local en %LOCALAPPDATA% y conversiones de texto.
#pragma once

#include <filesystem>
#include <string>

namespace srp {

// Directorio raiz de datos: %LOCALAPPDATA%\SteamRecentPlay
std::filesystem::path AppDataDir();

// Subdirectorios concretos.
std::filesystem::path ConfigFilePath();
std::filesystem::path ActivityCacheDir();
std::filesystem::path ImageCacheDir();

// Crea los directorios necesarios si no existen. Devuelve false si no se pudo.
bool EnsureDataDirs();

// Hash FNV-1a de 64 bits en hexadecimal, usado para nombrar archivos de imagen.
std::string HashKey(const std::string& key);

// Conversiones UTF-8 <-> UTF-16.
std::wstring Utf8ToWide(const std::string& s);
std::string WideToUtf8(const std::wstring& s);

// Lectura/escritura de archivos completos en formato binario.
bool ReadFileBytes(const std::filesystem::path& path, std::string& out);
bool WriteFileBytesAtomic(const std::filesystem::path& path, const std::string& data);

}  // namespace srp
