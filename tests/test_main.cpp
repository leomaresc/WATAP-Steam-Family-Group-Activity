// Pruebas de la logica no visual: analisis JSON, formateo, validacion de perfiles,
// clasificacion de actividad, ordenacion y persistencia (config/cache).
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "common/format.h"
#include "common/models.h"
#include "common/paths.h"
#include "steam/demo_data.h"
#include "steam/local_steam.h"
#include "steam/steam_api.h"
#include "storage/cache.h"
#include "storage/config.h"

namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool condition, const std::string& label) {
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::printf("  [FALLO] %s\n", label.c_str());
    }
}

void CheckEq(const std::string& actual, const std::string& expected, const std::string& label) {
    ++g_checks;
    if (actual != expected) {
        ++g_failures;
        std::printf("  [FALLO] %s\n     esperado: '%s'\n     obtenido: '%s'\n",
                    label.c_str(), expected.c_str(), actual.c_str());
    }
}

void CheckEqInt(long long actual, long long expected, const std::string& label) {
    ++g_checks;
    if (actual != expected) {
        ++g_failures;
        std::printf("  [FALLO] %s (esperado %lld, obtenido %lld)\n", label.c_str(), expected, actual);
    }
}

void Section(const char* name) { std::printf("== %s ==\n", name); }

// --- Formateo de minutos ---
void TestFormatMinutes() {
    Section("Formateo de minutos");
    CheckEq(srp::FormatMinutes(0), "0 min", "0 minutos");
    CheckEq(srp::FormatMinutes(45), "45 min", "45 minutos");
    CheckEq(srp::FormatMinutes(60), "1 h", "60 minutos = 1 h");
    CheckEq(srp::FormatMinutes(120), "2 h", "120 minutos = 2 h");
    CheckEq(srp::FormatMinutes(515), "8 h 35 min", "515 minutos = 8 h 35 min");
    CheckEq(srp::FormatMinutes(-10), "0 min", "valores negativos");
    CheckEq(srp::FormatMinutesOrDash(0), "--", "guion cuando no hay actividad");
    CheckEq(srp::FormatMinutesOrDash(30), "30 min", "actividad presente");
}

// --- Validacion y analisis de perfiles ---
void TestProfileInput() {
    Section("Identificacion de perfiles");
    Check(srp::IsValidSteamId64("76561198000000001"), "SteamID64 valido");
    Check(!srp::IsValidSteamId64("12345"), "SteamID64 demasiado corto");
    Check(!srp::IsValidSteamId64("86561198000000001"), "SteamID64 con prefijo invalido");
    Check(!srp::IsValidSteamId64("7656119800000000a"), "SteamID64 con letras");

    std::string value;
    Check(srp::ParseProfileInput("76561198000000001", value) == srp::ProfileInputKind::SteamId64,
          "entrada directa SteamID64");
    CheckEq(value, "76561198000000001", "valor SteamID64");

    Check(srp::ParseProfileInput("https://steamcommunity.com/profiles/76561198000000001/", value) ==
              srp::ProfileInputKind::SteamId64,
          "URL con /profiles/");
    CheckEq(value, "76561198000000001", "valor desde URL profiles");

    Check(srp::ParseProfileInput("https://steamcommunity.com/id/gaben/", value) ==
              srp::ProfileInputKind::Vanity,
          "URL con /id/");
    CheckEq(value, "gaben", "valor vanity desde URL");

    Check(srp::ParseProfileInput("gaben", value) == srp::ProfileInputKind::Vanity,
          "vanity escrito a mano");
    CheckEq(value, "gaben", "valor vanity directo");

    Check(srp::ParseProfileInput("   ", value) == srp::ProfileInputKind::Empty, "entrada vacia");
    Check(srp::ParseProfileInput("http://otro-sitio.com/perfil/raro", value) ==
              srp::ProfileInputKind::Invalid,
          "URL no reconocida");
}

// --- Clasificacion de actividad ---
void TestClassify() {
    Section("Clasificacion de actividad reciente");
    Check(srp::ClassifyActivity(0, 120, 480) == srp::ActivityLevel::None, "sin actividad");
    Check(srp::ClassifyActivity(30, 120, 480) == srp::ActivityLevel::Low, "actividad baja");
    Check(srp::ClassifyActivity(120, 120, 480) == srp::ActivityLevel::Moderate,
          "umbral moderado inclusive");
    Check(srp::ClassifyActivity(479, 120, 480) == srp::ActivityLevel::Moderate, "casi alto");
    Check(srp::ClassifyActivity(480, 120, 480) == srp::ActivityLevel::High,
          "umbral alto inclusive");
    CheckEq(srp::ActivityLevelLabel(srp::ActivityLevel::High), "Alta", "etiqueta alta");
    CheckEq(srp::ActivityLevelLabel(srp::ActivityLevel::None), "Sin actividad",
            "etiqueta sin actividad");
}

// --- Analisis de la respuesta de juegos recientes ---
void TestRecentlyPlayedParsing() {
    Section("Analisis de GetRecentlyPlayedGames");
    {
        const std::string body = R"({
            "response": {
                "total_count": 3,
                "games": [
                    {"appid": 570, "name": "Dota 2", "playtime_2weeks": 120, "playtime_forever": 9000},
                    {"appid": 730, "name": "Counter-Strike 2", "playtime_2weeks": 600, "playtime_forever": 30000},
                    {"appid": 400, "name": "Portal", "playtime_2weeks": 30}
                ]
            }
        })";
        srp::ProfileActivity activity;
        std::string message;
        const srp::FetchStatus status = srp::steam::ParseRecentlyPlayedJson(body, activity, message);
        Check(status == srp::FetchStatus::Ok, "estado Ok con juegos");
        CheckEqInt(static_cast<long long>(activity.games.size()), 3, "numero de juegos");
        // Ordenado por minutos recientes descendente.
        CheckEqInt(activity.games[0].appid, 730, "primer juego ordenado por tiempo reciente");
        CheckEqInt(activity.games[1].appid, 570, "segundo juego");
        CheckEqInt(activity.games[2].appid, 400, "tercer juego");
        Check(!activity.games[2].hasPlaytimeForever, "playtime_forever ausente marcado");
        Check(activity.games[0].hasPlaytimeForever, "playtime_forever presente");
    }
    {
        // Perfil privado o sin actividad: respuesta vacia.
        const std::string body = R"({"response":{}})";
        srp::ProfileActivity activity;
        std::string message;
        const srp::FetchStatus status = srp::steam::ParseRecentlyPlayedJson(body, activity, message);
        Check(status == srp::FetchStatus::NoRecentGames, "respuesta vacia -> sin juegos recientes");
        Check(!message.empty(), "mensaje informativo presente");
        Check(activity.games.empty(), "sin juegos en la lista");
    }
    {
        // Lista vacia explicita.
        const std::string body = R"({"response":{"total_count":0,"games":[]}})";
        srp::ProfileActivity activity;
        std::string message;
        Check(srp::steam::ParseRecentlyPlayedJson(body, activity, message) ==
                  srp::FetchStatus::NoRecentGames,
              "lista vacia -> sin juegos recientes");
    }
    {
        // JSON invalido.
        srp::ProfileActivity activity;
        std::string message;
        Check(srp::steam::ParseRecentlyPlayedJson("{no es json", activity, message) ==
                  srp::FetchStatus::NetworkError,
              "JSON invalido -> error");
    }
    {
        // Respuesta sin bloque response.
        srp::ProfileActivity activity;
        std::string message;
        Check(srp::steam::ParseRecentlyPlayedJson(R"({"otra":"cosa"})", activity, message) ==
                  srp::FetchStatus::PrivateOrUnavailable,
              "respuesta sin datos -> restringido");
    }
}

// --- Analisis de resumen de jugador ---
void TestPlayerSummariesParsing() {
    Section("Analisis de GetPlayerSummaries");
    const std::string body = R"({
        "response": {
            "players": [
                {"steamid": "76561198000000001", "personaname": "Ana",
                 "avatarfull": "https://avatars.example/ana_full.jpg"}
            ]
        }
    })";
    srp::Profile profile;
    std::string message;
    const srp::FetchStatus status = srp::steam::ParsePlayerSummariesJson(body, profile, message);
    Check(status == srp::FetchStatus::Ok, "estado Ok");
    CheckEq(profile.personaName, "Ana", "nombre del jugador");
    CheckEq(profile.avatarUrl, "https://avatars.example/ana_full.jpg", "avatar");

    const std::string empty = R"({"response":{"players":[]}})";
    srp::Profile missing;
    Check(srp::steam::ParsePlayerSummariesJson(empty, missing, message) ==
              srp::FetchStatus::InvalidProfile,
          "sin jugadores -> perfil no valido");
}

// --- Resolucion de vanity URL ---
void TestResolveVanityParsing() {
    Section("Analisis de ResolveVanityURL");
    std::string steamid;
    std::string message;
    const std::string ok = R"({"response":{"steamid":"76561198000000001","success":1}})";
    Check(srp::steam::ParseResolveVanityJson(ok, steamid, message) == srp::FetchStatus::Ok,
          "resolucion correcta");
    CheckEq(steamid, "76561198000000001", "steamid resuelto");

    steamid.clear();
    const std::string notFound = R"({"response":{"success":42,"message":"No match"}})";
    Check(srp::steam::ParseResolveVanityJson(notFound, steamid, message) ==
              srp::FetchStatus::InvalidProfile,
          "vanity sin coincidencia -> perfil no valido");
}

// --- Ordenacion por actividad reciente ---
void TestSorting() {
    Section("Ordenacion por actividad reciente");
    std::vector<srp::Game> games = {
        {0, "Juego A", 10, 100, true},
        {0, "Juego B", 500, 900, true},
        {0, "Juego C", 100, 200, true},
    };
    std::sort(games.begin(), games.end(), [](const srp::Game& a, const srp::Game& b) {
        if (a.playtime2Weeks != b.playtime2Weeks) return a.playtime2Weeks > b.playtime2Weeks;
        return a.playtimeForever > b.playtimeForever;
    });
    CheckEqInt(games[0].playtime2Weeks, 500, "mayor actividad primero");
    CheckEqInt(games[1].playtime2Weeks, 100, "actividad intermedia en medio");
    CheckEqInt(games[2].playtime2Weeks, 10, "menor actividad al final");
}

// --- Configuracion: ida y vuelta y saneado ---
void TestConfig() {
    Section("Configuracion (JSON puro)");
    srp::AppConfig config = srp::DefaultConfig();
    srp::Profile profile;
    profile.steamid64 = "76561198000000001";
    profile.personaName = "Ana";
    profile.avatarUrl = "https://avatars.example/ana.jpg";
    config.profiles.push_back(profile);
    config.prefs.autoRefreshEnabled = true;
    config.prefs.autoRefreshMinutes = 45;
    config.prefs.highActivityMinutes = 600;

    const std::string json = srp::ConfigToJson(config);
    srp::AppConfig loaded;
    std::string error;
    Check(srp::ConfigFromJson(json, loaded, error), "lectura correcta del JSON");
    CheckEqInt(static_cast<long long>(loaded.profiles.size()), 1, "un perfil recuperado");
    CheckEq(loaded.profiles[0].personaName, "Ana", "nombre recuperado");
    Check(loaded.prefs.autoRefreshEnabled, "auto-refresh recuperado");
    CheckEqInt(loaded.prefs.autoRefreshMinutes, 45, "intervalo recuperado");

    // La clave de API no debe aparecer nunca en el JSON de configuracion.
    Check(json.find("api") == std::string::npos || json.find("\"apiKey\"") == std::string::npos,
          "no se guardan claves en la configuracion");

    // Perfil propio (para importar amigos) se conserva.
    srp::AppConfig withOwn = srp::DefaultConfig();
    withOwn.ownSteamId64 = "76561198000000009";
    withOwn.ownPersonaName = "Yo";
    srp::AppConfig ownLoaded;
    Check(srp::ConfigFromJson(srp::ConfigToJson(withOwn), ownLoaded, error),
          "config con perfil propio leida");
    CheckEq(ownLoaded.ownSteamId64, "76561198000000009", "perfil propio recuperado");
    CheckEq(ownLoaded.ownPersonaName, "Yo", "nombre propio recuperado");
    srp::AppConfig badOwn = srp::DefaultConfig();
    badOwn.ownSteamId64 = "malo";
    badOwn.ownPersonaName = "X";
    srp::SanitizeConfig(badOwn);
    Check(badOwn.ownSteamId64.empty(), "perfil propio invalido descartado");
    Check(badOwn.ownPersonaName.empty(), "nombre propio se limpia con el perfil invalido");

    // Saneado: perfil invalido descartado y umbrales corrigidos.
    srp::AppConfig bad = srp::DefaultConfig();
    srp::Profile invalid;
    invalid.steamid64 = "no-valido";
    bad.profiles.push_back(invalid);
    bad.prefs.moderateActivityMinutes = 900;
    bad.prefs.highActivityMinutes = 200;
    const int dropped = srp::SanitizeConfig(bad);
    CheckEqInt(dropped, 1, "perfil invalido descartado");
    Check(bad.prefs.moderateActivityMinutes < bad.prefs.highActivityMinutes,
          "umbrales reordenados");

    // JSON corrupto.
    srp::AppConfig out;
    Check(!srp::ConfigFromJson("{roto", out, error), "JSON corrupto rechazado");
    Check(!error.empty(), "descripcion del error presente");
}

// --- Cache: ida y vuelta ---
void TestCache() {
    Section("Cache de actividad (JSON puro)");
    srp::ProfileActivity activity;
    activity.steamid64 = "76561198000000002";
    activity.personaName = "Bruno";
    activity.status = srp::FetchStatus::Ok;
    activity.statusMessage = "Consulta correcta.";
    activity.fetchedAtUnix = 1700000000;
    activity.games.push_back({730, "Counter-Strike 2", 120, 5000, true});

    const std::string json = srp::cache::ActivityToJson(activity);
    srp::ProfileActivity loaded;
    std::string error;
    Check(srp::cache::ActivityFromJson(json, loaded, error), "lectura de cache correcta");
    CheckEq(loaded.steamid64, "76561198000000002", "steamid de cache");
    CheckEqInt(static_cast<long long>(loaded.games.size()), 1, "juego de cache");
    CheckEq(loaded.games[0].name, "Counter-Strike 2", "nombre de juego de cache");
    Check(loaded.status == srp::FetchStatus::Ok, "estado de cache");

    srp::ProfileActivity bad;
    Check(!srp::cache::ActivityFromJson("[]", bad, error), "cache con formato incorrecto rechazada");
}

// --- Datos de demostracion ---
void TestDemoData() {
    Section("Modo demostracion");
    const std::vector<srp::Profile> profiles = srp::demo::DemoProfiles();
    CheckEqInt(static_cast<long long>(profiles.size()), 3, "tres perfiles de demostracion");
    srp::ProfileActivity activity;
    Check(srp::demo::DemoActivityFor(profiles[0].steamid64, activity), "actividad demo disponible");
    Check(!activity.games.empty(), "juegos demo presentes");
    Check(activity.games[0].playtime2Weeks >= activity.games.back().playtime2Weeks,
          "juegos demo ordenados");
    Check(srp::demo::IsDemoProfile(profiles[1].steamid64), "perfil demo reconocido");
    Check(!srp::demo::IsDemoProfile("76561198000000001"), "perfil real no es demo");
}

// --- Lista de amigos e importacion por lotes ---
void TestFriendParsing() {
    Section("Analisis de GetFriendList y resumenes por lotes");
    const std::string friends = R"({
        "friendslist": {
            "friends": [
                {"steamid": "76561198000000002", "relationship": "friend", "friend_since": 1500000000},
                {"steamid": "76561198000000003", "relationship": "friend", "friend_since": 1600000000},
                {"steamid": "no-valido", "relationship": "friend", "friend_since": 1}
            ]
        }
    })";
    std::vector<std::string> ids;
    std::string message;
    Check(srp::steam::ParseFriendListJson(friends, ids, message) == srp::FetchStatus::Ok,
          "lista de amigos analizada");
    CheckEqInt(static_cast<long long>(ids.size()), 2, "solo identificadores validos conservados");
    CheckEq(ids[0], "76561198000000002", "primer amigo");

    std::vector<std::string> none;
    Check(srp::steam::ParseFriendListJson("{\"response\":{}}", none, message) ==
              srp::FetchStatus::PrivateOrUnavailable,
          "lista de amigos privada detectada");
    Check(!message.empty(), "mensaje de lista privada presente");

    const std::string summaries = R"({
        "response": {
            "players": [
                {"steamid": "76561198000000002", "personaname": "Bruno", "avatarfull": "https://a/b.jpg"},
                {"steamid": "76561198000000003", "personaname": "Carla", "avatarmedium": "https://a/c.jpg"},
                {"steamid": "raro"}
            ]
        }
    })";
    std::vector<srp::Profile> players;
    Check(srp::steam::ParsePlayerSummariesListJson(summaries, players, message) ==
              srp::FetchStatus::Ok,
          "resumenes por lotes analizados");
    CheckEqInt(static_cast<long long>(players.size()), 2, "solo jugadores validos");
    CheckEq(players[1].personaName, "Carla", "nombre del segundo jugador");
    CheckEq(players[1].avatarUrl, "https://a/c.jpg", "avatar medium como respaldo");
}

// --- Deteccion de la cuenta local de Steam ---
void TestLocalSteam() {
    Section("Deteccion local de la cuenta de Steam");
    const std::string vdf = R"("users"
{
    "76561198000000001"
    {
        "AccountName"       "ana"
        "PersonaName"       "Ana"
        "RememberPassword"  "1"
        "MostRecent"        "0"
        "Timestamp"         "1600000000"
    }
    "76561198000000002"
    {
        "AccountName"       "bruno"
        "PersonaName"       "Bruno"
        "RememberPassword"  "1"
        "MostRecent"        "1"
        "Timestamp"         "1700000000"
    }
})";
    srp::steam::LocalAccount account;
    Check(srp::steam::ParseLoginUsersVdf(vdf, account), "loginusers.vdf analizado");
    CheckEq(account.steamid64, "76561198000000002", "cuenta mas reciente elegida");
    CheckEq(account.personaName, "Bruno", "nombre de la cuenta reciente");
    Check(account.mostRecent, "marca MostRecent respetada");

    // Sin MostRecent: se elige la marca de tiempo mas alta.
    const std::string noRecent = R"("users"
{
    "76561198000000001" { "Timestamp" "100" }
    "76561198000000009" { "Timestamp" "900" }
})";
    srp::steam::LocalAccount second;
    Check(srp::steam::ParseLoginUsersVdf(noRecent, second), "sin MostRecent tambien se analiza");
    CheckEq(second.steamid64, "76561198000000009", "se elige la marca de tiempo mayor");

    srp::steam::LocalAccount empty;
    Check(!srp::steam::ParseLoginUsersVdf("basura sin datos", empty),
          "contenido no valido rechazado");

    // Formato real de Steam: claves en minusculas y sin MostRecent.
    const std::string modern = R"("users"
{
    "76561198084172465"
    {
        "AccountName"       "megafronnyyt"
        "PersonaName"       "Diamantoso"
        "RememberPassword"  "1"
        "AutoLogin"         "1"
        "timestamp"         "1792159688"
    }
})";
    srp::steam::LocalAccount modernAccount;
    Check(srp::steam::ParseLoginUsersVdf(modern, modernAccount),
          "formato moderno (minusculas) analizado");
    CheckEq(modernAccount.steamid64, "76561198084172465", "steamid del formato moderno");
    CheckEq(modernAccount.personaName, "Diamantoso", "nombre del formato moderno");
    CheckEq(modernAccount.accountName, "megafronnyyt", "cuenta del formato moderno");
}

// --- URLs de la API ---
void TestUrls() {
    Section("Construccion de URLs");
    const std::string url = srp::steam::RecentlyPlayedUrl("CLAVE", "76561198000000001");
    Check(url.rfind("https://api.steampowered.com/IPlayerService/GetRecentlyPlayedGames/v1/", 0) == 0,
          "endpoint de juegos recientes");
    Check(url.find("steamid=76561198000000001") != std::string::npos, "steamid incluido");
    Check(srp::steam::CoverUrl(570) ==
              "https://cdn.cloudflare.steamstatic.com/steam/apps/570/header.jpg",
          "URL de portada");
    Check(srp::steam::StorePageUrl(570) == "https://store.steampowered.com/app/570",
          "URL de la tienda");
    const std::string vanity = srp::steam::ResolveVanityUrl("CLAVE", "gaben");
    Check(vanity.find("ResolveVanityURL/v1/") != std::string::npos, "endpoint vanity");
    Check(vanity.find("vanityurl=gaben") != std::string::npos, "vanity incluido");

    const std::string friends = srp::steam::FriendListUrl("CLAVE", "76561198000000001");
    Check(friends.find("GetFriendList/v1/") != std::string::npos, "endpoint de amigos");
    Check(friends.find("relationship=friend") != std::string::npos, "relacion friend");
    const std::string batch =
        srp::steam::PlayerSummariesUrl("CLAVE",
                                       std::vector<std::string>{"76561198000000001",
                                                                "76561198000000002"});
    Check(batch.find("steamids=76561198000000001,76561198000000002") != std::string::npos,
          "resumenes por lotes separados por coma");
}

// --- Grupo familiar de Steam (leido del cliente local) ---
void TestLocalFamilyGroup() {
    Section("Grupo familiar del cliente de Steam");

    // Recorte realista de localconfig.vdf con un bloque FamilyGroup.
    const std::string vdf = R"("UserLocalConfigStore"
{
	"Software"
	{
		"Valve"
		{
			"Steam"
			{
				"RecentGame"		"730"
			}
		}
	}
	"FamilyGroup"
	{
		"version"		"1"
		"groupid"		"7678462"
		"name"		"TheFamily"
		"country"		""
		"role"		"1"
		"members"
		{
			"0"
			{
				"accountid"		"266486772"
				"role"		"1"
			}
			"1"
			{
				"accountid"		"123906737"
				"role"		"1"
			}
			"2"
			{
				"accountid"		"0"
				"role"		"1"
			}
			"3"
			{
				"role"		"1"
			}
		}
	}
	"nettickets"
	{
		"304930"		"0100"
	}
})";

    srp::steam::LocalFamilyGroup group;
    Check(srp::steam::ParseLocalFamilyGroupVdf(vdf, group), "bloque FamilyGroup analizado");
    CheckEq(group.groupId, "7678462", "identificador del grupo familiar");
    CheckEq(group.name, "TheFamily", "nombre del grupo familiar");
    CheckEqInt(static_cast<long long>(group.memberSteamIds.size()), 2,
               "solo cuentan los miembros con accountid valido");
    // accountid 266486772 -> SteamID64 = base + accountid
    CheckEq(group.memberSteamIds[0], "76561198226752500", "conversion de accountid a SteamID64");
    // La cuenta propia ya detectada: 123906737 -> 76561198084172465
    CheckEq(group.memberSteamIds[1], "76561198084172465", "conversion de la cuenta propia");

    srp::steam::LocalFamilyGroup none;
    Check(!srp::steam::ParseLocalFamilyGroupVdf(
              "\"UserLocalConfigStore\"\n{\n\t\"Software\"\n\t{\n\t}\n}", none),
          "sin bloque FamilyGroup no hay grupo");

    srp::steam::LocalFamilyGroup bad;
    Check(!srp::steam::ParseLocalFamilyGroupVdf("esto no es un vdf", bad),
          "contenido no valido no produce grupo");

    const std::filesystem::path path =
        srp::steam::SteamLocalConfigPath("C:\\Steam", "123906737");
    CheckEq(path.filename().string(), "localconfig.vdf", "nombre del archivo de configuracion local");
    Check(path.string().find("userdata") != std::string::npos, "ruta dentro de userdata");
}

}  // namespace

int main() {
    std::printf("What Are They Actually Playing? - pruebas de la logica no visual\n\n");

    TestFormatMinutes();
    TestProfileInput();
    TestClassify();
    TestRecentlyPlayedParsing();
    TestPlayerSummariesParsing();
    TestResolveVanityParsing();
    TestSorting();
    TestConfig();
    TestCache();
    TestDemoData();
    TestFriendParsing();
    TestLocalFamilyGroup();
    TestLocalSteam();
    TestUrls();

    std::printf("\n%d comprobaciones, %d fallos\n", g_checks, g_failures);
    if (g_failures == 0) {
        std::printf("TODAS LAS PRUEBAS HAN PASADO\n");
    }
    return g_failures == 0 ? 0 : 1;
}
