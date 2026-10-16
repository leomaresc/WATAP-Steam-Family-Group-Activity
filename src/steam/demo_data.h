// Datos ficticios para el modo de demostracion (sin clave de API o sin conexion).
// Todos los perfiles y tiempos son inventados y se marcan claramente como "DEMO".
#pragma once

#include <string>
#include <vector>

#include "common/models.h"

namespace srp::demo {

// Identificador del perfil demo segun su indice (0..2).
std::vector<Profile> DemoProfiles();

// Actividad ficticia de un perfil demo (por steamid64). Devuelve false si no existe.
bool DemoActivityFor(const std::string& steamid64, ProfileActivity& out);

// Indica si un steamid64 pertenece al conjunto de demostracion.
bool IsDemoProfile(const std::string& steamid64);

}  // namespace srp::demo
