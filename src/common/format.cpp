#include "common/format.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>

namespace srp {
namespace {

std::string Trim(const std::string& s) {
    size_t begin = 0;
    size_t end = s.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(s[begin]))) ++begin;
    while (end > begin && std::isspace(static_cast<unsigned char>(s[end - 1]))) --end;
    return s.substr(begin, end - begin);
}

std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

bool AllDigits(const std::string& s) {
    if (s.empty()) return false;
    return std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
}

}  // namespace

std::string FormatMinutes(int minutes) {
    if (minutes <= 0) return "0 min";
    const int hours = minutes / 60;
    const int mins = minutes % 60;
    char buf[64];
    if (hours > 0 && mins > 0) {
        std::snprintf(buf, sizeof(buf), "%d h %d min", hours, mins);
    } else if (hours > 0) {
        std::snprintf(buf, sizeof(buf), "%d h", hours);
    } else {
        std::snprintf(buf, sizeof(buf), "%d min", mins);
    }
    return buf;
}

std::string FormatMinutesOrDash(int minutes) {
    if (minutes <= 0) return "--";
    return FormatMinutes(minutes);
}

bool IsValidSteamId64(const std::string& value) {
    const std::string v = Trim(value);
    if (v.size() != 17) return false;
    if (!AllDigits(v)) return false;
    // Los SteamID64 de cuentas individuales empiezan por 7656119.
    return v.rfind("7656119", 0) == 0;
}

ProfileInputKind ParseProfileInput(const std::string& raw, std::string& outValue) {
    outValue.clear();
    const std::string input = Trim(raw);
    if (input.empty()) return ProfileInputKind::Empty;

    // 1) SteamID64 directo.
    if (IsValidSteamId64(input)) {
        outValue = input;
        return ProfileInputKind::SteamId64;
    }

    std::string value = input;
    const std::string lower = ToLower(input);

    // 2) URL con /profiles/<steamid64>/ -> extrae el identificador numerico.
    const std::string profilesTag = "/profiles/";
    size_t pos = lower.find(profilesTag);
    if (pos != std::string::npos) {
        std::string tail = value.substr(pos + profilesTag.size());
        size_t slash = tail.find('/');
        if (slash != std::string::npos) tail = tail.substr(0, slash);
        size_t q = tail.find_first_of("?#");
        if (q != std::string::npos) tail = tail.substr(0, q);
        tail = Trim(tail);
        if (IsValidSteamId64(tail)) {
            outValue = tail;
            return ProfileInputKind::SteamId64;
        }
        return ProfileInputKind::Invalid;
    }

    // 3) URL con /id/<vanity>/ -> extrae el nombre vanity.
    const std::string idTag = "/id/";
    pos = lower.find(idTag);
    if (pos != std::string::npos) {
        std::string tail = value.substr(pos + idTag.size());
        size_t slash = tail.find('/');
        if (slash != std::string::npos) tail = tail.substr(0, slash);
        size_t q = tail.find_first_of("?#");
        if (q != std::string::npos) tail = tail.substr(0, q);
        tail = Trim(tail);
        if (!tail.empty()) {
            outValue = tail;
            return ProfileInputKind::Vanity;
        }
        return ProfileInputKind::Invalid;
    }

    // 4) Nombre vanity escrito a mano (sin URL).
    if (input.find('/') == std::string::npos && input.find(' ') == std::string::npos) {
        // Descarta hostnames sueltos que no aportan un vanity util.
        if (!lower.empty() && lower.find("steamcommunity.com") == std::string::npos) {
            outValue = input;
            return ProfileInputKind::Vanity;
        }
    }

    return ProfileInputKind::Invalid;
}

ActivityLevel ClassifyActivity(int minutes2Weeks, int moderateMinutes, int highMinutes) {
    if (minutes2Weeks <= 0) return ActivityLevel::None;
    if (highMinutes > 0 && minutes2Weeks >= highMinutes) return ActivityLevel::High;
    if (moderateMinutes > 0 && minutes2Weeks >= moderateMinutes) return ActivityLevel::Moderate;
    return ActivityLevel::Low;
}

std::string ActivityLevelLabel(ActivityLevel level) {
    switch (level) {
        case ActivityLevel::High: return "Alta";
        case ActivityLevel::Moderate: return "Moderada";
        case ActivityLevel::Low: return "Baja";
        case ActivityLevel::None:
        default: return "Sin actividad";
    }
}

std::string ActivityLevelDescription(ActivityLevel level) {
    switch (level) {
        case ActivityLevel::High: return "Actividad reciente alta";
        case ActivityLevel::Moderate: return "Actividad reciente moderada";
        case ActivityLevel::Low: return "Actividad reciente baja";
        case ActivityLevel::None:
        default: return "Sin actividad reciente";
    }
}

std::string FormatUnixLocal(int64_t unixSeconds) {
    if (unixSeconds <= 0) return "--";
    const std::time_t t = static_cast<std::time_t>(unixSeconds);
    std::tm tmv{};
#if defined(_WIN32)
    if (localtime_s(&tmv, &t) != 0) return "--";
#else
    if (localtime_r(&t, &tmv) == nullptr) return "--";
#endif
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%02d/%02d/%04d %02d:%02d",
                  tmv.tm_mday, tmv.tm_mon + 1, tmv.tm_year + 1900,
                  tmv.tm_hour, tmv.tm_min);
    return buf;
}

std::string FormatRelativeFromNow(int64_t unixSeconds, int64_t nowUnix) {
    if (unixSeconds <= 0) return "sin consultar";
    int64_t delta = nowUnix - unixSeconds;
    if (delta < 0) delta = 0;
    if (delta < 60) return "hace unos segundos";
    const int64_t minutes = delta / 60;
    if (minutes < 60) return "hace " + std::to_string(minutes) + " min";
    const int64_t hours = minutes / 60;
    if (hours < 24) return "hace " + std::to_string(hours) + " h";
    const int64_t days = hours / 24;
    if (days == 1) return "hace 1 dia";
    return "hace " + std::to_string(days) + " dias";
}

std::string FetchStatusLabel(FetchStatus status) {
    switch (status) {
        case FetchStatus::Ok: return "Datos disponibles";
        case FetchStatus::NoRecentGames: return "Sin juegos recientes";
        case FetchStatus::PrivateOrUnavailable: return "Datos restringidos";
        case FetchStatus::NetworkError: return "Error de conexion";
        case FetchStatus::InvalidApiKey: return "Clave de API invalida";
        case FetchStatus::InvalidProfile: return "Perfil no valido";
        case FetchStatus::RateLimited: return "Limite de solicitudes";
        case FetchStatus::NotFetched:
        default: return "Sin consultar";
    }
}

std::string DefaultStatusMessage(FetchStatus status) {
    switch (status) {
        case FetchStatus::Ok:
            return "Consulta correcta.";
        case FetchStatus::NoRecentGames:
            return "La consulta fue correcta pero Steam no devolvio juegos recientes. "
                   "Puede que no haya jugado en las ultimas dos semanas o que su perfil "
                   "restrinja la visibilidad de la actividad.";
        case FetchStatus::PrivateOrUnavailable:
            return "Steam no permite leer la actividad de este perfil (privacidad o perfil inexistente). "
                   "La ausencia de datos no demuestra que no haya jugado.";
        case FetchStatus::NetworkError:
            return "No se pudo contactar con Steam. Se muestran los ultimos datos guardados si existen.";
        case FetchStatus::InvalidApiKey:
            return "La clave de Steam Web API falta o ha sido rechazada. Introduzca una clave valida en Ajustes.";
        case FetchStatus::InvalidProfile:
            return "El perfil indicado no existe o no es valido.";
        case FetchStatus::RateLimited:
            return "Se han realizado demasiadas solicitudes. Espere unos minutos antes de reintentar.";
        case FetchStatus::NotFetched:
        default:
            return "Pulse Actualizar para consultar la actividad reciente.";
    }
}

int64_t UnixNow() {
    return static_cast<int64_t>(std::time(nullptr));
}

}  // namespace srp
