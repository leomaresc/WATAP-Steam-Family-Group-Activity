// Implementacion de los dialogos modales definidos en resources/app.rc.
#include "ui/dialogs.h"

#include <commctrl.h>
#include <shellapi.h>
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <memory>

#include "common/format.h"
#include "common/paths.h"
#include "resource.h"
#include "steam/http.h"
#include "steam/steam_api.h"
#include "storage/config.h"
#include "storage/credentials.h"
#include "ui/darkmode.h"
#include "ui/gfx.h"

namespace srp::ui {
namespace {

constexpr UINT kMsgKeyTestDone = WM_APP + 20;  // lParam: KeyTestPayload*

constexpr COLORREF kColBg = RGB(24, 25, 30);
constexpr COLORREF kColEdit = RGB(38, 40, 51);
constexpr COLORREF kColText = RGB(231, 233, 238);
constexpr COLORREF kColTextDim = RGB(155, 161, 176);
constexpr COLORREF kColBad = RGB(224, 108, 117);
constexpr COLORREF kColGood = RGB(123, 201, 111);
constexpr COLORREF kColAccent = RGB(102, 192, 244);
constexpr COLORREF kColWarn = RGB(224, 179, 65);

HBRUSH BgBrush() {
    static HBRUSH brush = CreateSolidBrush(kColBg);
    return brush;
}

HBRUSH EditBrush() {
    static HBRUSH brush = CreateSolidBrush(kColEdit);
    return brush;
}

std::wstring Widen(const std::string& s) { return Utf8ToWide(s); }
std::string Narrow(const std::wstring& s) { return WideToUtf8(s); }

Gdiplus::Color Darken(const Gdiplus::Color& c, int amount) {
    return Gdiplus::Color(
        c.GetA(),
        static_cast<BYTE>(std::max(0, static_cast<int>(c.GetR()) - amount)),
        static_cast<BYTE>(std::max(0, static_cast<int>(c.GetG()) - amount)),
        static_cast<BYTE>(std::max(0, static_cast<int>(c.GetB()) - amount)));
}

// Dibuja un boton propio (BS_OWNERDRAW) con el estilo oscuro de la aplicacion.
void DrawOwnerButton(LPDRAWITEMSTRUCT item) {
    if (item == nullptr) return;
    const bool disabled = (item->itemState & ODS_DISABLED) != 0;
    const bool pressed = (item->itemState & ODS_SELECTED) != 0;
    bool isDefault = (item->itemState & ODS_DEFAULT) != 0;
    // Los botones propios no llevan BS_DEFPUSHBUTTON, así que consultamos al
    // cuadro de dialogo cual es el boton predeterminado activo.
    if (const HWND parent = GetParent(item->hwndItem); parent != nullptr) {
        const LRESULT def = SendMessageW(parent, DM_GETDEFID, 0, 0);
        if (HIWORD(def) == DC_HASDEFID &&
            static_cast<int>(LOWORD(def)) == GetDlgCtrlID(item->hwndItem)) {
            isDefault = true;
        }
    }

    Gdiplus::Graphics g(item->hDC);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

    const int w = item->rcItem.right - item->rcItem.left;
    const int h = item->rcItem.bottom - item->rcItem.top;
    Gdiplus::SolidBrush background(Gdiplus::Color(255, 24, 25, 30));
    g.FillRectangle(&background, Gdiplus::Rect(item->rcItem.left, item->rcItem.top, w, h));

    Gdiplus::Color fill(255, 33, 35, 44);
    Gdiplus::Color border(255, 60, 64, 78);
    Gdiplus::Color text(255, 231, 233, 238);
    if (isDefault) {
        fill = Gdiplus::Color(255, 102, 192, 244);
        border = fill;
        text = Gdiplus::Color(255, 16, 18, 22);
    }
    if (pressed) fill = Darken(fill, 22);
    if (disabled) {
        fill = Gdiplus::Color(255, 27, 29, 36);
        border = Gdiplus::Color(255, 49, 52, 63);
        text = Gdiplus::Color(255, 110, 116, 132);
    }

    const Gdiplus::RectF rect(static_cast<float>(item->rcItem.left) + 1.0f,
                              static_cast<float>(item->rcItem.top) + 1.0f,
                              static_cast<float>(w) - 2.0f, static_cast<float>(h) - 2.0f);
    FillRoundRect(g, rect, 5.0f, fill);
    StrokeRoundRect(g, rect, 5.0f, border, 1.0f);

    const int length = GetWindowTextLengthW(item->hwndItem);
    std::wstring label(static_cast<size_t>(std::max(0, length)) + 1, L'\0');
    GetWindowTextW(item->hwndItem, label.data(), static_cast<int>(label.size()));
    label.resize(static_cast<size_t>(std::max(0, length)));

    const HFONT hfont = reinterpret_cast<HFONT>(SendMessageW(item->hwndItem, WM_GETFONT, 0, 0));
    const HFONT useFont =
        hfont != nullptr ? hfont : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    Gdiplus::Font font(item->hDC, useFont);
    DrawTextIn(g, label, font, rect, text, Gdiplus::StringAlignmentCenter, true, true);
}

std::string GetText(HWND hwnd, int id) {
    const int len = GetWindowTextLengthW(GetDlgItem(hwnd, id));
    if (len <= 0) return std::string();
    std::wstring buffer(static_cast<size_t>(len) + 1, L'\0');
    GetDlgItemTextW(hwnd, id, buffer.data(), static_cast<int>(buffer.size()));
    buffer.resize(static_cast<size_t>(len));
    return Narrow(buffer);
}

void SetText(HWND hwnd, int id, const std::string& text) {
    SetDlgItemTextW(hwnd, id, Widen(text).c_str());
}

// Colorea los controles comunes de un dialogo para el tema oscuro.
LRESULT DarkCtlColor(UINT msg, WPARAM wParam, LPARAM lParam, int statusId, COLORREF statusColor) {
    HDC dc = reinterpret_cast<HDC>(wParam);
    HWND control = reinterpret_cast<HWND>(lParam);
    switch (msg) {
        case WM_CTLCOLORDLG:
            return reinterpret_cast<LRESULT>(BgBrush());
        case WM_CTLCOLORSTATIC:
            if (statusId != 0 && control != nullptr && GetDlgCtrlID(control) == statusId) {
                SetTextColor(dc, statusColor);
            } else if (control != nullptr && GetDlgCtrlID(control) == IDC_KEY_LINK) {
                SetTextColor(dc, kColAccent);
            } else {
                SetTextColor(dc, kColText);
            }
            SetBkColor(dc, kColBg);
            return reinterpret_cast<LRESULT>(BgBrush());
        case WM_CTLCOLORBTN:
            SetTextColor(dc, kColText);
            SetBkColor(dc, kColBg);
            return reinterpret_cast<LRESULT>(BgBrush());
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX:
            SetTextColor(dc, kColText);
            SetBkColor(dc, kColEdit);
            return reinterpret_cast<LRESULT>(EditBrush());
        default:
            break;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Dialogo de integrante
// ---------------------------------------------------------------------------

struct ProfileCtx {
    std::string input;
    std::string alias;
    ProfileDialogResult result;
    COLORREF statusColor = kColTextDim;
};

INT_PTR CALLBACK ProfileDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* ctx = reinterpret_cast<ProfileCtx*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
        case WM_INITDIALOG: {
            ctx = reinterpret_cast<ProfileCtx*>(lParam);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(ctx));
            ApplyDarkModeToWindow(hwnd);
            SetText(hwnd, IDC_PROFILE_INPUT, ctx->input);
            SetText(hwnd, IDC_PROFILE_ALIAS, ctx->alias);
            SetText(hwnd, IDC_PROFILE_STATUS, "");
            SetFocus(GetDlgItem(hwnd, IDC_PROFILE_INPUT));
            SendMessageW(hwnd, DM_SETDEFID, IDOK, 0);
            return FALSE;  // ya se asigno el foco
        }
        case WM_COMMAND: {
            const int id = LOWORD(wParam);
            if (id == IDOK) {
                const std::string input = GetText(hwnd, IDC_PROFILE_INPUT);
                const std::string alias = GetText(hwnd, IDC_PROFILE_ALIAS);
                std::string value;
                const ProfileInputKind kind = ParseProfileInput(input, value);
                if (kind == ProfileInputKind::Empty) {
                    ctx->statusColor = kColWarn;
                    SetText(hwnd, IDC_PROFILE_STATUS,
                            "Introduce un SteamID64, una URL de perfil o un nombre de perfil.");
                    return TRUE;
                }
                if (kind == ProfileInputKind::Invalid) {
                    ctx->statusColor = kColBad;
                    SetText(hwnd, IDC_PROFILE_STATUS,
                            "El valor no parece un SteamID64 ni una URL de perfil valida.");
                    return TRUE;
                }
                ctx->result.accepted = true;
                ctx->result.input = input;
                ctx->result.alias = alias;
                EndDialog(hwnd, IDOK);
                return TRUE;
            }
            if (id == IDCANCEL) {
                EndDialog(hwnd, IDCANCEL);
                return TRUE;
            }
            break;
        }
        case WM_DRAWITEM:
            DrawOwnerButton(reinterpret_cast<LPDRAWITEMSTRUCT>(lParam));
            return TRUE;
        case WM_CTLCOLORDLG:
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX:
        case WM_CTLCOLORBTN:
            return DarkCtlColor(msg, wParam, lParam, IDC_PROFILE_STATUS,
                                ctx != nullptr ? ctx->statusColor : kColTextDim);
        case WM_CLOSE:
            EndDialog(hwnd, IDCANCEL);
            return TRUE;
        default:
            break;
    }
    return FALSE;
}

// ---------------------------------------------------------------------------
// Dialogo de ajustes
// ---------------------------------------------------------------------------

struct SettingsCtx {
    Preferences* prefs = nullptr;
    bool* clearCache = nullptr;
};

void UpdateAutoRefreshState(HWND hwnd) {
    const bool enabled = IsDlgButtonChecked(hwnd, IDC_PREF_AUTOREFRESH) == BST_CHECKED;
    EnableWindow(GetDlgItem(hwnd, IDC_PREF_AUTOREFRESH_MIN), enabled);
}

void UpdateHideLowState(HWND hwnd) {
    const bool enabled = IsDlgButtonChecked(hwnd, IDC_PREF_HIDE_LOW) == BST_CHECKED;
    EnableWindow(GetDlgItem(hwnd, IDC_PREF_LOW_MIN), enabled);
}

INT_PTR CALLBACK SettingsDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* ctx = reinterpret_cast<SettingsCtx*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
        case WM_INITDIALOG: {
            ctx = reinterpret_cast<SettingsCtx*>(lParam);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(ctx));
            ApplyDarkModeToWindow(hwnd);

            const Preferences& p = *ctx->prefs;
            CheckDlgButton(hwnd, IDC_PREF_AUTOREFRESH,
                           p.autoRefreshEnabled ? BST_CHECKED : BST_UNCHECKED);
            SetDlgItemInt(hwnd, IDC_PREF_AUTOREFRESH_MIN, static_cast<UINT>(p.autoRefreshMinutes),
                          FALSE);
            CheckDlgButton(hwnd, IDC_PREF_SHOW_COVERS, p.showCovers ? BST_CHECKED : BST_UNCHECKED);
            SetDlgItemInt(hwnd, IDC_PREF_MOD_MIN, static_cast<UINT>(p.moderateActivityMinutes),
                          FALSE);
            SetDlgItemInt(hwnd, IDC_PREF_HIGH_MIN, static_cast<UINT>(p.highActivityMinutes), FALSE);
            CheckDlgButton(hwnd, IDC_PREF_HIDE_LOW,
                           p.hideLowActivity ? BST_CHECKED : BST_UNCHECKED);
            SetDlgItemInt(hwnd, IDC_PREF_LOW_MIN, static_cast<UINT>(p.lowActivityFilterMinutes),
                          FALSE);
            UpdateAutoRefreshState(hwnd);
            UpdateHideLowState(hwnd);
            SendMessageW(hwnd, DM_SETDEFID, IDOK, 0);
            return TRUE;
        }
        case WM_COMMAND: {
            const int id = LOWORD(wParam);
            const int code = HIWORD(wParam);
            if (id == IDOK) {
                Preferences updated = *ctx->prefs;
                updated.autoRefreshEnabled =
                    IsDlgButtonChecked(hwnd, IDC_PREF_AUTOREFRESH) == BST_CHECKED;
                updated.autoRefreshMinutes =
                    static_cast<int>(GetDlgItemInt(hwnd, IDC_PREF_AUTOREFRESH_MIN, nullptr, FALSE));
                updated.showCovers =
                    IsDlgButtonChecked(hwnd, IDC_PREF_SHOW_COVERS) == BST_CHECKED;
                updated.moderateActivityMinutes =
                    static_cast<int>(GetDlgItemInt(hwnd, IDC_PREF_MOD_MIN, nullptr, FALSE));
                updated.highActivityMinutes =
                    static_cast<int>(GetDlgItemInt(hwnd, IDC_PREF_HIGH_MIN, nullptr, FALSE));
                updated.hideLowActivity =
                    IsDlgButtonChecked(hwnd, IDC_PREF_HIDE_LOW) == BST_CHECKED;
                updated.lowActivityFilterMinutes =
                    static_cast<int>(GetDlgItemInt(hwnd, IDC_PREF_LOW_MIN, nullptr, FALSE));

                AppConfig sanitizer;  // reutiliza la normalizacion de umbrales
                sanitizer.prefs = updated;
                srp::SanitizeConfig(sanitizer);
                const std::string keepSelection = updated.lastSelectedProfile;
                sanitizer.prefs.lastSelectedProfile = keepSelection;
                *ctx->prefs = sanitizer.prefs;
                EndDialog(hwnd, IDOK);
                return TRUE;
            }
            if (id == IDCANCEL) {
                EndDialog(hwnd, IDCANCEL);
                return TRUE;
            }
            if (id == IDC_PREF_CLEAR_CACHE) {
                const int answer = MessageBoxW(hwnd,
                                               L"Se borraran las respuestas y las imagenes guardadas "
                                               L"en disco. Los integrantes configurados se conservan. "
                                               L"¿Continuar?",
                                               L"Borrar cache local", MB_YESNO | MB_ICONQUESTION);
                if (answer == IDYES) {
                    *ctx->clearCache = true;
                    MessageBoxW(hwnd, L"La cache se borrara al guardar los ajustes.",
                                L"Borrar cache local", MB_OK | MB_ICONINFORMATION);
                }
                return TRUE;
            }
            if (id == IDC_PREF_AUTOREFRESH && code == BN_CLICKED) {
                UpdateAutoRefreshState(hwnd);
                return TRUE;
            }
            if (id == IDC_PREF_HIDE_LOW && code == BN_CLICKED) {
                UpdateHideLowState(hwnd);
                return TRUE;
            }
            break;
        }
        case WM_DRAWITEM:
            DrawOwnerButton(reinterpret_cast<LPDRAWITEMSTRUCT>(lParam));
            return TRUE;
        case WM_CTLCOLORDLG:
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX:
        case WM_CTLCOLORBTN:
            return DarkCtlColor(msg, wParam, lParam, 0, kColTextDim);
        case WM_CLOSE:
            EndDialog(hwnd, IDCANCEL);
            return TRUE;
        default:
            break;
    }
    return FALSE;
}

// ---------------------------------------------------------------------------
// Dialogo de clave de API
// ---------------------------------------------------------------------------

struct KeyTestPayload {
    bool ok = false;
    std::string message;
};

struct ApiKeyCtx {
    Worker* worker = nullptr;
    bool* changed = nullptr;
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>>(true);
    COLORREF statusColor = kColTextDim;
};

void SetKeyStatus(HWND hwnd, ApiKeyCtx* ctx, const std::string& text, COLORREF color) {
    ctx->statusColor = color;
    SetText(hwnd, IDC_KEY_STATUS, text);
    InvalidateRect(GetDlgItem(hwnd, IDC_KEY_STATUS), nullptr, TRUE);
}

INT_PTR CALLBACK ApiKeyDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* ctx = reinterpret_cast<ApiKeyCtx*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
        case WM_INITDIALOG: {
            ctx = reinterpret_cast<ApiKeyCtx*>(lParam);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(ctx));
            ApplyDarkModeToWindow(hwnd);
            if (creds::HasApiKey()) {
                SetKeyStatus(hwnd, ctx, "Hay una clave guardada en el Administrador de credenciales.",
                             kColGood);
            } else {
                SetKeyStatus(hwnd, ctx, "No hay ninguna clave guardada.", kColTextDim);
            }
            SendMessageW(hwnd, DM_SETDEFID, IDOK, 0);
            return TRUE;
        }
        case kMsgKeyTestDone: {
            auto* payload = reinterpret_cast<KeyTestPayload*>(lParam);
            if (payload != nullptr) {
                SetKeyStatus(hwnd, ctx, payload->message, payload->ok ? kColGood : kColBad);
                EnableWindow(GetDlgItem(hwnd, IDC_KEY_TEST), TRUE);
                delete payload;
            }
            return TRUE;
        }
        case WM_COMMAND: {
            const int id = LOWORD(wParam);
            if (id == IDOK) {
                EndDialog(hwnd, IDOK);
                return TRUE;
            }
            if (id == IDC_KEY_SAVE) {
                const std::string key = GetText(hwnd, IDC_KEY_EDIT);
                if (key.empty()) {
                    SetKeyStatus(hwnd, ctx, "Escribe una clave antes de guardar.", kColWarn);
                    return TRUE;
                }
                std::string error;
                if (creds::SaveApiKey(key, error)) {
                    *ctx->changed = true;
                    SetText(hwnd, IDC_KEY_EDIT, "");
                    SetKeyStatus(hwnd, ctx, "Clave guardada. Pulsa Probar para verificarla.",
                                 kColGood);
                } else {
                    SetKeyStatus(hwnd, ctx, error, kColBad);
                }
                return TRUE;
            }
            if (id == IDC_KEY_TEST) {
                std::string key = GetText(hwnd, IDC_KEY_EDIT);
                if (key.empty()) creds::LoadApiKey(key);
                if (key.empty()) {
                    SetKeyStatus(hwnd, ctx, "No hay ninguna clave para probar.", kColWarn);
                    return TRUE;
                }
                SetKeyStatus(hwnd, ctx, "Probando la clave con Steam...", kColTextDim);
                EnableWindow(GetDlgItem(hwnd, IDC_KEY_TEST), FALSE);

                HWND target = hwnd;
                auto alive = ctx->alive;
                ctx->worker->Post([target, key, alive]() {
                    srp::HttpClient http;
                    const srp::steam::FetchOutcome outcome = srp::steam::TestApiKey(http, key);
                    auto* payload = new KeyTestPayload();
                    payload->ok = outcome.status == srp::FetchStatus::Ok;
                    payload->message = outcome.message;
                    if (alive->load()) {
                        if (PostMessageW(target, kMsgKeyTestDone, 0,
                                         reinterpret_cast<LPARAM>(payload)) == FALSE) {
                            delete payload;
                        }
                    } else {
                        delete payload;
                    }
                });
                return TRUE;
            }
            if (id == IDC_KEY_DELETE) {
                std::string error;
                if (creds::DeleteApiKey(error)) {
                    *ctx->changed = true;
                    SetKeyStatus(hwnd, ctx, "La clave almacenada se ha eliminado.", kColTextDim);
                } else {
                    SetKeyStatus(hwnd, ctx, error, kColBad);
                }
                return TRUE;
            }
            if (id == IDC_KEY_LINK) {
                ShellExecuteW(hwnd, L"open", L"https://steamcommunity.com/dev/apikey", nullptr,
                              nullptr, SW_SHOWNORMAL);
                return TRUE;
            }
            if (id == IDCANCEL) {
                EndDialog(hwnd, IDCANCEL);
                return TRUE;
            }
            break;
        }
        case WM_DRAWITEM:
            DrawOwnerButton(reinterpret_cast<LPDRAWITEMSTRUCT>(lParam));
            return TRUE;
        case WM_CTLCOLORDLG:
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX:
        case WM_CTLCOLORBTN:
            return DarkCtlColor(msg, wParam, lParam, IDC_KEY_STATUS,
                                ctx != nullptr ? ctx->statusColor : kColTextDim);
        case WM_SETCURSOR: {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);
            RECT link;
            GetWindowRect(GetDlgItem(hwnd, IDC_KEY_LINK), &link);
            MapWindowPoints(HWND_DESKTOP, hwnd, reinterpret_cast<POINT*>(&link), 2);
            if (PtInRect(&link, pt)) {
                SetCursor(LoadCursorW(nullptr, IDC_HAND));
                return TRUE;
            }
            break;
        }
        case WM_DESTROY:
            if (ctx != nullptr) ctx->alive->store(false);
            return FALSE;
        case WM_CLOSE:
            EndDialog(hwnd, IDOK);
            return TRUE;
        default:
            break;
    }
    return FALSE;
}

// ---------------------------------------------------------------------------
// Dialogo Acerca de
// ---------------------------------------------------------------------------

INT_PTR CALLBACK AboutDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    (void)lParam;
    switch (msg) {
        case WM_INITDIALOG:
            ApplyDarkModeToWindow(hwnd);
            SendMessageW(hwnd, DM_SETDEFID, IDOK, 0);
            return TRUE;
        case WM_COMMAND:
            if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
                EndDialog(hwnd, LOWORD(wParam));
                return TRUE;
            }
            break;
        case WM_DRAWITEM:
            DrawOwnerButton(reinterpret_cast<LPDRAWITEMSTRUCT>(lParam));
            return TRUE;
        case WM_CTLCOLORDLG:
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX:
        case WM_CTLCOLORBTN:
            return DarkCtlColor(msg, wParam, lParam, 0, kColTextDim);
        case WM_CLOSE:
            EndDialog(hwnd, IDCANCEL);
            return TRUE;
        default:
            break;
    }
    return FALSE;
}

}  // namespace

ProfileDialogResult ShowProfileDialog(HWND parent, const std::string& input,
                                      const std::string& alias) {
    ProfileCtx ctx;
    ctx.input = input;
    ctx.alias = alias;
    DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_PROFILE), parent,
                    ProfileDlgProc, reinterpret_cast<LPARAM>(&ctx));
    return ctx.result;
}

bool ShowSettingsDialog(HWND parent, Preferences& prefs, bool& clearCacheRequested) {
    SettingsCtx ctx;
    ctx.prefs = &prefs;
    ctx.clearCache = &clearCacheRequested;
    const INT_PTR result = DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_SETTINGS),
                                           parent, SettingsDlgProc,
                                           reinterpret_cast<LPARAM>(&ctx));
    return result == IDOK;
}

void ShowApiKeyDialog(HWND parent, Worker& worker, bool& keyChanged) {
    ApiKeyCtx ctx;
    ctx.worker = &worker;
    ctx.changed = &keyChanged;
    DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_APIKEY), parent, ApiKeyDlgProc,
                    reinterpret_cast<LPARAM>(&ctx));
}

void ShowAboutDialog(HWND parent) {
    DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_ABOUT), parent, AboutDlgProc, 0);
}

}  // namespace srp::ui
