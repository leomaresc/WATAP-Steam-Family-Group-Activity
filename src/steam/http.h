// Cliente HTTP minimo sobre WinHTTP (HTTPS, tiempos de espera, sin redirecciones a HTTP).
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace srp {

enum class HttpError {
    None = 0,
    Timeout,
    Connection,     // no se pudo resolver/establecer la conexion
    Tls,
    TooManyRedirects,
    Unknown
};

struct HttpResponse {
    HttpError error = HttpError::None;
    int statusCode = 0;
    std::string body;
    std::string errorMessage;

    bool Ok() const { return error == HttpError::None && statusCode >= 200 && statusCode < 300; }
};

class HttpClient {
public:
    HttpClient();
    ~HttpClient();
    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;

    // Realiza una peticion GET. `url` debe ser https://...
    HttpResponse Get(const std::string& url,
                     const std::vector<std::pair<std::string, std::string>>& headers = {},
                     int timeoutMs = 15000);

    // Descarga el contenido de `url` a un archivo temporal y lo mueve a `destPath`.
    // Devuelve true si se guardo correctamente.
    bool DownloadToFile(const std::string& url, const std::wstring& destPath,
                        int timeoutMs, std::string& errorOut);

    void SetUserAgent(const std::string& userAgent) { userAgent_ = userAgent; }

private:
    std::string userAgent_;
};

}  // namespace srp
