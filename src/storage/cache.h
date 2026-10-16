// Cache local de respuestas de actividad por perfil.
// Se guarda un unico archivo por perfil con la ultima consulta correcta.
#pragma once

#include <filesystem>
#include <string>

#include "common/models.h"

namespace srp::cache {

std::filesystem::path ActivityPath(const std::string& steamid64);

// Serializacion (pura, comprobable).
std::string ActivityToJson(const ProfileActivity& activity);
bool ActivityFromJson(const std::string& json, ProfileActivity& out, std::string& errorOut);

// Escritura/lectura en disco.
bool SaveActivity(const ProfileActivity& activity);
bool LoadActivity(const std::string& steamid64, ProfileActivity& out);

// Elimina toda la cache (respuestas e imagenes).
void ClearAll();

}  // namespace srp::cache
