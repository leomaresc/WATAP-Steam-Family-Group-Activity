#include "ui/image_cache.h"

#include <shlwapi.h>

#include "common/paths.h"

namespace srp::ui {

ImageCache::~ImageCache() { Clear(); }

std::filesystem::path ImageCache::PathFor(const std::string& key) {
    return ImageCacheDir() / (HashKey(key) + ".img");
}

bool ImageCache::HasFile(const std::string& key) const {
    std::error_code ec;
    return std::filesystem::exists(PathFor(key), ec) &&
           std::filesystem::file_size(PathFor(key), ec) > 0;
}

bool ImageCache::LoadFromDisk(const std::string& key) {
    std::string bytes;
    if (!ReadFileBytes(PathFor(key), bytes) || bytes.empty()) return false;

    IStream* stream = SHCreateMemStream(reinterpret_cast<const BYTE*>(bytes.data()),
                                        static_cast<UINT>(bytes.size()));
    if (stream == nullptr) return false;

    Gdiplus::Bitmap* bitmap = Gdiplus::Bitmap::FromStream(stream);
    if (bitmap == nullptr || bitmap->GetLastStatus() != Gdiplus::Ok) {
        if (bitmap != nullptr) delete bitmap;
        stream->Release();
        return false;
    }

    Entry entry;
    entry.bitmap = bitmap;
    entry.stream = stream;  // se libera al descartar la entrada
    images_[key] = entry;
    return true;
}

Gdiplus::Bitmap* ImageCache::Get(const std::string& key) {
    auto it = images_.find(key);
    if (it != images_.end()) return it->second.bitmap;
    if (failed_.count(key) > 0) return nullptr;
    if (LoadFromDisk(key)) {
        return images_[key].bitmap;
    }
    return nullptr;
}

bool ImageCache::BeginDownload(const std::string& key) {
    if (images_.count(key) > 0) return false;
    if (pending_.count(key) > 0) return false;
    if (failed_.count(key) > 0) return false;
    pending_.insert(key);
    return true;
}

void ImageCache::EndDownload(const std::string& key, bool success) {
    pending_.erase(key);
    if (!success) {
        failed_.insert(key);
        return;
    }
    LoadFromDisk(key);
}

bool ImageCache::StoreBytes(const std::string& key, const std::string& bytes) {
    if (bytes.empty()) return false;
    if (!EnsureDataDirs()) return false;
    if (!WriteFileBytesAtomic(PathFor(key), bytes)) return false;
    pending_.erase(key);
    failed_.erase(key);
    LoadFromDisk(key);
    return true;
}

void ImageCache::Clear() {
    for (auto& [key, entry] : images_) {
        if (entry.bitmap != nullptr) delete entry.bitmap;
        if (entry.stream != nullptr) entry.stream->Release();
    }
    images_.clear();
    pending_.clear();
    failed_.clear();
}

}  // namespace srp::ui
