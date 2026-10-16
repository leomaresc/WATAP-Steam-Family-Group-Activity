#include "steam/http.h"

#include <windows.h>
#include <winhttp.h>

#include <cstdio>
#include <filesystem>

#include "common/paths.h"

namespace srp {
namespace {

struct WinHttpHandle {
    HINTERNET handle = nullptr;
    WinHttpHandle() = default;
    explicit WinHttpHandle(HINTERNET h) : handle(h) {}
    ~WinHttpHandle() {
        if (handle) WinHttpCloseHandle(handle);
    }
    WinHttpHandle(const WinHttpHandle&) = delete;
    WinHttpHandle& operator=(const WinHttpHandle&) = delete;
    operator HINTERNET() const { return handle; }
    explicit operator bool() const { return handle != nullptr; }
};

HttpError MapWin32Error(DWORD code, std::string& messageOut) {
    switch (code) {
        case ERROR_WINHTTP_TIMEOUT:
            messageOut = "Tiempo de espera agotado";
            return HttpError::Timeout;
        case ERROR_WINHTTP_NAME_NOT_RESOLVED:
        case ERROR_WINHTTP_CANNOT_CONNECT:
        case ERROR_WINHTTP_CONNECTION_ERROR:
            messageOut = "No se pudo conectar con el servidor";
            return HttpError::Connection;
        case ERROR_WINHTTP_SECURE_FAILURE:
        case ERROR_WINHTTP_CLIENT_AUTH_CERT_NEEDED:
            messageOut = "Fallo de seguridad TLS";
            return HttpError::Tls;
        case ERROR_WINHTTP_REDIRECT_FAILED:
            messageOut = "Redireccion no permitida";
            return HttpError::TooManyRedirects;
        default:
            messageOut = "Error de red (" + std::to_string(code) + ")";
            return HttpError::Unknown;
    }
}

std::wstring Widen(const std::string& s) { return Utf8ToWide(s); }

}  // namespace

HttpClient::HttpClient() : userAgent_("SteamRecentPlay/1.0 (Windows; WinHTTP)") {}

HttpClient::~HttpClient() = default;

HttpResponse HttpClient::Get(const std::string& url,
                             const std::vector<std::pair<std::string, std::string>>& headers,
                             int timeoutMs) {
    HttpResponse result;
    if (url.rfind("https://", 0) != 0) {
        result.error = HttpError::Unknown;
        result.errorMessage = "Solo se permiten direcciones https";
        return result;
    }

    const std::wstring wurl = Widen(url);
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256] = {};
    wchar_t path[4096] = {};
    wchar_t extra[2048] = {};
    uc.lpszHostName = host;
    uc.dwHostNameLength = static_cast<DWORD>(std::size(host) - 1);
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = static_cast<DWORD>(std::size(path) - 1);
    uc.lpszExtraInfo = extra;
    uc.dwExtraInfoLength = static_cast<DWORD>(std::size(extra) - 1);

    if (!WinHttpCrackUrl(wurl.c_str(), static_cast<DWORD>(wurl.size()), 0, &uc)) {
        result.error = HttpError::Unknown;
        result.errorMessage = "URL no valida";
        return result;
    }
    if (uc.nScheme != INTERNET_SCHEME_HTTPS) {
        result.error = HttpError::Unknown;
        result.errorMessage = "Solo se admite HTTPS";
        return result;
    }

    WinHttpHandle session(WinHttpOpen(Widen(userAgent_).c_str(),
                                      WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                      WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session) {
        result.error = MapWin32Error(GetLastError(), result.errorMessage);
        return result;
    }

    WinHttpSetTimeouts(session, timeoutMs, timeoutMs, timeoutMs, timeoutMs);

    WinHttpHandle connect(WinHttpConnect(session, host, uc.nPort, 0));
    if (!connect) {
        result.error = MapWin32Error(GetLastError(), result.errorMessage);
        return result;
    }

    std::wstring target = path;
    target += extra;
    DWORD flags = WINHTTP_FLAG_SECURE;
    WinHttpHandle request(WinHttpOpenRequest(connect, L"GET", target.c_str(), nullptr,
                                             WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                             flags));
    if (!request) {
        result.error = MapWin32Error(GetLastError(), result.errorMessage);
        return result;
    }

    for (const auto& [name, value] : headers) {
        std::wstring headerLine = Widen(name) + L": " + Widen(value);
        WinHttpAddRequestHeaders(request, headerLine.c_str(),
                                 static_cast<DWORD>(-1), WINHTTP_ADDREQ_FLAG_ADD);
    }

    if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        result.error = MapWin32Error(GetLastError(), result.errorMessage);
        return result;
    }

    if (!WinHttpReceiveResponse(request, nullptr)) {
        result.error = MapWin32Error(GetLastError(), result.errorMessage);
        return result;
    }

    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    if (WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize,
                            WINHTTP_NO_HEADER_INDEX)) {
        result.statusCode = static_cast<int>(statusCode);
    }

    // Lectura del cuerpo por bloques con limite defensivo (8 MB).
    constexpr size_t kMaxBody = 8u * 1024u * 1024u;
    std::string body;
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request, &available)) {
            result.error = MapWin32Error(GetLastError(), result.errorMessage);
            return result;
        }
        if (available == 0) break;
        if (body.size() + available > kMaxBody) {
            break;
        }
        const size_t offset = body.size();
        body.resize(offset + available);
        DWORD read = 0;
        if (!WinHttpReadData(request, body.data() + offset, available, &read)) {
            result.error = MapWin32Error(GetLastError(), result.errorMessage);
            return result;
        }
        body.resize(offset + read);
        if (read == 0) break;
    }
    result.body = std::move(body);
    return result;
}

bool HttpClient::DownloadToFile(const std::string& url, const std::wstring& destPath,
                                int timeoutMs, std::string& errorOut) {
    const HttpResponse response = Get(url, {}, timeoutMs);
    if (!response.Ok()) {
        errorOut = response.error != HttpError::None ? response.errorMessage
                                                     : ("HTTP " + std::to_string(response.statusCode));
        return false;
    }
    if (response.body.empty()) {
        errorOut = "Respuesta vacia";
        return false;
    }
    std::filesystem::path dest(destPath);
    if (!WriteFileBytesAtomic(dest, response.body)) {
        errorOut = "No se pudo escribir el archivo";
        return false;
    }
    return true;
}

}  // namespace srp
