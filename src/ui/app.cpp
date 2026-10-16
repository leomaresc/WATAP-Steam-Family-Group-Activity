// Ventana principal de Steam Recent Play.
//
// Toda la interfaz se dibuja a mano sobre GDI+ (salvo el campo de busqueda y los
// desplegables, que son controles nativos oscurecidos). La red y las imagenes se
// gestionan en un unico hilo de trabajo; los resultados llegan por mensajes.
#include "ui/app.h"

#include <commctrl.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <windows.h>
#include <windowsx.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "common/format.h"
#include "common/paths.h"
#include "resource.h"
#include "steam/demo_data.h"
#include "steam/http.h"
#include "steam/steam_api.h"
#include "storage/cache.h"
#include "storage/config.h"
#include "storage/credentials.h"
#include "ui/darkmode.h"
#include "ui/dialogs.h"
#include "ui/gfx.h"

namespace srp::ui {
namespace {

constexpr wchar_t kClassName[] = L"SteamRecentPlayMainWindow";

constexpr int kSearchId = 2001;
constexpr int kFilterId = 2002;
constexpr int kSortId = 2003;

constexpr UINT_PTR kTimerAutoRefresh = 1;
constexpr UINT_PTR kTimerSave = 2;

// Metricas de diseno (a 96 ppp; el tema las escala segun el DPI real).
constexpr int kHeaderH = 58;
constexpr int kToolbarH = 46;
constexpr int kStatusH = 28;
constexpr int kSidebarW = 272;
constexpr int kFilterBarH = 52;
constexpr int kBtnH = 30;
constexpr int kRowH = 62;
constexpr int kCardH = 92;
constexpr int kGeneralCardH = 112;
constexpr int kMemberHeaderH = 34;

bool PointIn(const RECT& r, POINT p) {
    return p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom;
}

RECT OffsetRect(const RECT& r, int dx, int dy) {
    RECT out{r.left + dx, r.top + dy, r.right + dx, r.bottom + dy};
    return out;
}

bool Intersects(const RECT& a, const RECT& b) {
    return a.left < b.right && a.right > b.left && a.top < b.bottom && a.bottom > b.top;
}

std::string ToLowerAscii(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

std::string Initials(const std::string& name) {
    for (unsigned char c : name) {
        if (!std::isspace(c)) {
            return std::string(1, static_cast<char>(std::toupper(c)));
        }
    }
    return "?";
}

std::string ReadWindowText(HWND hwnd) {
    const int len = GetWindowTextLengthW(hwnd);
    if (len <= 0) return std::string();
    std::wstring buffer(static_cast<size_t>(len) + 1, L'\0');
    GetWindowTextW(hwnd, buffer.data(), static_cast<int>(buffer.size()));
    buffer.resize(static_cast<size_t>(len));
    return WideToUtf8(buffer);
}

}  // namespace

MainWindow::~MainWindow() {
    worker_.Stop();
    if (uiFont_ != nullptr) DeleteObject(uiFont_);
    if (editBrush_ != nullptr) DeleteObject(editBrush_);
    if (comboBrush_ != nullptr) DeleteObject(comboBrush_);
}

bool MainWindow::RegisterWindowClass(HINSTANCE instance) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = &MainWindow::WndProcStatic;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APPICON));
    wc.hIconSm = wc.hIcon;
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kClassName;
    if (RegisterClassExW(&wc) == 0) {
        return GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    }
    return true;
}

bool MainWindow::Create(HINSTANCE instance, int showCommand) {
    instance_ = instance;
    hwnd_ = CreateWindowExW(0, kClassName, L"Steam Recent Play",
                            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT,
                            1200, 780, nullptr, nullptr, instance, this);
    if (hwnd_ == nullptr) return false;
    ShowWindow(hwnd_, showCommand);
    UpdateWindow(hwnd_);
    return true;
}

LRESULT CALLBACK MainWindow::WndProcStatic(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    MainWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<MainWindow*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    } else {
        self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self != nullptr) return self->WndProc(msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT MainWindow::WndProc(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            return OnCreate() ? 0 : -1;
        case WM_DESTROY:
            OnDestroy();
            return 0;
        case WM_PAINT:
            OnPaint();
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_SIZE:
            OnSize();
            return 0;
        case WM_DPICHANGED:
            OnDpiChanged(wParam, reinterpret_cast<const RECT*>(lParam));
            return 0;
        case WM_GETMINMAXINFO: {
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
            mmi->ptMinTrackSize.x = theme_.Px(940);
            mmi->ptMinTrackSize.y = theme_.Px(600);
            return 0;
        }
        case WM_TIMER:
            OnTimer(wParam);
            return 0;
        case WM_MOUSEMOVE: {
            POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            OnMouseMove(pt);
            return 0;
        }
        case WM_MOUSELEAVE:
            OnMouseLeave();
            return 0;
        case WM_LBUTTONDOWN: {
            POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            return OnLeftButtonDown(pt) ? 0 : 0;
        }
        case WM_LBUTTONUP: {
            POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            OnLeftButtonUp(pt);
            return 0;
        }
        case WM_MOUSEWHEEL: {
            POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ScreenToClient(hwnd_, &pt);
            OnMouseWheel(pt, GET_WHEEL_DELTA_WPARAM(wParam));
            return 0;
        }
        case WM_KEYDOWN:
            OnKeyDown(wParam);
            return 0;
        case WM_COMMAND:
            OnCommand(wParam, lParam);
            return 0;
        case WM_SETCURSOR: {
            if (LOWORD(lParam) != HTCLIENT) break;
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hwnd_, &pt);
            OnSetCursor(pt);
            return TRUE;
        }
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORSTATIC: {
            HDC dc = reinterpret_cast<HDC>(wParam);
            SetTextColor(dc, RGB(231, 233, 238));
            SetBkColor(dc, RGB(38, 40, 51));
            return reinterpret_cast<LRESULT>(editBrush_);
        }
        case WM_CTLCOLORLISTBOX: {
            HDC dc = reinterpret_cast<HDC>(wParam);
            SetTextColor(dc, RGB(231, 233, 238));
            SetBkColor(dc, RGB(38, 40, 51));
            return reinterpret_cast<LRESULT>(editBrush_);
        }
        case WM_SRP_ACTIVITY: {
            auto* result = reinterpret_cast<ActivityResult*>(lParam);
            if (result != nullptr) {
                pendingFetch_.erase(result->profile.steamid64);
                const bool ok = result->status == FetchStatus::Ok ||
                                result->status == FetchStatus::NoRecentGames;
                const std::string id = result->profile.steamid64;
                auto it = activities_.find(id);
                if (ok) {
                    activities_[id] = result->activity;
                    lastRefreshUnix_ = UnixNow();
                    UpdateProfileMeta(id, result->activity.personaName, result->activity.avatarUrl);
                } else {
                    if (it == activities_.end()) {
                        ProfileActivity placeholder;
                        placeholder.steamid64 = id;
                        placeholder.personaName = result->profile.personaName;
                        placeholder.avatarUrl = result->profile.avatarUrl;
                        placeholder.status = result->status;
                        placeholder.statusMessage = result->message;
                        activities_[id] = placeholder;
                    } else {
                        it->second.status = result->status;
                        it->second.statusMessage = result->message;
                        if (it->second.fetchedAtUnix > 0) it->second.fromCache = true;
                    }
                }
                statusMessage_ = result->message.empty() ? FetchStatusLabel(result->status)
                                                         : result->message;
                if (pendingFetch_.empty()) {
                    refreshing_ = false;
                    if (ok) {
                        statusMessage_ = "Actualizacion completada: " +
                                         std::to_string(displayProfiles_.size()) + " integrantes.";
                    }
                }
                delete result;
                LayoutLists();
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            return 0;
        }
        case WM_SRP_IMAGE_OK:
        case WM_SRP_IMAGE_FAIL: {
            auto* key = reinterpret_cast<std::string*>(lParam);
            if (key != nullptr) {
                images_.EndDownload(*key, msg == WM_SRP_IMAGE_OK);
                delete key;
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            return 0;
        }
        case WM_SRP_VANITY: {
            auto* result = reinterpret_cast<VanityResult*>(lParam);
            pendingVanity_ = false;
            if (result != nullptr) {
                if (result->ok && IsValidSteamId64(result->steamid64)) {
                    const std::string alias = pendingVanityAlias_;
                    pendingVanityAlias_.clear();
                    AddProfileById(result->steamid64, alias);
                } else {
                    statusMessage_ = result->message;
                    MessageBoxW(hwnd_, Widen(result->message).c_str(), L"Integrante",
                                MB_OK | MB_ICONWARNING);
                }
                delete result;
            }
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        }
        case WM_CLOSE:
            DestroyWindow(hwnd_);
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd_, msg, wParam, lParam);
}

// ---------------------------------------------------------------------------
// Ciclo de vida
// ---------------------------------------------------------------------------

bool MainWindow::OnCreate() {
    dpi_ = GetDpiForWindow(hwnd_);
    if (dpi_ == 0) dpi_ = 96;
    theme_.Init(static_cast<float>(dpi_));

    uiFont_ = CreateFontW(-MulDiv(9, static_cast<int>(dpi_), 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE,
                          FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                          CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    editBrush_ = CreateSolidBrush(RGB(38, 40, 51));
    comboBrush_ = CreateSolidBrush(RGB(33, 35, 44));

    searchEdit_ = CreateWindowExW(0, L"EDIT", L"",
                                  WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_TABSTOP, 0, 0, 10,
                                  10, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSearchId)),
                                  instance_, nullptr);
    filterCombo_ = CreateWindowExW(
        0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, 0, 0, 10, 10, hwnd_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kFilterId)), instance_, nullptr);
    sortCombo_ = CreateWindowExW(
        0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, 0, 0, 10, 10, hwnd_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSortId)), instance_, nullptr);

    for (HWND control : {searchEdit_, filterCombo_, sortCombo_}) {
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
        ApplyDarkModeToWindow(control);
    }
    SendMessageW(searchEdit_, EM_SETCUEBANNER, TRUE,
                 reinterpret_cast<LPARAM>(L"Buscar juego por nombre..."));

    ApplyDarkModeToWindow(hwnd_);

    LoadState();
    ComputeLayout();

    if (!demoMode_ && hasApiKey_ && !config_.profiles.empty()) {
        RefreshProfiles(true);
    }
    ApplyAutoRefreshTimer();
    return true;
}

void MainWindow::OnDestroy() {
    alive_->store(false);
    KillTimer(hwnd_, kTimerAutoRefresh);
    KillTimer(hwnd_, kTimerSave);
    FlushConfig();
    worker_.Stop();
    PostQuitMessage(0);
}

void MainWindow::OnPaint() {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd_, &ps);
    RECT rc;
    GetClientRect(hwnd_, &rc);
    const int width = std::max(1L, rc.right);
    const int height = std::max(1L, rc.bottom);

    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP bitmap = CreateCompatibleBitmap(hdc, width, height);
    HBITMAP oldBitmap = reinterpret_cast<HBITMAP>(SelectObject(memDC, bitmap));
    {
        Gdiplus::Graphics g(memDC);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);
        g.SetCompositingQuality(Gdiplus::CompositingQualityHighSpeed);
        PaintAll(g);
    }
    BitBlt(hdc, 0, 0, width, height, memDC, 0, 0, SRCCOPY);
    SelectObject(memDC, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(memDC);
    EndPaint(hwnd_, &ps);
}

void MainWindow::OnSize() {
    ComputeLayout();
    InvalidateRect(hwnd_, nullptr, TRUE);
}

void MainWindow::OnDpiChanged(WPARAM wParam, const RECT* suggested) {
    dpi_ = HIWORD(wParam);
    if (dpi_ == 0) dpi_ = 96;
    theme_.Init(static_cast<float>(dpi_));
    if (uiFont_ != nullptr) DeleteObject(uiFont_);
    uiFont_ = CreateFontW(-MulDiv(9, static_cast<int>(dpi_), 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE,
                          FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                          CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    for (HWND control : {searchEdit_, filterCombo_, sortCombo_}) {
        if (control != nullptr) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
    }
    if (suggested != nullptr) {
        SetWindowPos(hwnd_, nullptr, suggested->left, suggested->top,
                     suggested->right - suggested->left, suggested->bottom - suggested->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }
    ComputeLayout();
    InvalidateRect(hwnd_, nullptr, TRUE);
}

void MainWindow::OnTimer(WPARAM timerId) {
    if (timerId == kTimerSave) {
        KillTimer(hwnd_, kTimerSave);
        FlushConfig();
        return;
    }
    if (timerId == kTimerAutoRefresh && config_.prefs.autoRefreshEnabled && !demoMode_ &&
        hasApiKey_) {
        RefreshProfiles(true);
    }
}

// ---------------------------------------------------------------------------
// Entrada
// ---------------------------------------------------------------------------

void MainWindow::OnMouseMove(POINT pt) {
    if (draggingSidebar_ || draggingContent_) {
        const bool sidebar = draggingSidebar_;
        const RECT& area = sidebar ? rcSidebarList_ : rcContentList_;
        const int viewH = area.bottom - area.top;
        int contentH = sidebar ? sidebarContentHeight_ : contentContentHeight_;
        if (contentH > viewH) {
            double ratio = static_cast<double>(pt.y - area.top) / std::max(1, viewH);
            int newScroll = static_cast<int>(ratio * contentH);
            int maxScroll = contentH - viewH;
            newScroll = std::clamp(newScroll, 0, maxScroll);
            if (sidebar) sidebarScroll_ = newScroll;
            else contentScroll_ = newScroll;
            LayoutLists();
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return;
    }

    TRACKMOUSEEVENT track{};
    track.cbSize = sizeof(track);
    track.dwFlags = TME_LEAVE;
    track.hwndTrack = hwnd_;
    TrackMouseEvent(&track);

    int newHover = -1;
    for (int i = static_cast<int>(hits_.size()) - 1; i >= 0; --i) {
        if (PointIn(hits_[i].rect, pt)) {
            newHover = i;
            break;
        }
    }
    if (newHover != hoverIndex_) {
        hoverIndex_ = newHover;
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void MainWindow::OnMouseLeave() {
    if (hoverIndex_ != -1) {
        hoverIndex_ = -1;
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

bool MainWindow::OnLeftButtonDown(POINT pt) {
    SetFocus(hwnd_);
    pressedIndex_ = -1;
    for (int i = static_cast<int>(hits_.size()) - 1; i >= 0; --i) {
        if (PointIn(hits_[i].rect, pt)) {
            pressedIndex_ = i;
            if (hits_[i].kind == Hit::SidebarTrack) draggingSidebar_ = true;
            else if (hits_[i].kind == Hit::ContentTrack) draggingContent_ = true;
            break;
        }
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
    return pressedIndex_ != -1;
}

void MainWindow::OnLeftButtonUp(POINT pt) {
    if (draggingSidebar_ || draggingContent_) {
        draggingSidebar_ = false;
        draggingContent_ = false;
        return;
    }
    if (pressedIndex_ < 0) return;
    const int pressed = pressedIndex_;
    pressedIndex_ = -1;
    if (pressed < static_cast<int>(hits_.size())) {
        const HitRegion region = hits_[pressed];
        if (PointIn(region.rect, pt)) HandleHit(region);
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::OnMouseWheel(POINT pt, int delta) {
    const int steps = delta / WHEEL_DELTA;
    if (steps == 0) return;
    const int amount = steps * theme_.Px(kRowH);
    if (PointIn(rcSidebarList_, pt)) {
        sidebarScroll_ -= amount;
    } else if (PointIn(rcContentList_, pt)) {
        contentScroll_ -= amount;
    } else {
        contentScroll_ -= amount;
    }
    LayoutLists();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::OnKeyDown(WPARAM key) {
    switch (key) {
        case VK_F5:
            RefreshProfiles(false);
            break;
        case VK_ESCAPE:
            if (!searchQuery_.empty()) {
                SetWindowTextW(searchEdit_, L"");
                searchQuery_.clear();
                LayoutLists();
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            break;
        case 'F':
            if (GetKeyState(VK_CONTROL) < 0) {
                SetFocus(searchEdit_);
            }
            break;
        default:
            break;
    }
}

void MainWindow::OnCommand(WPARAM wParam, LPARAM lParam) {
    (void)lParam;
    if (rebuildingCombos_) return;
    const int id = LOWORD(wParam);
    const int code = HIWORD(wParam);

    if (id == kSearchId && code == EN_CHANGE) {
        searchQuery_ = ReadWindowText(searchEdit_);
        LayoutLists();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (id == kFilterId && code == CBN_SELCHANGE) {
        const int sel = static_cast<int>(SendMessageW(filterCombo_, CB_GETCURSEL, 0, 0));
        if (sel <= 0 || sel - 1 >= static_cast<int>(displayProfiles_.size())) {
            selectedProfile_.clear();
        } else {
            selectedProfile_ = displayProfiles_[sel - 1].steamid64;
        }
        config_.prefs.lastSelectedProfile = selectedProfile_;
        SaveConfigNow();
        LayoutLists();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (id == kSortId && code == CBN_SELCHANGE) {
        sortMode_ = static_cast<int>(SendMessageW(sortCombo_, CB_GETCURSEL, 0, 0));
        LayoutLists();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
}

void MainWindow::OnSetCursor(POINT pt) {
    for (int i = static_cast<int>(hits_.size()) - 1; i >= 0; --i) {
        if (PointIn(hits_[i].rect, pt)) {
            switch (hits_[i].kind) {
                case Hit::None:
                case Hit::GameCard:
                case Hit::SidebarTrack:
                case Hit::ContentTrack:
                    break;
                default:
                    SetCursor(LoadCursorW(nullptr, IDC_HAND));
                    return;
            }
            break;
        }
    }
    SetCursor(LoadCursorW(nullptr, IDC_ARROW));
}

// ---------------------------------------------------------------------------
// Datos y estado
// ---------------------------------------------------------------------------

void MainWindow::LoadState() {
    std::string error;
    if (!LoadConfig(config_, error)) {
        statusMessage_ = error;
    }
    SanitizeConfig(config_);
    hasApiKey_ = creds::LoadApiKey(apiKey_);
    if (!hasApiKey_) apiKey_.clear();
    demoMode_ = config_.profiles.empty() && !hasApiKey_;
    if (!config_.prefs.lastSelectedProfile.empty()) {
        selectedProfile_ = config_.prefs.lastSelectedProfile;
    }
    ReloadProfiles();
    if (demoMode_) {
        statusMessage_ = "Modo de demostracion: los datos mostrados son ficticios.";
    } else if (!hasApiKey_) {
        statusMessage_ = "Sin clave de API. Anade una en Clave API o usa el modo demo.";
    } else if (statusMessage_.empty()) {
        statusMessage_ = "Pulse Actualizar para consultar la actividad reciente.";
    }
}

void MainWindow::FlushConfig() {
    if (!configDirty_) return;
    std::string error;
    SaveConfig(config_, error);
    configDirty_ = false;
}

void MainWindow::SaveConfigNow() {
    configDirty_ = true;
    SetTimer(hwnd_, kTimerSave, 700, nullptr);
}

void MainWindow::ApplyAutoRefreshTimer() {
    KillTimer(hwnd_, kTimerAutoRefresh);
    if (config_.prefs.autoRefreshEnabled) {
        const int minutes = std::max(5, config_.prefs.autoRefreshMinutes);
        SetTimer(hwnd_, kTimerAutoRefresh, static_cast<UINT>(minutes) * 60u * 1000u, nullptr);
    }
}

void MainWindow::ReloadProfiles() {
    displayProfiles_.clear();
    if (demoMode_) {
        displayProfiles_ = demo::DemoProfiles();
    } else {
        displayProfiles_ = config_.profiles;
    }

    if (!demoMode_) {
        for (auto it = activities_.begin(); it != activities_.end();) {
            if (demo::IsDemoProfile(it->first)) it = activities_.erase(it);
            else ++it;
        }
    }

    for (const Profile& profile : displayProfiles_) {
        if (activities_.count(profile.steamid64) > 0) continue;
        ProfileActivity activity;
        if (demoMode_) {
            if (demo::DemoActivityFor(profile.steamid64, activity)) {
                activities_[profile.steamid64] = activity;
            }
        } else if (cache::LoadActivity(profile.steamid64, activity)) {
            activities_[profile.steamid64] = activity;
            cacheUsed_ = true;
        }
    }

    if (demoMode_ || !selectedProfile_.empty()) {
        if (FindProfile(selectedProfile_) == nullptr) selectedProfile_.clear();
    }
    RebuildCombos();
}

void MainWindow::SetDemoMode(bool enabled) {
    if (demoMode_ == enabled) return;
    demoMode_ = enabled;
    activities_.clear();
    pendingFetch_.clear();
    sortedCache_.clear();
    selectedProfile_.clear();
    if (enabled) {
        statusMessage_ = "Modo de demostracion: los datos mostrados son ficticios.";
    } else if (hasApiKey_) {
        statusMessage_ = "Modo real activo. Pulse Actualizar para consultar Steam.";
    } else {
        statusMessage_ = "Sin clave de API. Anadela en Clave API.";
    }
    ReloadProfiles();
    ComputeLayout();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::UpdateProfileMeta(const std::string& steamid64, const std::string& name,
                                   const std::string& avatarUrl) {
    bool changed = false;
    for (Profile& profile : config_.profiles) {
        if (profile.steamid64 != steamid64) continue;
        if (!name.empty() && profile.personaName != name) {
            profile.personaName = name;
            changed = true;
        }
        if (!avatarUrl.empty() && profile.avatarUrl != avatarUrl) {
            profile.avatarUrl = avatarUrl;
            changed = true;
        }
    }
    for (Profile& profile : displayProfiles_) {
        if (profile.steamid64 != steamid64) continue;
        if (!name.empty()) profile.personaName = name;
        if (!avatarUrl.empty()) profile.avatarUrl = avatarUrl;
    }
    if (changed) {
        SaveConfigNow();
    }
}

void MainWindow::RebuildCombos() {
    if (filterCombo_ == nullptr) return;
    rebuildingCombos_ = true;
    SendMessageW(filterCombo_, CB_RESETCONTENT, 0, 0);
    SendMessageW(filterCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Todos los integrantes"));
    int selected = 0;
    for (size_t i = 0; i < displayProfiles_.size(); ++i) {
        const std::wstring name = Widen(DisplayName(displayProfiles_[i]));
        SendMessageW(filterCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
        if (displayProfiles_[i].steamid64 == selectedProfile_) {
            selected = static_cast<int>(i) + 1;
        }
    }
    comboFilterCount_ = static_cast<int>(displayProfiles_.size()) + 1;
    SendMessageW(filterCombo_, CB_SETCURSEL, selected, 0);

    SendMessageW(sortCombo_, CB_RESETCONTENT, 0, 0);
    SendMessageW(sortCombo_, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(L"Mas tiempo reciente primero"));
    SendMessageW(sortCombo_, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(L"Menos jugado por otros primero"));
    SendMessageW(sortCombo_, CB_SETCURSEL, sortMode_, 0);
    rebuildingCombos_ = false;
}

void MainWindow::RefreshProfiles(bool all) {
    if (demoMode_) {
        for (const Profile& profile : displayProfiles_) {
            ProfileActivity activity;
            if (demo::DemoActivityFor(profile.steamid64, activity)) {
                activities_[profile.steamid64] = activity;
            }
        }
        lastRefreshUnix_ = UnixNow();
        statusMessage_ = "Datos de demostracion actualizados (ficticios).";
        LayoutLists();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (!hasApiKey_) {
        statusMessage_ = "No hay clave de API. Anade una en Clave API o usa el modo demo.";
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (displayProfiles_.empty()) {
        statusMessage_ = "No hay integrantes. Anade uno para empezar.";
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    std::vector<Profile> targets;
    for (const Profile& profile : displayProfiles_) {
        if (all || selectedProfile_.empty() || profile.steamid64 == selectedProfile_) {
            targets.push_back(profile);
        }
    }
    if (targets.empty()) return;

    refreshing_ = true;
    statusMessage_ = "Consultando la actividad reciente en Steam...";
    for (const Profile& profile : targets) RequestActivity(profile);
    LayoutLists();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::RequestActivity(const Profile& profile) {
    if (pendingFetch_.count(profile.steamid64) > 0) return;
    pendingFetch_.insert(profile.steamid64);
    const Profile target = profile;
    const std::string key = apiKey_;
    auto alive = alive_;
    worker_.Post([this, target, key, alive]() {
        HttpClient http;
        ProfileActivity activity;
        activity.steamid64 = target.steamid64;
        activity.personaName = target.personaName;
        activity.avatarUrl = target.avatarUrl;
        const steam::FetchOutcome outcome = steam::FetchProfileActivity(http, key, target, activity);
        bool saved = false;
        if (outcome.status == FetchStatus::Ok || outcome.status == FetchStatus::NoRecentGames) {
            saved = cache::SaveActivity(activity);
        }
        auto* result = new ActivityResult();
        result->activity = std::move(activity);
        result->profile = target;
        result->status = outcome.status;
        result->message = outcome.message;
        result->savedToCache = saved;
        if (alive->load()) {
            if (PostMessageW(hwnd_, WM_SRP_ACTIVITY, 0, reinterpret_cast<LPARAM>(result)) == FALSE) {
                delete result;
            }
        } else {
            delete result;
        }
    });
}

void MainWindow::EnsureImage(const std::string& key, const std::string& url) {
    if (url.empty() || key.empty()) return;
    if (images_.Get(key) != nullptr) return;
    if (images_.Failed(key)) return;
    if (!images_.BeginDownload(key)) return;
    const std::wstring destPath = ImageCache::PathFor(key).wstring();
    auto alive = alive_;
    worker_.Post([this, key, url, destPath, alive]() {
        HttpClient http;
        std::string error;
        const bool ok = http.DownloadToFile(url, destPath, 20000, error);
        auto* copy = new std::string(key);
        const UINT message = ok ? WM_SRP_IMAGE_OK : WM_SRP_IMAGE_FAIL;
        if (alive->load()) {
            if (PostMessageW(hwnd_, message, 0, reinterpret_cast<LPARAM>(copy)) == FALSE) {
                delete copy;
            }
        } else {
            delete copy;
        }
    });
}

void MainWindow::RequestImage(const std::string& key, const std::string& url) { EnsureImage(key, url); }

// ---------------------------------------------------------------------------
// Datos derivados
// ---------------------------------------------------------------------------

const Profile* MainWindow::FindProfile(const std::string& steamid64) const {
    for (const Profile& profile : displayProfiles_) {
        if (profile.steamid64 == steamid64) return &profile;
    }
    return nullptr;
}

const ProfileActivity* MainWindow::FindActivity(const std::string& steamid64) const {
    auto it = activities_.find(steamid64);
    if (it == activities_.end()) return nullptr;
    return &it->second;
}

int MainWindow::OthersRecentMinutes(uint32_t appid, const std::string& excludeSteamid) const {
    int total = 0;
    for (const Profile& profile : displayProfiles_) {
        if (profile.steamid64 == excludeSteamid) continue;
        auto it = activities_.find(profile.steamid64);
        if (it == activities_.end()) continue;
        for (const Game& game : it->second.games) {
            if (game.appid == appid) {
                total += game.playtime2Weeks;
                break;
            }
        }
    }
    return total;
}

std::string MainWindow::DisplayName(const Profile& profile) const {
    if (!profile.alias.empty()) return profile.alias;
    if (!profile.personaName.empty()) return profile.personaName;
    if (profile.steamid64.size() >= 4) return "Perfil " + profile.steamid64.substr(profile.steamid64.size() - 4);
    return "Perfil";
}

std::string MainWindow::ProfileUrl(const std::string& steamid64) const {
    return "https://steamcommunity.com/profiles/" + steamid64;
}

bool MainWindow::MatchesSearch(const std::string& name) const {
    if (searchQuery_.empty()) return true;
    return ToLowerAscii(name).find(ToLowerAscii(searchQuery_)) != std::string::npos;
}

std::vector<Game> MainWindow::BuildGameList(const ProfileActivity& activity,
                                            const std::string& ownerSteamId) const {
    std::vector<Game> games;
    const Preferences& prefs = config_.prefs;
    for (const Game& game : activity.games) {
        if (game.playtime2Weeks <= 0) continue;
        if (prefs.hideLowActivity && game.playtime2Weeks < prefs.lowActivityFilterMinutes) continue;
        if (!MatchesSearch(game.name)) continue;
        games.push_back(game);
    }
    if (sortMode_ == 1) {
        std::stable_sort(games.begin(), games.end(), [&](const Game& a, const Game& b) {
            const int oa = OthersRecentMinutes(a.appid, ownerSteamId);
            const int ob = OthersRecentMinutes(b.appid, ownerSteamId);
            if (oa != ob) return oa < ob;
            return a.playtime2Weeks > b.playtime2Weeks;
        });
    } else {
        std::stable_sort(games.begin(), games.end(), [](const Game& a, const Game& b) {
            if (a.playtime2Weeks != b.playtime2Weeks) return a.playtime2Weeks > b.playtime2Weeks;
            return a.playtimeForever > b.playtimeForever;
        });
    }
    return games;
}

// ---------------------------------------------------------------------------
// Acciones
// ---------------------------------------------------------------------------

bool MainWindow::OpenUrl(const std::string& url) const {
    const HINSTANCE result = ShellExecuteW(hwnd_, L"open", Widen(url).c_str(), nullptr, nullptr,
                                           SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(result) > 32;
}

void MainWindow::HandleHit(const HitRegion& region) {
    switch (region.kind) {
        case Hit::RefreshSelected:
            RefreshProfiles(false);
            break;
        case Hit::RefreshAll:
            RefreshProfiles(true);
            break;
        case Hit::Settings:
            OpenSettings();
            break;
        case Hit::ApiKey:
            OpenApiKeyDialog();
            break;
        case Hit::Demo:
            SetDemoMode(!demoMode_);
            break;
        case Hit::AddProfile:
            AddProfileDialog();
            break;
        case Hit::EditProfile:
            EditSelectedProfile();
            break;
        case Hit::RemoveProfile:
            RemoveSelectedProfile();
            break;
        case Hit::TabGames:
            generalView_ = false;
            LayoutLists();
            InvalidateRect(hwnd_, nullptr, FALSE);
            break;
        case Hit::TabOverview:
            generalView_ = true;
            LayoutLists();
            InvalidateRect(hwnd_, nullptr, FALSE);
            break;
        case Hit::HideLow:
            config_.prefs.hideLowActivity = !config_.prefs.hideLowActivity;
            SaveConfigNow();
            LayoutLists();
            InvalidateRect(hwnd_, nullptr, FALSE);
            break;
        case Hit::ClearSearch:
            SetWindowTextW(searchEdit_, L"");
            searchQuery_.clear();
            LayoutLists();
            InvalidateRect(hwnd_, nullptr, FALSE);
            break;
        case Hit::ProfileRow:
            selectedProfile_ = region.param;
            config_.prefs.lastSelectedProfile = selectedProfile_;
            SaveConfigNow();
            RebuildCombos();
            LayoutLists();
            InvalidateRect(hwnd_, nullptr, FALSE);
            break;
        case Hit::ProfileOpen:
            OpenUrl(ProfileUrl(region.param));
            break;
        case Hit::CardOpen:
        case Hit::GeneralCard: {
            const uint32_t appid = static_cast<uint32_t>(std::strtoul(region.param.c_str(), nullptr, 10));
            if (appid != 0 && !OpenUrl("steam://store/" + std::to_string(appid))) {
                OpenUrl(steam::StorePageUrl(appid));
            }
            break;
        }
        case Hit::GameCard:
        case Hit::SidebarTrack:
        case Hit::ContentTrack:
        case Hit::None:
        default:
            break;
    }
}

void MainWindow::OpenAbout() { ShowAboutDialog(hwnd_); }

void MainWindow::AddProfileDialog() {
    if (demoMode_) {
        MessageBoxW(hwnd_,
                    L"Estas en modo de demostracion con datos ficticios.\n"
                    L"Sal del modo demo para anadir integrantes reales.",
                    L"Modo demostracion", MB_OK | MB_ICONINFORMATION);
        return;
    }
    const ProfileDialogResult result = ShowProfileDialog(hwnd_, "", "");
    if (!result.accepted) return;

    std::string value;
    const ProfileInputKind kind = ParseProfileInput(result.input, value);
    if (kind == ProfileInputKind::SteamId64) {
        AddProfileById(value, result.alias);
        return;
    }
    if (kind == ProfileInputKind::Vanity) {
        if (!hasApiKey_) {
            MessageBoxW(hwnd_,
                        L"Se necesita una clave de Steam Web API para resolver nombres de perfil.",
                        L"Integrante", MB_OK | MB_ICONWARNING);
            return;
        }
        if (pendingVanity_) return;
        pendingVanity_ = true;
        pendingVanityAlias_ = result.alias;
        statusMessage_ = "Resolviendo el nombre de perfil con Steam...";
        const std::string vanity = value;
        const std::string key = apiKey_;
        auto alive = alive_;
        worker_.Post([this, vanity, key, alive]() {
            HttpClient http;
            std::string steamid;
            const steam::FetchOutcome outcome = steam::ResolveVanity(http, key, vanity, steamid);
            auto* result = new VanityResult();
            result->vanity = vanity;
            result->steamid64 = steamid;
            result->ok = outcome.status == FetchStatus::Ok;
            result->message = outcome.message;
            if (alive->load()) {
                if (PostMessageW(hwnd_, WM_SRP_VANITY, 0, reinterpret_cast<LPARAM>(result)) == FALSE) {
                    delete result;
                }
            } else {
                delete result;
            }
        });
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    MessageBoxW(hwnd_, L"El valor introducido no es valido.", L"Integrante",
                MB_OK | MB_ICONWARNING);
}

void MainWindow::AddProfileById(const std::string& steamid64, const std::string& alias) {
    if (!IsValidSteamId64(steamid64)) {
        MessageBoxW(hwnd_, L"El SteamID64 no es valido.", L"Integrante", MB_OK | MB_ICONWARNING);
        return;
    }
    if (FindProfile(steamid64) != nullptr) {
        MessageBoxW(hwnd_, L"Ese integrante ya esta en la lista.", L"Integrante",
                    MB_OK | MB_ICONINFORMATION);
        return;
    }
    Profile profile;
    profile.steamid64 = steamid64;
    profile.alias = alias;
    config_.profiles.push_back(profile);
    ProfileActivity activity;
    activity.steamid64 = steamid64;
    activities_[steamid64] = activity;
    selectedProfile_ = steamid64;
    config_.prefs.lastSelectedProfile = steamid64;
    SaveConfigNow();
    ReloadProfiles();
    LayoutLists();
    InvalidateRect(hwnd_, nullptr, FALSE);
    RefreshProfiles(false);
}

void MainWindow::EditSelectedProfile() {
    if (selectedProfile_.empty()) return;
    const Profile* profile = FindProfile(selectedProfile_);
    if (profile == nullptr) return;

    const ProfileDialogResult result = ShowProfileDialog(hwnd_, selectedProfile_, profile->alias);
    if (!result.accepted) return;

    std::string value;
    const ProfileInputKind kind = ParseProfileInput(result.input, value);
    if (kind == ProfileInputKind::Vanity) {
        MessageBoxW(hwnd_,
                    L"Para cambiar el identificador por un nombre de perfil, anade un integrante "
                    L"nuevo y quita el anterior.",
                    L"Editar integrante", MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (kind != ProfileInputKind::SteamId64) {
        MessageBoxW(hwnd_, L"El SteamID64 no es valido.", L"Editar integrante",
                    MB_OK | MB_ICONWARNING);
        return;
    }

    const std::string oldId = selectedProfile_;
    if (value != oldId && FindProfile(value) != nullptr) {
        MessageBoxW(hwnd_, L"Ya existe un integrante con ese SteamID64.", L"Editar integrante",
                    MB_OK | MB_ICONINFORMATION);
        return;
    }
    for (Profile& entry : config_.profiles) {
        if (entry.steamid64 == oldId) {
            entry.steamid64 = value;
            entry.alias = result.alias;
            break;
        }
    }
    if (value != oldId) {
        auto it = activities_.find(oldId);
        if (it != activities_.end()) {
            ProfileActivity activity = it->second;
            activities_.erase(it);
            activity.steamid64 = value;
            activities_[value] = activity;
        }
        std::error_code ec;
        std::filesystem::remove(cache::ActivityPath(oldId), ec);
    }
    selectedProfile_ = value;
    config_.prefs.lastSelectedProfile = value;
    SaveConfigNow();
    ReloadProfiles();
    LayoutLists();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::RemoveSelectedProfile() {
    if (selectedProfile_.empty()) return;
    const Profile* profile = FindProfile(selectedProfile_);
    if (profile == nullptr) return;
    const std::wstring prompt = L"¿Quitar a " + Widen(DisplayName(*profile)) + L" de la lista?";
    if (MessageBoxW(hwnd_, prompt.c_str(), L"Quitar integrante",
                    MB_YESNO | MB_ICONQUESTION) != IDYES) {
        return;
    }
    const std::string id = selectedProfile_;
    config_.profiles.erase(std::remove_if(config_.profiles.begin(), config_.profiles.end(),
                                          [&](const Profile& entry) {
                                              return entry.steamid64 == id;
                                          }),
                           config_.profiles.end());
    activities_.erase(id);
    std::error_code ec;
    std::filesystem::remove(cache::ActivityPath(id), ec);
    selectedProfile_.clear();
    config_.prefs.lastSelectedProfile.clear();
    SaveConfigNow();
    ReloadProfiles();
    LayoutLists();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::OpenSettings() {
    Preferences prefs = config_.prefs;
    bool clearCache = false;
    if (ShowSettingsDialog(hwnd_, prefs, clearCache)) {
        config_.prefs = prefs;
        if (clearCache) {
            cache::ClearAll();
            images_.Clear();
            activities_.clear();
            sortedCache_.clear();
            EnsureDataDirs();
            ReloadProfiles();
        }
        SaveConfigNow();
        ApplyAutoRefreshTimer();
        LayoutLists();
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void MainWindow::OpenApiKeyDialog() {
    bool changed = false;
    ShowApiKeyDialog(hwnd_, worker_, changed);
    if (changed) {
        hasApiKey_ = creds::LoadApiKey(apiKey_);
        if (!hasApiKey_) apiKey_.clear();
        if (!hasApiKey_ && !demoMode_) {
            statusMessage_ = "No hay clave de API. Anade una para consultar Steam.";
        } else if (hasApiKey_) {
            statusMessage_ = "Clave de API actualizada. Pulse Actualizar.";
        }
        RebuildCombos();
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

// ---------------------------------------------------------------------------
// Disposicion
// ---------------------------------------------------------------------------

void MainWindow::ComputeLayout() {
    GetClientRect(hwnd_, &rcClient_);
    const int width = rcClient_.right;
    const int height = rcClient_.bottom;
    const int pad = theme_.Px(12);
    const int gap = theme_.Px(8);

    rcHeader_ = {0, 0, width, theme_.Px(kHeaderH)};
    rcToolbar_ = {0, rcHeader_.bottom, width, rcHeader_.bottom + theme_.Px(kToolbarH)};
    rcStatus_ = {0, height - theme_.Px(kStatusH), width, height};

    const int bodyTop = rcToolbar_.bottom;
    const int bodyBottom = rcStatus_.top;
    int sidebarWidth = theme_.Px(kSidebarW);
    if (sidebarWidth > width / 2) sidebarWidth = width / 2;
    rcSidebar_ = {0, bodyTop, sidebarWidth, bodyBottom};
    rcContent_ = {sidebarWidth, bodyTop, width, bodyBottom};

    const int titleH = theme_.Px(36);
    const int btnH = theme_.Px(kBtnH);
    rcSidebarList_ = {rcSidebar_.left, rcSidebar_.top + titleH, rcSidebar_.right,
                      rcSidebar_.bottom - btnH - pad};

    const int buttonWidth = (rcSidebar_.right - rcSidebar_.left - pad * 2 - gap * 2) / 3;
    const int buttonY = rcSidebar_.bottom - btnH - pad / 2;
    rcBtnAdd_ = {rcSidebar_.left + pad, buttonY, rcSidebar_.left + pad + buttonWidth, buttonY + btnH};
    rcBtnEdit_ = {rcBtnAdd_.right + gap, buttonY, rcBtnAdd_.right + gap + buttonWidth, buttonY + btnH};
    rcBtnRemove_ = {rcBtnEdit_.right + gap, buttonY, rcBtnEdit_.right + gap + buttonWidth, buttonY + btnH};

    const int toolbarHeight = rcToolbar_.bottom - rcToolbar_.top;
    const int toolbarY = rcToolbar_.top + (toolbarHeight - btnH) / 2;
    int x = pad;
    const int tabWidth = theme_.Px(150);
    rcTabGames_ = {x, toolbarY, x + tabWidth, toolbarY + btnH};
    x = rcTabGames_.right + gap;
    rcTabOverview_ = {x, toolbarY, x + tabWidth, toolbarY + btnH};
    x = rcTabOverview_.right + theme_.Px(16);
    const int refreshWidth = theme_.Px(112);
    rcBtnRefresh_ = {x, toolbarY, x + refreshWidth, toolbarY + btnH};
    x = rcBtnRefresh_.right + gap;
    const int refreshAllWidth = theme_.Px(136);
    rcBtnRefreshAll_ = {x, toolbarY, x + refreshAllWidth, toolbarY + btnH};

    int right = width - pad;
    const int demoWidth = theme_.Px(112);
    rcBtnDemo_ = {right - demoWidth, toolbarY, right, toolbarY + btnH};
    right = rcBtnDemo_.left - gap;
    const int apiWidth = theme_.Px(96);
    rcBtnApiKey_ = {right - apiWidth, toolbarY, right, toolbarY + btnH};
    right = rcBtnApiKey_.left - gap;
    const int settingsWidth = theme_.Px(96);
    rcBtnSettings_ = {right - settingsWidth, toolbarY, right, toolbarY + btnH};

    const int filterBarHeight = theme_.Px(kFilterBarH);
    rcContentList_ = {rcContent_.left, rcContent_.top + filterBarHeight, rcContent_.right,
                      rcContent_.bottom};

    const int controlH = theme_.Px(26);
    const int controlY = rcContent_.top + (filterBarHeight - controlH) / 2;
    int cx = rcContent_.left + pad;
    const int searchWidth = theme_.Px(250);
    rcSearch_ = {cx, controlY, cx + searchWidth, controlY + controlH};
    rcSearchClear_ = {rcSearch_.right - theme_.Px(26), controlY, rcSearch_.right, controlY + controlH};
    cx = rcSearch_.right + theme_.Px(10);
    const int filterWidth = theme_.Px(200);
    rcFilter_ = {cx, controlY, cx + filterWidth, controlY + controlH};
    cx = rcFilter_.right + theme_.Px(10);
    const int sortWidth = theme_.Px(230);
    rcSort_ = {cx, controlY, cx + sortWidth, controlY + controlH};
    const int hideWidth = theme_.Px(184);
    rcHideLow_ = {rcContent_.right - pad - hideWidth, controlY, rcContent_.right - pad,
                  controlY + controlH};

    rcSidebarTrack_ = {rcSidebar_.right - theme_.Px(7), rcSidebarList_.top, rcSidebar_.right,
                       rcSidebarList_.bottom};
    rcContentTrack_ = {rcContent_.right - theme_.Px(7), rcContentList_.top, rcContent_.right,
                       rcContentList_.bottom};

    if (searchEdit_ != nullptr) {
        MoveWindow(searchEdit_, rcSearch_.left + theme_.Px(8), rcSearch_.top + theme_.Px(3),
                   rcSearch_.right - rcSearch_.left - theme_.Px(34),
                   rcSearch_.bottom - rcSearch_.top - theme_.Px(6), TRUE);
    }
    if (filterCombo_ != nullptr) {
        MoveWindow(filterCombo_, rcFilter_.left, rcFilter_.top, rcFilter_.right - rcFilter_.left,
                   theme_.Px(220), TRUE);
    }
    if (sortCombo_ != nullptr) {
        MoveWindow(sortCombo_, rcSort_.left, rcSort_.top, rcSort_.right - rcSort_.left,
                   theme_.Px(220), TRUE);
    }

    ClampScroll();
    LayoutLists();
}

void MainWindow::ClampScroll() {
    const int sideView = rcSidebarList_.bottom - rcSidebarList_.top;
    sidebarScroll_ = std::clamp(sidebarScroll_, 0, std::max(0, sidebarContentHeight_ - sideView));
    const int contentView = rcContentList_.bottom - rcContentList_.top;
    contentScroll_ = std::clamp(contentScroll_, 0, std::max(0, contentContentHeight_ - contentView));
}

void MainWindow::LayoutLists() {
    const int rowH = theme_.Px(kRowH);

    profileRowIds_.clear();
    profileRowIds_.push_back(std::string());  // fila "Todos"
    for (const Profile& profile : displayProfiles_) profileRowIds_.push_back(profile.steamid64);
    sidebarContentHeight_ = static_cast<int>(profileRowIds_.size()) * rowH;

    BuildContent();
    ClampScroll();

    profileRowRects_.clear();
    profileRowRects_.reserve(profileRowIds_.size());
    int y = rcSidebarList_.top - sidebarScroll_;
    for (size_t i = 0; i < profileRowIds_.size(); ++i) {
        profileRowRects_.push_back({rcSidebarList_.left, y, rcSidebarList_.right, y + rowH});
        y += rowH;
    }
}

void MainWindow::BuildContent() {
    contentEntries_.clear();
    sortedCache_.clear();

    const int listLeft = rcContentList_.left + theme_.Px(16);
    const int listRight = rcContentList_.right - theme_.Px(20);
    const int listWidth = std::max(theme_.Px(240), listRight - listLeft);
    const int cardH = theme_.Px(kCardH);
    const int generalH = theme_.Px(kGeneralCardH);
    const int headerH = theme_.Px(kMemberHeaderH);
    const int gap = theme_.Px(10);
    int y = rcContentList_.top - contentScroll_;
    const int startY = y;

    auto addGame = [&](const Game& game, const std::string& ownerId, const std::string& ownerName,
                       bool showOthers) {
        ContentEntry entry;
        entry.type = ContentEntry::Type::Game;
        entry.rect = {listLeft, y, listLeft + listWidth, y + cardH};
        entry.game = game;
        entry.ownerId = ownerId;
        entry.ownerName = ownerName;
        entry.showOthers = showOthers;
        contentEntries_.push_back(std::move(entry));
        y += cardH + gap;
    };

    if (generalView_) {
        struct Aggregate {
            std::string name;
            int total = 0;
            std::vector<std::pair<std::string, int>> members;
        };
        std::map<uint32_t, Aggregate> aggregates;
        for (const Profile& profile : displayProfiles_) {
            const ProfileActivity* activity = FindActivity(profile.steamid64);
            if (activity == nullptr) continue;
            for (const Game& game : activity->games) {
                if (game.playtime2Weeks <= 0) continue;
                if (config_.prefs.hideLowActivity &&
                    game.playtime2Weeks < config_.prefs.lowActivityFilterMinutes) {
                    continue;
                }
                if (!MatchesSearch(game.name)) continue;
                Aggregate& aggregate = aggregates[game.appid];
                if (aggregate.name.empty()) aggregate.name = game.name;
                aggregate.total += game.playtime2Weeks;
                aggregate.members.push_back({DisplayName(profile), game.playtime2Weeks});
            }
        }
        std::vector<std::pair<uint32_t, Aggregate>> ordered(aggregates.begin(), aggregates.end());
        std::sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) {
            return a.second.total > b.second.total;
        });
        for (auto& [appid, aggregate] : ordered) {
            std::sort(aggregate.members.begin(), aggregate.members.end(),
                      [](const auto& a, const auto& b) { return a.second > b.second; });
            ContentEntry entry;
            entry.type = ContentEntry::Type::General;
            entry.rect = {listLeft, y, listLeft + listWidth, y + generalH};
            entry.game.appid = appid;
            entry.title = aggregate.name;
            entry.totalRecent = aggregate.total;
            entry.members = std::move(aggregate.members);
            contentEntries_.push_back(std::move(entry));
            y += generalH + gap;
        }
    } else if (selectedProfile_.empty()) {
        for (const Profile& profile : displayProfiles_) {
            const ProfileActivity* activity = FindActivity(profile.steamid64);
            if (activity == nullptr) continue;
            const std::vector<Game> games = BuildGameList(*activity, profile.steamid64);
            if (games.empty()) continue;

            int totalRecent = 0;
            for (const Game& game : activity->games) totalRecent += game.playtime2Weeks;
            const ActivityLevel level =
                ClassifyActivity(totalRecent, config_.prefs.moderateActivityMinutes,
                                 config_.prefs.highActivityMinutes);

            ContentEntry header;
            header.type = ContentEntry::Type::MemberHeader;
            header.rect = {listLeft, y, listLeft + listWidth, y + headerH};
            header.ownerId = profile.steamid64;
            header.ownerName = DisplayName(profile);
            header.headerSubtitle = FormatMinutes(totalRecent) + " en 2 semanas · " +
                                    std::to_string(games.size()) + " juegos recientes";
            header.headerLevel = static_cast<int>(level);
            contentEntries_.push_back(std::move(header));
            y += headerH;

            for (const Game& game : games) {
                addGame(game, profile.steamid64, DisplayName(profile), false);
            }
            y += theme_.Px(4);
        }
    } else {
        const Profile* profile = FindProfile(selectedProfile_);
        const ProfileActivity* activity = FindActivity(selectedProfile_);
        if (profile != nullptr && activity != nullptr) {
            const std::vector<Game> games = BuildGameList(*activity, selectedProfile_);
            for (const Game& game : games) {
                addGame(game, selectedProfile_, DisplayName(*profile), true);
            }
        }
    }

    contentContentHeight_ = y - startY;

    const int viewHeight = rcContentList_.bottom - rcContentList_.top;
    const int maxScroll = std::max(0, contentContentHeight_ - viewHeight);
    if (contentScroll_ > maxScroll) {
        // Recalcula una sola vez con el desplazamiento corregido.
        contentScroll_ = maxScroll;
        BuildContent();
    }
}

// ---------------------------------------------------------------------------
// Dibujo
// ---------------------------------------------------------------------------

void MainWindow::PaintAll(Gdiplus::Graphics& g) {
    hits_.clear();
    FillRect(g, RectFOf(rcClient_), theme_.bg);
    DrawHeader(g);
    DrawToolbar(g);
    DrawSidebar(g);
    DrawContent(g);
    DrawStatusBar(g);
}

void MainWindow::DrawHeader(Gdiplus::Graphics& g) {
    FillRect(g, RectFOf(rcHeader_), theme_.panelAlt);
    FillRect(g, Gdiplus::RectF(0.0f, static_cast<float>(rcHeader_.bottom - 1),
                               static_cast<float>(rcClient_.right), 1.0f),
             theme_.border);

    const float pad = static_cast<float>(theme_.Px(18));
    DrawTextIn(g, L"Steam Recent Play", *theme_.fontBig,
               Gdiplus::RectF(pad, static_cast<float>(rcHeader_.top + theme_.Px(8)),
                              static_cast<float>(rcClient_.right) * 0.5f,
                              static_cast<float>(theme_.Px(26))),
               theme_.text, Gdiplus::StringAlignmentNear, false, true);
    DrawTextIn(g, Widen("Actividad reciente de la familia · ultimas dos semanas"),
               *theme_.fontSmall,
               Gdiplus::RectF(pad, static_cast<float>(rcHeader_.top + theme_.Px(34)),
                              static_cast<float>(rcClient_.right) * 0.6f,
                              static_cast<float>(theme_.Px(16))),
               theme_.textDim, Gdiplus::StringAlignmentNear, false, true);

    // Insignias a la derecha.
    int right = rcHeader_.right - theme_.Px(18);
    const int badgeH = theme_.Px(22);
    const int badgeY = rcHeader_.top + (rcHeader_.bottom - rcHeader_.top - badgeH) / 2;

    auto drawBadgeRight = [&](const std::string& text, const Gdiplus::Color& color) {
        const std::wstring wide = Widen(text);
        const int textW = static_cast<int>(MeasureTextWidth(g, wide, *theme_.fontSmall)) + theme_.Px(20);
        RECT rect{right - textW, badgeY, right, badgeY + badgeH};
        DrawBadge(g, rect, text, color);
        right = rect.left - theme_.Px(8);
    };

    if (demoMode_) drawBadgeRight("MODO DEMO", theme_.warn);
    if (hasApiKey_) {
        drawBadgeRight("Clave configurada", theme_.good);
    } else {
        drawBadgeRight("Sin clave de API", theme_.textFaint);
    }
    if (refreshing_) drawBadgeRight("Actualizando...", theme_.accent);
}

void MainWindow::DrawToolbar(Gdiplus::Graphics& g) {
    FillRect(g, RectFOf(rcToolbar_), theme_.panel);
    FillRect(g, Gdiplus::RectF(0.0f, static_cast<float>(rcToolbar_.bottom - 1),
                               static_cast<float>(rcClient_.right), 1.0f),
             theme_.border);

    {
        const int i = AddHit(Hit::TabGames, rcTabGames_);
        DrawChip(g, rcTabGames_, "Juegos recientes", !generalView_, i == hoverIndex_);
    }
    {
        const int i = AddHit(Hit::TabOverview, rcTabOverview_);
        DrawChip(g, rcTabOverview_, "Vista general", generalView_, i == hoverIndex_);
    }
    {
        const int i = AddHit(Hit::RefreshSelected, rcBtnRefresh_);
        DrawButton(g, rcBtnRefresh_, "Actualizar", 1, i == hoverIndex_, i == pressedIndex_,
                   !refreshing_);
    }
    {
        const int i = AddHit(Hit::RefreshAll, rcBtnRefreshAll_);
        DrawButton(g, rcBtnRefreshAll_, "Actualizar todo", 0, i == hoverIndex_, i == pressedIndex_,
                   !refreshing_);
    }
    {
        const int i = AddHit(Hit::Demo, rcBtnDemo_);
        DrawButton(g, rcBtnDemo_, demoMode_ ? "Salir de demo" : "Modo demo", demoMode_ ? 1 : 0,
                   i == hoverIndex_, i == pressedIndex_);
    }
    {
        const int i = AddHit(Hit::ApiKey, rcBtnApiKey_);
        DrawButton(g, rcBtnApiKey_, "Clave API", 0, i == hoverIndex_, i == pressedIndex_);
    }
    {
        const int i = AddHit(Hit::Settings, rcBtnSettings_);
        DrawButton(g, rcBtnSettings_, "Ajustes", 0, i == hoverIndex_, i == pressedIndex_);
    }
}

void MainWindow::DrawSidebar(Gdiplus::Graphics& g) {
    FillRect(g, RectFOf(rcSidebar_), theme_.panel);
    FillRect(g, Gdiplus::RectF(static_cast<float>(rcSidebar_.right - 1),
                               static_cast<float>(rcSidebar_.top), 1.0f,
                               static_cast<float>(rcSidebar_.bottom - rcSidebar_.top)),
             theme_.border);

    const float pad = static_cast<float>(theme_.Px(16));
    DrawTextIn(g, L"Integrantes", *theme_.fontTitle,
               Gdiplus::RectF(pad, static_cast<float>(rcSidebar_.top + theme_.Px(9)),
                              static_cast<float>(rcSidebar_.right) - pad * 2.0f,
                              static_cast<float>(theme_.Px(20))),
               theme_.text, Gdiplus::StringAlignmentNear, false, true);
    const std::string count = std::to_string(displayProfiles_.size()) +
                              (displayProfiles_.size() == 1 ? " perfil" : " perfiles");
    DrawTextIn(g, Widen(count), *theme_.fontSmall,
               Gdiplus::RectF(pad, static_cast<float>(rcSidebar_.top + theme_.Px(12)),
                              static_cast<float>(rcSidebar_.right) - pad * 2.0f,
                              static_cast<float>(theme_.Px(16))),
               theme_.textFaint, Gdiplus::StringAlignmentFar, false, true);

    const Gdiplus::GraphicsState state = g.Save();
    Gdiplus::Rect clip(rcSidebarList_.left, rcSidebarList_.top,
                       rcSidebarList_.right - rcSidebarList_.left,
                       rcSidebarList_.bottom - rcSidebarList_.top);
    g.SetClip(clip);

    // Fila "Todos".
    if (!profileRowRects_.empty()) {
        const RECT rect = profileRowRects_[0];
        if (Intersects(rect, rcSidebarList_)) {
            const int i = AddHit(Hit::ProfileRow, rect, std::string());
            const bool selected = selectedProfile_.empty();
            RECT bg = rect;
            InflateRect(&bg, -theme_.Px(6), -theme_.Px(3));
            const float radius = theme_.Pxf(8);
            if (selected) FillRoundRect(g, RectFOf(bg), radius, theme_.cardSelected);
            else if (i == hoverIndex_) FillRoundRect(g, RectFOf(bg), radius, theme_.cardHover);
            else FillRoundRect(g, RectFOf(bg), radius, theme_.card);

            int recentGames = 0;
            int totalRecent = 0;
            for (const Profile& profile : displayProfiles_) {
                const ProfileActivity* activity = FindActivity(profile.steamid64);
                if (activity == nullptr) continue;
                recentGames += static_cast<int>(activity->games.size());
                for (const Game& game : activity->games) totalRecent += game.playtime2Weeks;
            }
            const float textPad = static_cast<float>(theme_.Px(14));
            DrawTextIn(g, L"Todos los integrantes", *theme_.fontBodyBold,
                       Gdiplus::RectF(static_cast<float>(bg.left) + textPad,
                                      static_cast<float>(bg.top + theme_.Px(8)),
                                      static_cast<float>(bg.right - bg.left) - textPad * 2.0f,
                                      static_cast<float>(theme_.Px(18))),
                       theme_.text, Gdiplus::StringAlignmentNear, false, true);
            const std::string subtitle = std::to_string(recentGames) + " juegos · " +
                                         FormatMinutes(totalRecent) + " en 2 semanas";
            DrawTextIn(g, Widen(subtitle), *theme_.fontSmall,
                       Gdiplus::RectF(static_cast<float>(bg.left) + textPad,
                                      static_cast<float>(bg.top + theme_.Px(28)),
                                      static_cast<float>(bg.right - bg.left) - textPad * 2.0f,
                                      static_cast<float>(theme_.Px(16))),
                       theme_.textDim, Gdiplus::StringAlignmentNear, false, true);
        }
    }

    for (size_t index = 0; index < displayProfiles_.size(); ++index) {
        const RECT rect = profileRowRects_[index + 1];
        if (!Intersects(rect, rcSidebarList_)) continue;
        const Profile& profile = displayProfiles_[index];
        const int i = AddHit(Hit::ProfileRow, rect, profile.steamid64);
        DrawProfileRow(g, rect, profile, FindActivity(profile.steamid64),
                       profile.steamid64 == selectedProfile_, i == hoverIndex_);
    }

    g.Restore(state);

    DrawScrollbar(g, rcSidebarTrack_, sidebarContentHeight_, sidebarScroll_);

    {
        const int i = AddHit(Hit::AddProfile, rcBtnAdd_);
        DrawButton(g, rcBtnAdd_, "Anadir", 0, i == hoverIndex_, i == pressedIndex_, !demoMode_);
    }
    {
        const bool enabled = !demoMode_ && !selectedProfile_.empty();
        const int i = enabled ? AddHit(Hit::EditProfile, rcBtnEdit_) : -1;
        DrawButton(g, rcBtnEdit_, "Editar", 0, i == hoverIndex_, i == pressedIndex_, enabled);
    }
    {
        const bool enabled = !demoMode_ && !selectedProfile_.empty();
        const int i = enabled ? AddHit(Hit::RemoveProfile, rcBtnRemove_) : -1;
        DrawButton(g, rcBtnRemove_, "Quitar", 0, i == hoverIndex_, i == pressedIndex_, enabled);
    }
}

void MainWindow::DrawContent(Gdiplus::Graphics& g) {
    FillRect(g, RectFOf(rcContent_), theme_.bg);

    // Campo de busqueda (borde dibujado por detras del control nativo).
    FillRoundRect(g, RectFOf(rcSearch_), theme_.Pxf(6), theme_.panelAlt);
    StrokeRoundRect(g, RectFOf(rcSearch_), theme_.Pxf(6), theme_.border, 1.0f);
    {
        const int i = AddHit(Hit::ClearSearch, rcSearchClear_);
        DrawTextIn(g, L"\x00D7", *theme_.fontBody,
                   RectFOf(rcSearchClear_), i == hoverIndex_ ? theme_.text : theme_.textFaint,
                   Gdiplus::StringAlignmentCenter, true, false);
    }
    {
        const int i = AddHit(Hit::HideLow, rcHideLow_);
        DrawChip(g, rcHideLow_, "Ocultar poca actividad", config_.prefs.hideLowActivity,
                 i == hoverIndex_);
    }

    const Gdiplus::GraphicsState state = g.Save();
    Gdiplus::Rect clip(rcContentList_.left, rcContentList_.top,
                       rcContentList_.right - rcContentList_.left,
                       rcContentList_.bottom - rcContentList_.top);
    g.SetClip(clip);

    if (contentEntries_.empty()) {
        std::string title;
        std::string subtitle;
        if (demoMode_) {
            title = "Datos de demostracion";
            subtitle = "Estos datos son ficticios. Anade una clave de API para ver actividad real.";
        } else if (displayProfiles_.empty()) {
            title = "Todavia no hay integrantes";
            subtitle = "Pulsa \"Anadir\" en la izquierda para agregar un perfil de Steam.";
        } else if (!hasApiKey_) {
            title = "Falta la clave de API";
            subtitle = "Pulsa \"Clave API\" para introducir tu clave de Steam Web API.";
        } else {
            title = "Sin resultados que mostrar";
            subtitle = "Pulsa \"Actualizar\" para consultar la actividad reciente, o ajusta los filtros.";
        }
        DrawEmptyState(g, rcContentList_, title, subtitle);
    } else {
        for (const ContentEntry& entry : contentEntries_) {
            if (!Intersects(entry.rect, rcContentList_)) continue;
            switch (entry.type) {
                case ContentEntry::Type::MemberHeader: {
                    DrawTextIn(g, Widen(entry.ownerName), *theme_.fontBodyBold,
                               Gdiplus::RectF(static_cast<float>(entry.rect.left),
                                              static_cast<float>(entry.rect.top),
                                              static_cast<float>(entry.rect.right - entry.rect.left),
                                              static_cast<float>(theme_.Px(18))),
                               theme_.text, Gdiplus::StringAlignmentNear, false, true);
                    DrawTextIn(g, Widen(entry.headerSubtitle), *theme_.fontSmall,
                               Gdiplus::RectF(static_cast<float>(entry.rect.left),
                                              static_cast<float>(entry.rect.top + theme_.Px(18)),
                                              static_cast<float>(entry.rect.right - entry.rect.left) -
                                                  static_cast<float>(theme_.Px(28)),
                                              static_cast<float>(theme_.Px(14))),
                               theme_.textDim, Gdiplus::StringAlignmentNear, false, true);
                    const int dot = theme_.Px(10);
                    RECT dotRect{entry.rect.right - dot - theme_.Px(6),
                                 entry.rect.top + theme_.Px(7), 0, 0};
                    dotRect.right = dotRect.left + dot;
                    dotRect.bottom = dotRect.top + dot;
                    FillRoundRect(g, RectFOf(dotRect), theme_.Pxf(5),
                                  theme_.LevelColor(entry.headerLevel));
                    break;
                }
                case ContentEntry::Type::Game: {
                    const int i = AddHit(Hit::GameCard, entry.rect,
                                         std::to_string(entry.game.appid));
                    DrawGameCard(g, entry.rect, entry.game, entry.ownerId, entry.ownerName,
                                 entry.showOthers, i == hoverIndex_);
                    break;
                }
                case ContentEntry::Type::General: {
                    const int i = AddHit(Hit::GeneralCard, entry.rect,
                                         std::to_string(entry.game.appid));
                    DrawGeneralCard(g, entry.rect, entry.game.appid, entry.title, entry.totalRecent,
                                    entry.members, i == hoverIndex_);
                    break;
                }
            }
        }
    }
    g.Restore(state);

    DrawScrollbar(g, rcContentTrack_, contentContentHeight_, contentScroll_);
}

void MainWindow::DrawStatusBar(Gdiplus::Graphics& g) {
    FillRect(g, RectFOf(rcStatus_), theme_.panelAlt);
    FillRect(g, Gdiplus::RectF(0.0f, static_cast<float>(rcStatus_.top),
                               static_cast<float>(rcClient_.right), 1.0f),
             theme_.border);

    const float pad = static_cast<float>(theme_.Px(16));
    DrawTextIn(g, Widen(statusMessage_), *theme_.fontSmall,
               Gdiplus::RectF(pad, static_cast<float>(rcStatus_.top),
                              static_cast<float>(rcClient_.right) * 0.66f,
                              static_cast<float>(rcStatus_.bottom - rcStatus_.top)),
               theme_.textDim, Gdiplus::StringAlignmentNear, true, true);

    int64_t newest = 0;
    bool cached = false;
    for (const auto& [id, activity] : activities_) {
        (void)id;
        if (activity.fetchedAtUnix > newest) newest = activity.fetchedAtUnix;
        if (activity.fromCache) cached = true;
    }
    std::string rightText = "Ultima consulta: " + FormatRelativeFromNow(newest, UnixNow());
    if (cached) rightText += " · datos en cache";
    DrawTextIn(g, Widen(rightText), *theme_.fontSmall,
               Gdiplus::RectF(pad, static_cast<float>(rcStatus_.top),
                              static_cast<float>(rcClient_.right) - pad * 2.0f,
                              static_cast<float>(rcStatus_.bottom - rcStatus_.top)),
               theme_.textFaint, Gdiplus::StringAlignmentFar, true, true);
}

void MainWindow::DrawProfileRow(Gdiplus::Graphics& g, const RECT& rect, const Profile& profile,
                                const ProfileActivity* activity, bool selected, bool hovered) {
    RECT bg = rect;
    InflateRect(&bg, -theme_.Px(6), -theme_.Px(3));
    const float radius = theme_.Pxf(8);
    if (selected) {
        FillRoundRect(g, RectFOf(bg), radius, theme_.cardSelected);
    } else if (hovered) {
        FillRoundRect(g, RectFOf(bg), radius, theme_.cardHover);
    } else {
        FillRoundRect(g, RectFOf(bg), radius, theme_.card);
    }

    const int pad = theme_.Px(8);
    const int avatarSize = theme_.Px(38);
    RECT avatar{bg.left + pad, bg.top + (bg.bottom - bg.top - avatarSize) / 2, 0, 0};
    avatar.right = avatar.left + avatarSize;
    avatar.bottom = avatar.top + avatarSize;

    const std::string key = "avatar_" + profile.steamid64;
    if (!profile.avatarUrl.empty()) EnsureImage(key, profile.avatarUrl);
    Gdiplus::Bitmap* bitmap = images_.Get(key);
    if (bitmap != nullptr) {
        DrawImageCover(g, bitmap, RectFOf(avatar), theme_.Pxf(avatarSize / 2.0f));
    } else {
        DrawImagePlaceholder(g, RectFOf(avatar), Initials(DisplayName(profile)),
                             *theme_.fontBodyBold, theme_.panelAlt, theme_.textFaint,
                             theme_.Pxf(avatarSize / 2.0f));
    }

    const int textLeft = avatar.right + pad;
    const int openWidth = theme_.Px(22);
    const int textRight = bg.right - pad - openWidth;

    DrawTextIn(g, Widen(DisplayName(profile)), *theme_.fontBodyBold,
               Gdiplus::RectF(static_cast<float>(textLeft), static_cast<float>(bg.top + theme_.Px(6)),
                              static_cast<float>(textRight - textLeft),
                              static_cast<float>(theme_.Px(18))),
               theme_.text, Gdiplus::StringAlignmentNear, false, true);

    const FetchStatus status = activity != nullptr ? activity->status : FetchStatus::NotFetched;
    std::string subtitle;
    if (activity != nullptr && (status == FetchStatus::Ok || status == FetchStatus::NoRecentGames)) {
        const int n = static_cast<int>(activity->games.size());
        subtitle = std::to_string(n) + (n == 1 ? " juego reciente" : " juegos recientes");
        if (activity->fromCache) subtitle += " · cache";
        subtitle += " · " + FormatRelativeFromNow(activity->fetchedAtUnix, UnixNow());
    } else {
        subtitle = FetchStatusLabel(status);
    }
    DrawTextIn(g, Widen(subtitle), *theme_.fontSmall,
               Gdiplus::RectF(static_cast<float>(textLeft), static_cast<float>(bg.top + theme_.Px(25)),
                              static_cast<float>(textRight - textLeft),
                              static_cast<float>(theme_.Px(16))),
               theme_.textDim, Gdiplus::StringAlignmentNear, false, true);

    if (activity != nullptr && status == FetchStatus::Ok) {
        int total = 0;
        for (const Game& game : activity->games) total += game.playtime2Weeks;
        const int level = static_cast<int>(ClassifyActivity(
            total, config_.prefs.moderateActivityMinutes, config_.prefs.highActivityMinutes));
        const int dot = theme_.Px(10);
        RECT dotRect{bg.right - pad - openWidth - dot, bg.top + (bg.bottom - bg.top - dot) / 2, 0, 0};
        dotRect.right = dotRect.left + dot;
        dotRect.bottom = dotRect.top + dot;
        FillRoundRect(g, RectFOf(dotRect), theme_.Pxf(5), theme_.LevelColor(level));
    }

    RECT openRect{bg.right - pad - openWidth, bg.top + (bg.bottom - bg.top - theme_.Px(20)) / 2, 0, 0};
    openRect.right = bg.right - pad;
    openRect.bottom = openRect.top + theme_.Px(20);
    const int i = AddHit(Hit::ProfileOpen, openRect, profile.steamid64);
    DrawTextIn(g, L"\x2197", *theme_.fontBody, RectFOf(openRect),
               i == hoverIndex_ ? theme_.accent : theme_.textFaint,
               Gdiplus::StringAlignmentCenter, true, false);
}

void MainWindow::DrawGameCard(Gdiplus::Graphics& g, const RECT& rect, const Game& game,
                              const std::string& ownerId, const std::string& ownerName,
                              bool showOthers, bool hovered) {
    (void)ownerName;
    const float radius = theme_.Pxf(8);
    FillRoundRect(g, RectFOf(rect), radius, hovered ? theme_.cardHover : theme_.card);
    StrokeRoundRect(g, RectFOf(rect), radius, theme_.border, 1.0f);

    const int pad = theme_.Px(12);
    const int coverW = theme_.Px(118);
    const int coverH = theme_.Px(56);
    RECT cover{rect.left + pad, rect.top + (rect.bottom - rect.top - coverH) / 2, 0, 0};
    cover.right = cover.left + coverW;
    cover.bottom = cover.top + coverH;
    if (config_.prefs.showCovers && game.appid != 0) {
        const std::string key = "cover_" + std::to_string(game.appid);
        EnsureImage(key, steam::CoverUrl(game.appid));
        Gdiplus::Bitmap* bitmap = images_.Get(key);
        if (bitmap != nullptr) {
            DrawImageCover(g, bitmap, RectFOf(cover), theme_.Pxf(6));
        } else {
            DrawImagePlaceholder(g, RectFOf(cover), Initials(game.name), *theme_.fontTitle,
                                 theme_.panelAlt, theme_.textFaint, theme_.Pxf(6));
        }
    } else {
        DrawImagePlaceholder(g, RectFOf(cover), Initials(game.name), *theme_.fontTitle,
                             theme_.panelAlt, theme_.textFaint, theme_.Pxf(6));
    }

    const int textLeft = cover.right + pad;
    const int openWidth = theme_.Px(84);
    const int textRight = rect.right - pad - openWidth - pad;

    DrawTextIn(g, Widen(game.name), *theme_.fontBodyBold,
               Gdiplus::RectF(static_cast<float>(textLeft), static_cast<float>(rect.top + theme_.Px(10)),
                              static_cast<float>(textRight - textLeft),
                              static_cast<float>(theme_.Px(20))),
               theme_.text, Gdiplus::StringAlignmentNear, false, true);

    const std::string recent = FormatMinutes(game.playtime2Weeks);
    const float recentWidth = MeasureTextWidth(g, Widen(recent), *theme_.fontTitle);
    DrawTextIn(g, Widen(recent), *theme_.fontTitle,
               Gdiplus::RectF(static_cast<float>(textLeft), static_cast<float>(rect.top + theme_.Px(33)),
                              recentWidth + 6.0f, static_cast<float>(theme_.Px(22))),
               theme_.accent, Gdiplus::StringAlignmentNear, false, false);
    DrawTextIn(g, L"en las ultimas 2 semanas", *theme_.fontSmall,
               Gdiplus::RectF(static_cast<float>(textLeft) + recentWidth + 10.0f,
                              static_cast<float>(rect.top + theme_.Px(33)),
                              static_cast<float>(textRight - textLeft) - recentWidth - 10.0f,
                              static_cast<float>(theme_.Px(22))),
               theme_.textDim, Gdiplus::StringAlignmentNear, true, true);

    const std::string meta = game.hasPlaytimeForever
                                 ? ("Total acumulado: " + FormatMinutes(game.playtimeForever))
                                 : "Total acumulado: --";
    DrawTextIn(g, Widen(meta), *theme_.fontSmall,
               Gdiplus::RectF(static_cast<float>(textLeft), static_cast<float>(rect.top + theme_.Px(62)),
                              static_cast<float>(textRight - textLeft),
                              static_cast<float>(theme_.Px(18))),
               theme_.textDim, Gdiplus::StringAlignmentNear, false, true);

    if (showOthers) {
        const int others = OthersRecentMinutes(game.appid, ownerId);
        if (others > 0) {
            DrawTextIn(g, Widen("Otros integrantes: " + FormatMinutes(others)), *theme_.fontSmall,
                       Gdiplus::RectF(static_cast<float>(textLeft), static_cast<float>(rect.top + theme_.Px(62)),
                                      static_cast<float>(textRight - textLeft),
                                      static_cast<float>(theme_.Px(18))),
                       theme_.warn, Gdiplus::StringAlignmentFar, false, true);
        }
    }

    RECT openRect{rect.right - pad - openWidth, rect.top + (rect.bottom - rect.top - theme_.Px(28)) / 2, 0, 0};
    openRect.right = rect.right - pad;
    openRect.bottom = openRect.top + theme_.Px(28);
    const int i = AddHit(Hit::CardOpen, openRect, std::to_string(game.appid));
    DrawButton(g, openRect, "Tienda", 0, i == hoverIndex_, i == pressedIndex_);
}

void MainWindow::DrawGeneralCard(Gdiplus::Graphics& g, const RECT& rect, uint32_t appid,
                                 const std::string& name, int totalRecent,
                                 const std::vector<std::pair<std::string, int>>& members,
                                 bool hovered) {
    const float radius = theme_.Pxf(8);
    FillRoundRect(g, RectFOf(rect), radius, hovered ? theme_.cardHover : theme_.card);
    StrokeRoundRect(g, RectFOf(rect), radius, theme_.border, 1.0f);

    const int pad = theme_.Px(12);
    const int coverW = theme_.Px(92);
    const int coverH = theme_.Px(44);
    RECT cover{rect.left + pad, rect.top + (rect.bottom - rect.top - coverH) / 2, 0, 0};
    cover.right = cover.left + coverW;
    cover.bottom = cover.top + coverH;
    if (config_.prefs.showCovers && appid != 0) {
        const std::string key = "cover_" + std::to_string(appid);
        EnsureImage(key, steam::CoverUrl(appid));
        Gdiplus::Bitmap* bitmap = images_.Get(key);
        if (bitmap != nullptr) DrawImageCover(g, bitmap, RectFOf(cover), theme_.Pxf(6));
        else DrawImagePlaceholder(g, RectFOf(cover), Initials(name), *theme_.fontTitle,
                                  theme_.panelAlt, theme_.textFaint, theme_.Pxf(6));
    } else {
        DrawImagePlaceholder(g, RectFOf(cover), Initials(name), *theme_.fontTitle, theme_.panelAlt,
                             theme_.textFaint, theme_.Pxf(6));
    }

    const int textLeft = cover.right + pad;
    const int right = rect.right - pad;

    DrawTextIn(g, Widen(name), *theme_.fontBodyBold,
               Gdiplus::RectF(static_cast<float>(textLeft), static_cast<float>(rect.top + pad),
                              static_cast<float>(right - textLeft),
                              static_cast<float>(theme_.Px(20))),
               theme_.text, Gdiplus::StringAlignmentNear, false, true);

    const std::string total = "Total reciente: " + FormatMinutes(totalRecent) + " · " +
                              std::to_string(members.size()) +
                              (members.size() == 1 ? " integrante" : " integrantes");
    DrawTextIn(g, Widen(total), *theme_.fontSmall,
               Gdiplus::RectF(static_cast<float>(textLeft), static_cast<float>(rect.top + pad + theme_.Px(22)),
                              static_cast<float>(right - textLeft),
                              static_cast<float>(theme_.Px(16))),
               theme_.textDim, Gdiplus::StringAlignmentNear, false, true);

    int maxMinutes = 0;
    for (const auto& member : members) maxMinutes = std::max(maxMinutes, member.second);

    float x = static_cast<float>(textLeft);
    const float chipY = static_cast<float>(rect.top + pad + theme_.Px(44));
    const float chipH = static_cast<float>(theme_.Px(22));
    for (const auto& [memberName, minutes] : members) {
        const std::string label = memberName + " · " + FormatMinutes(minutes);
        const float width = MeasureTextWidth(g, Widen(label), *theme_.fontSmall) +
                            static_cast<float>(theme_.Px(16));
        if (x + width > static_cast<float>(right)) break;
        const Gdiplus::RectF chip(x, chipY, width, chipH);
        const bool isMax = minutes == maxMinutes;
        FillRoundRect(g, chip, chipH / 2.0f, isMax ? theme_.accentSoft : theme_.panelAlt);
        DrawTextIn(g, Widen(label), *theme_.fontSmall, chip,
                   isMax ? theme_.accent : theme_.textDim, Gdiplus::StringAlignmentCenter, true,
                   true);
        x += width + static_cast<float>(theme_.Px(8));
    }
}

void MainWindow::DrawButton(Gdiplus::Graphics& g, const RECT& rect, const std::string& text,
                            int style, bool hovered, bool pressed, bool enabled) {
    Gdiplus::Color background = theme_.panelAlt;
    Gdiplus::Color foreground = theme_.text;
    switch (style) {
        case 1:
            background = theme_.accent;
            foreground = Gdiplus::Color(255, 16, 18, 22);
            break;
        case 2:
            background = hovered ? theme_.cardHover : theme_.panelAlt;
            break;
        default:
            background = hovered ? theme_.cardHover : theme_.panelAlt;
            break;
    }
    if (pressed && enabled) {
        background = Gdiplus::Color(
            background.GetA(),
            static_cast<BYTE>(std::max(0, background.GetR() - 18)),
            static_cast<BYTE>(std::max(0, background.GetG() - 18)),
            static_cast<BYTE>(std::max(0, background.GetB() - 18)));
    }
    if (!enabled) {
        background = theme_.panel;
        foreground = theme_.textFaint;
    }

    const float radius = theme_.Pxf(6);
    FillRoundRect(g, RectFOf(rect), radius, background);
    if (style != 1) {
        StrokeRoundRect(g, RectFOf(rect), radius, theme_.border, 1.0f);
    }
    DrawTextIn(g, Widen(text), *theme_.fontBody, RectFOf(rect), foreground,
               Gdiplus::StringAlignmentCenter, true, true);
}

void MainWindow::DrawChip(Gdiplus::Graphics& g, const RECT& rect, const std::string& text,
                          bool active, bool hovered) {
    const float radius = theme_.Pxf(6);
    Gdiplus::Color background = theme_.panelAlt;
    Gdiplus::Color foreground = theme_.textDim;
    if (active) {
        background = theme_.accentSoft;
        foreground = theme_.accent;
    } else if (hovered) {
        background = theme_.cardHover;
        foreground = theme_.text;
    }
    FillRoundRect(g, RectFOf(rect), radius, background);
    StrokeRoundRect(g, RectFOf(rect), radius, active ? theme_.accentDim : theme_.border, 1.0f);
    DrawTextIn(g, Widen(text), *theme_.fontBody, RectFOf(rect), foreground,
               Gdiplus::StringAlignmentCenter, true, true);
}

void MainWindow::DrawBadge(Gdiplus::Graphics& g, const RECT& rect, const std::string& text,
                           const Gdiplus::Color& color) {
    Gdiplus::Color background(static_cast<BYTE>(color.GetA() / 4), color.GetR(), color.GetG(),
                              color.GetB());
    FillRoundRect(g, RectFOf(rect), static_cast<float>(rect.bottom - rect.top) / 2.0f, background);
    DrawTextIn(g, Widen(text), *theme_.fontSmall, RectFOf(rect), color,
               Gdiplus::StringAlignmentCenter, true, true);
}

void MainWindow::DrawScrollbar(Gdiplus::Graphics& g, const RECT& area, int contentHeight,
                               int scroll) {
    const int viewHeight = area.bottom - area.top;
    if (viewHeight <= 0 || contentHeight <= viewHeight) return;
    const int barWidth = theme_.Px(6);
    const int x = area.right - barWidth - theme_.Px(2);
    const Gdiplus::RectF track(static_cast<float>(x), static_cast<float>(area.top + 2),
                               static_cast<float>(barWidth),
                               static_cast<float>(viewHeight - 4));
    FillRoundRect(g, track, barWidth / 2.0f, Gdiplus::Color(255, 30, 31, 38));

    int thumbHeight = static_cast<int>(static_cast<int64_t>(viewHeight) * viewHeight / contentHeight);
    thumbHeight = std::max(theme_.Px(30), thumbHeight);
    const int maxScroll = std::max(1, contentHeight - viewHeight);
    const int thumbY = area.top + static_cast<int>(static_cast<int64_t>(viewHeight - thumbHeight) *
                                                    scroll / maxScroll);
    const Gdiplus::RectF thumb(static_cast<float>(x), static_cast<float>(thumbY),
                               static_cast<float>(barWidth), static_cast<float>(thumbHeight));
    FillRoundRect(g, thumb, barWidth / 2.0f, theme_.borderLight);
}

void MainWindow::DrawEmptyState(Gdiplus::Graphics& g, const RECT& area, const std::string& title,
                                const std::string& subtitle) {
    const float centerY = static_cast<float>(area.top) +
                          static_cast<float>(area.bottom - area.top) * 0.4f;
    DrawTextIn(g, Widen(title), *theme_.fontBig,
               Gdiplus::RectF(static_cast<float>(area.left),
                              centerY - static_cast<float>(theme_.Px(20)),
                              static_cast<float>(area.right - area.left),
                              static_cast<float>(theme_.Px(26))),
               theme_.text, Gdiplus::StringAlignmentCenter, true, true);
    DrawTextIn(g, Widen(subtitle), *theme_.fontBody,
               Gdiplus::RectF(static_cast<float>(area.left) + static_cast<float>(theme_.Px(40)),
                              centerY + static_cast<float>(theme_.Px(10)),
                              static_cast<float>(area.right - area.left) -
                                  static_cast<float>(theme_.Px(80)),
                              static_cast<float>(theme_.Px(40))),
               theme_.textDim, Gdiplus::StringAlignmentCenter, true, true);
}

int MainWindow::AddHit(Hit kind, const RECT& rect, const std::string& param, int index) {
    HitRegion region;
    region.rect = rect;
    region.kind = kind;
    region.param = param;
    region.index = index;
    const int position = static_cast<int>(hits_.size());
    hits_.push_back(std::move(region));
    return position;
}

}  // namespace srp::ui
