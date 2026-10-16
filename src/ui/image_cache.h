// Cache de imagenes (portadas y avatares) en memoria y en disco.
// Las descargas se realizan en el hilo de trabajo; este objeto se usa en el hilo de interfaz.
#pragma once

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>

#include <filesystem>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>

namespace srp::ui {

class ImageCache {
public:
    ImageCache() = default;
    ~ImageCache();

    ImageCache(const ImageCache&) = delete;
    ImageCache& operator=(const ImageCache&) = delete;

    // Devuelve la imagen desde memoria o, si existe en disco, la carga. nullptr si no esta.
    Gdiplus::Bitmap* Get(const std::string& key);

    bool HasFile(const std::string& key) const;
    static std::filesystem::path PathFor(const std::string& key);

    // Marca una descarga en curso. Devuelve false si ya estaba pendiente o si ya existe.
    bool BeginDownload(const std::string& key);
    // Finaliza una descarga. Si tuvo exito, carga la imagen desde disco.
    void EndDownload(const std::string& key, bool success);

    // Guarda bytes en disco y carga la imagen en memoria (uso desde hilo de interfaz).
    bool StoreBytes(const std::string& key, const std::string& bytes);

    bool Failed(const std::string& key) const { return failed_.count(key) > 0; }
    size_t PendingCount() const { return pending_.size(); }

    void Clear();

private:
    bool LoadFromDisk(const std::string& key);

    struct Entry {
        Gdiplus::Bitmap* bitmap = nullptr;
        IStream* stream = nullptr;
    };

    std::unordered_map<std::string, Entry> images_;
    std::set<std::string> pending_;
    std::set<std::string> failed_;
};

}  // namespace srp::ui
