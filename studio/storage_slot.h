#pragma once

#include <string>

#ifdef USE_ESP32
#include <nvs.h>
#elif defined(USE_HOST)
#include "esphome/core/application.h"
#include <cerrno>
#include <cstdio>
#include <filesystem>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace glaze {

struct StorageSlot {
  const char *key;
  const char *file_suffix;

  // Missing records read as empty. A storage error returns false.
  bool access(std::string &bytes, bool write) const;
};

#ifdef USE_ESP32
inline bool StorageSlot::access(std::string &bytes, bool write) const {
  nvs_handle_t handle;
  if (nvs_open("glaze_scale", NVS_READWRITE, &handle) != ESP_OK) return false;
  esp_err_t err;
  if (write) {
    err = bytes.empty() ? nvs_erase_key(handle, key) : nvs_set_blob(handle, key, bytes.data(), bytes.size());
    if (err == ESP_ERR_NVS_NOT_FOUND && bytes.empty()) err = ESP_OK;
    if (err == ESP_OK) err = nvs_commit(handle);
  } else {
    size_t size = 0;
    err = nvs_get_blob(handle, key, nullptr, &size);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
      bytes.clear();
      err = ESP_OK;
    } else if (err == ESP_OK) {
      bytes.resize(size);
      err = size ? nvs_get_blob(handle, key, bytes.data(), &size) : ESP_FAIL;
    }
  }
  nvs_close(handle);
  return err == ESP_OK;
}
#elif defined(USE_HOST)
// Each host record has its own atomically replaced file, like a single NVS key.
inline bool StorageSlot::access(std::string &bytes, bool write) const {
  namespace fs = std::filesystem;
  const char *dir = getenv("ESPHOME_PREFDIR");
  const char *home = getenv("HOME");
  if (!dir && !home) return false;
  fs::path root = dir ? fs::path(dir) : fs::path(home) / ".esphome" / "prefs";
  const auto path = root / (std::string(esphome::App.get_name().c_str()) + file_suffix);
  if (!write) {
    FILE *file = fopen(path.c_str(), "rb");
    if (!file) return errno == ENOENT;
    char buffer[512];
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), file))) bytes.append(buffer, n);
    bool ok = !ferror(file) && !bytes.empty();
    fclose(file);
    return ok;
  }
  std::error_code ec;
  fs::create_directories(root, ec);
  if (ec) return false;
  if (bytes.empty()) {
    if (unlink(path.c_str()) && errno != ENOENT) return false;
  } else {
    const auto temp = path.string() + ".tmp";
    FILE *file = fopen(temp.c_str(), "wb");
    if (!file) return false;
    bool ok = fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size() && !fflush(file) && !fsync(fileno(file));
    if (fclose(file) || !ok || rename(temp.c_str(), path.c_str())) return false;
  }
  int dir_fd = open(root.c_str(), O_RDONLY);
  if (dir_fd < 0) return false;
  bool ok = fsync(dir_fd) == 0;
  close(dir_fd);
  return ok;
}
#endif

}  // namespace glaze
