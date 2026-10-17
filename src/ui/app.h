// Ventana principal: lista de integrantes, tarjetas de juegos recientes y vista general.
#pragma once

#include <windows.h>

#include <atomic>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "common/models.h"
#include "ui/image_cache.h"
#include "ui/theme.h"
#include "ui/worker.h"

namespace srp::ui {

// Mensajes internos entre el hilo de trabajo y la interfaz.
constexpr UINT WM_SRP_ACTIVITY = WM_APP + 1;   // lParam: ActivityResult*
constexpr UINT WM_SRP_IMAGE_OK = WM_APP + 2;   // lParam: std::string* (clave)
constexpr UINT WM_SRP_IMAGE_FAIL = WM_APP + 3; // lParam: std::string* (clave)
constexpr UINT WM_SRP_VANITY = WM_APP + 4;     // lParam: VanityResult*
constexpr UINT WM_SRP_FRIENDS = WM_APP + 5;    // lParam: FriendsResult*

struct ActivityResult {
    ProfileActivity activity;
    Profile profile;
    FetchStatus status = FetchStatus::NotFetched;
    std::string message;
    bool savedToCache = false;
};

struct VanityResult {
    std::string vanity;
    std::string steamid64;
    bool ok = false;
    std::string message;
};

// Resultado de la importacion de integrantes (amigos o grupo familiar) desde el
// hilo de trabajo.
struct FriendsResult {
    std::vector<Profile> friends;
    bool ok = false;
    std::string message;
    std::string ownSteamId;
    std::string ownPersonaName;
    std::string sourceLabel;   // "tu grupo familiar" o "tus amigos"
    std::string groupName;     // nombre del grupo familiar, si lo hay
};

class MainWindow {
public:
    MainWindow() = default;
    ~MainWindow();

    static bool RegisterWindowClass(HINSTANCE instance);
    bool Create(HINSTANCE instance, int showCommand);

    HWND Handle() const { return hwnd_; }

private:
    // Tipos de zona interactiva.
    enum class Hit {
        None,
        Title,
        RefreshSelected,
        RefreshAll,
        Settings,
        ApiKey,
        Demo,
        AddProfile,
        EditProfile,
        RemoveProfile,
        ImportFamily,
        ImportFriends,
        TabGames,
        TabOverview,
        HideLow,
        ClearSearch,
        ProfileRow,
        GameCard,
        GeneralCard,
        CardOpen,
        ProfileOpen,
        SidebarTrack,
        ContentTrack
    };

    struct HitRegion {
        RECT rect{};
        Hit kind = Hit::None;
        std::string param;  // steamid64 o appid
        int index = 0;
    };

    // Entrada de contenido de la vista principal: cabecera de miembro, tarjeta de juego o general.
    struct ContentEntry {
        enum class Type { MemberHeader, Game, General };
        Type type = Type::Game;
        RECT rect{};
        Game game;                     // Type::Game
        std::string ownerId;           // Type::Game
        std::string ownerName;         // Type::Game
        bool showOthers = false;       // Type::Game
        std::string headerSubtitle;    // Type::MemberHeader
        int headerLevel = 0;           // Type::MemberHeader
        std::string title;             // Type::General
        int totalRecent = 0;           // Type::General
        std::vector<std::pair<std::string, int>> members;  // Type::General (nombre, minutos)
    };

    static LRESULT CALLBACK WndProcStatic(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT WndProc(UINT msg, WPARAM wParam, LPARAM lParam);

    // Ciclo de vida
    bool OnCreate();
    void OnDestroy();
    void OnPaint();
    void OnSize();
    void OnDpiChanged(WPARAM wParam, const RECT* suggested);
    void OnTimer(WPARAM timerId);

    // Entrada
    void OnMouseMove(POINT pt);
    void OnMouseLeave();
    bool OnLeftButtonDown(POINT pt);
    void OnLeftButtonUp(POINT pt);
    void OnMouseWheel(POINT pt, int delta);
    void OnKeyDown(WPARAM key);
    void OnCommand(WPARAM wParam, LPARAM lParam);
    void OnSetCursor(POINT pt);

    // Acciones
    void LoadState();
    void SaveConfigNow();
    void RebuildCombos();
    void RefreshProfiles(bool all);
    void AddProfileDialog();
    // Importa integrantes: grupo familiar de Steam (principal) o lista de amigos.
    void ImportMembers(bool family);
    void EnsureOwnSteamId(bool family);  // detecta/valida el perfil propio y lanza la importacion
    void StartMemberImport(const std::string& ownSteamId, bool family);
    void ApplyImportedFriends(std::vector<Profile> friends, const std::string& sourceLabel);
    void EditSelectedProfile();
    void RemoveSelectedProfile();
    void OpenSettings();
    void OpenApiKeyDialog();
    void OpenAbout();
    void HandleHit(const HitRegion& region);
    bool OpenUrl(const std::string& url) const;

    // Estado de datos y vistas
    void ReloadProfiles();
    void SetDemoMode(bool enabled);
    void UpdateProfileMeta(const std::string& steamid64, const std::string& name,
                           const std::string& avatarUrl);
    void AddProfileById(const std::string& steamid64, const std::string& alias);
    bool MatchesSearch(const std::string& name) const;
    const std::vector<Profile>& CurrentProfiles() const { return displayProfiles_; }

    // Trabajo en segundo plano
    void RequestActivity(const Profile& profile);
    void RequestImage(const std::string& key, const std::string& url);
    void EnsureImage(const std::string& key, const std::string& url);

    // Datos derivados
    const Profile* FindProfile(const std::string& steamid64) const;
    const ProfileActivity* FindActivity(const std::string& steamid64) const;
    int OthersRecentMinutes(uint32_t appid, const std::string& excludeSteamid) const;
    std::string DisplayName(const Profile& profile) const;
    std::string ProfileUrl(const std::string& steamid64) const;

    // Disposicion y dibujo
    void ComputeLayout();
    void LayoutLists();
    void BuildContent();
    void ClampScroll();
    void PaintAll(Gdiplus::Graphics& g);
    void DrawHeader(Gdiplus::Graphics& g);
    void DrawToolbar(Gdiplus::Graphics& g);
    void DrawSidebar(Gdiplus::Graphics& g);
    void DrawContent(Gdiplus::Graphics& g);
    void DrawStatusBar(Gdiplus::Graphics& g);
    void DrawProfileRow(Gdiplus::Graphics& g, const RECT& rect, const Profile& profile,
                        const ProfileActivity* activity, bool selected, bool hovered);
    void DrawGameCard(Gdiplus::Graphics& g, const RECT& rect, const Game& game,
                      const std::string& ownerId, const std::string& ownerName, bool showOthers,
                      bool hovered);
    void DrawGeneralCard(Gdiplus::Graphics& g, const RECT& rect, uint32_t appid,
                         const std::string& name, int totalRecent,
                         const std::vector<std::pair<std::string, int>>& members, bool hovered);
    void DrawButton(Gdiplus::Graphics& g, const RECT& rect, const std::string& text, int style,
                    bool hovered, bool pressed, bool enabled = true);
    void DrawChip(Gdiplus::Graphics& g, const RECT& rect, const std::string& text, bool active,
                  bool hovered);
    void DrawBadge(Gdiplus::Graphics& g, const RECT& rect, const std::string& text,
                   const Gdiplus::Color& color);
    void DrawScrollbar(Gdiplus::Graphics& g, const RECT& area, int contentHeight, int scroll);
    void DrawEmptyState(Gdiplus::Graphics& g, const RECT& area, const std::string& title,
                        const std::string& subtitle);

    int AddHit(Hit kind, const RECT& rect, const std::string& param = {}, int index = 0);
    std::vector<Game> BuildGameList(const ProfileActivity& activity,
                                    const std::string& ownerSteamId) const;
    void FlushConfig();
    void ApplyAutoRefreshTimer();

    HWND hwnd_ = nullptr;
    HINSTANCE instance_ = nullptr;
    UINT dpi_ = 96;

    Theme theme_;
    ImageCache images_;
    Worker worker_;

    AppConfig config_;
    std::string apiKey_;
    bool hasApiKey_ = false;
    bool demoMode_ = false;
    bool cacheUsed_ = false;

    std::map<std::string, ProfileActivity> activities_;
    std::set<std::string> pendingFetch_;
    std::map<std::string, std::vector<Game>> sortedCache_;
    std::vector<Profile> displayProfiles_;
    std::vector<ContentEntry> contentEntries_;
    bool pendingVanity_ = false;
    bool pendingVanityIsOwn_ = false;
    bool pendingOwnImportFamily_ = true;
    std::string pendingVanityAlias_;
    bool pendingFriends_ = false;
    bool rebuildingCombos_ = false;
    bool configDirty_ = false;
    std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);

    // Estado de la vista
    std::string selectedProfile_;
    std::string searchQuery_;
    bool generalView_ = false;
    int sortMode_ = 0;
    int sidebarScroll_ = 0;
    int contentScroll_ = 0;
    int sidebarContentHeight_ = 0;
    int contentContentHeight_ = 0;
    int hoverIndex_ = -1;
    int pressedIndex_ = -1;
    bool draggingSidebar_ = false;
    bool draggingContent_ = false;

    // Guino oculto: 10 clics seguidos sobre el titulo cambian el texto.
    int titleClickCount_ = 0;
    ULONGLONG titleLastClickTick_ = 0;
    bool titleEasterEgg_ = false;

    int64_t lastRefreshUnix_ = 0;
    std::string statusMessage_;
    bool refreshing_ = false;

    // Controles nativos
    HWND searchEdit_ = nullptr;    HWND filterCombo_ = nullptr;
    HWND sortCombo_ = nullptr;
    HFONT uiFont_ = nullptr;
    HBRUSH editBrush_ = nullptr;
    HBRUSH comboBrush_ = nullptr;

    // Regiones interactivas calculadas en el ultimo dibujo
    std::vector<HitRegion> hits_;
    std::vector<RECT> profileRowRects_;
    std::vector<std::string> profileRowIds_;
    int comboFilterCount_ = 0;

    // Disposicion
    RECT rcClient_{};
    RECT rcHeader_{};
    RECT rcToolbar_{};
    RECT rcSidebar_{};
    RECT rcContent_{};
    RECT rcStatus_{};
    RECT rcSidebarList_{};
    RECT rcContentList_{};
    RECT rcBtnRefresh_{};
    RECT rcBtnRefreshAll_{};
    RECT rcBtnSettings_{};
    RECT rcBtnApiKey_{};
    RECT rcBtnDemo_{};
    RECT rcBtnAdd_{};
    RECT rcBtnEdit_{};
    RECT rcBtnRemove_{};
    RECT rcBtnImportFamily_{};
    RECT rcBtnImportFriends_{};
    RECT rcTabGames_{};
    RECT rcTabOverview_{};
    RECT rcHideLow_{};
    RECT rcSearch_{};
    RECT rcFilter_{};
    RECT rcSort_{};
    RECT rcSearchClear_{};
    RECT rcSidebarTrack_{};
    RECT rcContentTrack_{};
    RECT rcTitle_{};
};

}  // namespace srp::ui
