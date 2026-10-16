#include "steam/demo_data.h"

#include <algorithm>

#include "common/format.h"

namespace srp::demo {
namespace {

struct DemoGame {
    uint32_t appid;
    const char* name;
    int recent;
    int forever;
};

struct DemoProfile {
    const char* steamid64;
    const char* persona;
    std::vector<DemoGame> games;
    FetchStatus status;
    const char* message;
};

const std::vector<DemoProfile>& Table() {
    static const std::vector<DemoProfile> table = {
        {"76561190000000001", "DEMO - Ana",
         {
             {570, "Dota 2 (DEMO)", 620, 41230},
             {413150, "Stardew Valley (DEMO)", 340, 8120},
             {892970, "Valheim (DEMO)", 90, 2450},
         },
         FetchStatus::Ok, "Consulta de demostracion."},
        {"76561190000000002", "DEMO - Bruno",
         {
             {730, "Counter-Strike 2 (DEMO)", 780, 98760},
             {570, "Dota 2 (DEMO)", 210, 33210},
             {1245620, "ELDEN RING (DEMO)", 55, 5120},
         },
         FetchStatus::Ok, "Consulta de demostracion."},
        {"76561190000000003", "DEMO - Carla",
         {
             {413150, "Stardew Valley (DEMO)", 480, 15400},
             {620, "Portal 2 (DEMO)", 45, 980},
         },
         FetchStatus::Ok, "Consulta de demostracion."},
    };
    return table;
}

}  // namespace

std::vector<Profile> DemoProfiles() {
    std::vector<Profile> profiles;
    for (const DemoProfile& d : Table()) {
        Profile p;
        p.steamid64 = d.steamid64;
        p.personaName = d.persona;
        p.avatarUrl.clear();
        profiles.push_back(std::move(p));
    }
    return profiles;
}

bool DemoActivityFor(const std::string& steamid64, ProfileActivity& out) {
    for (const DemoProfile& d : Table()) {
        if (steamid64 == d.steamid64) {
            out = ProfileActivity{};
            out.steamid64 = d.steamid64;
            out.personaName = d.persona;
            out.status = FetchStatus::Ok;
            out.statusMessage = d.message;
            out.fetchedAtUnix = UnixNow();
            out.fromCache = false;
            for (const DemoGame& g : d.games) {
                Game game;
                game.appid = g.appid;
                game.name = g.name;
                game.playtime2Weeks = g.recent;
                game.playtimeForever = g.forever;
                game.hasPlaytimeForever = true;
                out.games.push_back(std::move(game));
            }
            std::stable_sort(out.games.begin(), out.games.end(), [](const Game& a, const Game& b) {
                return a.playtime2Weeks > b.playtime2Weeks;
            });
            return true;
        }
    }
    return false;
}

bool IsDemoProfile(const std::string& steamid64) {
    for (const DemoProfile& d : Table()) {
        if (steamid64 == d.steamid64) return true;
    }
    return false;
}

}  // namespace srp::demo
