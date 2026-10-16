// Utilidades de formateo, validacion de identificadores y clasificacion de actividad.
// Sin dependencias de la interfaz grafica: todo es comprobable desde las pruebas.
#pragma once

#include <cstdint>
#include <string>

#include "common/models.h"

namespace srp {

// Convierte minutos a un texto legible en espanol: "8 h 35 min", "45 min", "2 h".
std::string FormatMinutes(int minutes);

// Igual que FormatMinutes pero devuelve un guion cuando no hay actividad (<= 0).
std::string FormatMinutesOrDash(int minutes);

// Comprueba que la cadena sea un SteamID64 valido (17 digitos, formato 7656119...).
bool IsValidSteamId64(const std::string& value);

// Tipo de entrada reconocida en el campo de alta de perfiles.
enum class ProfileInputKind {
    SteamId64,
    Vanity,       // requiere resolver mediante ResolveVanityURL
    Empty,
    Invalid
};

// Analiza un SteamID64 o una URL/nombre de perfil de Steam.
// Rellena outValue con el SteamID64 o el nombre vanity.
ProfileInputKind ParseProfileInput(const std::string& input, std::string& outValue);

// Clasifica los minutos recientes en un nivel de actividad.
ActivityLevel ClassifyActivity(int minutes2Weeks, int moderateMinutes, int highMinutes);

// Etiqueta en espanol del nivel de actividad: "Alta", "Moderada", "Baja", "Sin actividad".
std::string ActivityLevelLabel(ActivityLevel level);

// Etiqueta descriptiva mas larga para la interfaz.
std::string ActivityLevelDescription(ActivityLevel level);

// Formatea una marca de tiempo Unix como fecha local "dd/mm/aaaa hh:mm".
std::string FormatUnixLocal(int64_t unixSeconds);

// Texto relativo aproximado: "hace 5 min", "hace 3 h", "hace 2 dias".
std::string FormatRelativeFromNow(int64_t unixSeconds, int64_t nowUnix);

// Descripcion legible de un estado de consulta.
std::string FetchStatusLabel(FetchStatus status);

// Mensaje orientativo por defecto para un estado de consulta.
std::string DefaultStatusMessage(FetchStatus status);

// Fecha local del dia (00:00) para agrupar, en segundos Unix.
int64_t UnixNow();

}  // namespace srp
