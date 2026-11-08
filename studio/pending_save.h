#pragma once

#include <ArduinoJson.h>
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

// Loading an empty slot succeeds. A read error must never be mistaken for an empty slot.
#ifdef USE_ESP32
inline bool access_pending_slot(std::string &bytes, bool write) {
  nvs_handle_t handle;
  if (nvs_open("glaze_scale", NVS_READWRITE, &handle) != ESP_OK) return false;
  constexpr auto key = "pending_v1";
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
// The host preferences format uses a one-byte length. Keep the complete request
// in an atomic sibling file so a simulator restart exercises the same contract as NVS.
inline bool access_pending_slot(std::string &bytes, bool write) {
  namespace fs = std::filesystem;
  const char *dir = getenv("ESPHOME_PREFDIR");
  const char *home = getenv("HOME");
  if (!dir && !home) return false;
  fs::path root = dir ? fs::path(dir) : fs::path(home) / ".esphome" / "prefs";
  const auto path = root / (std::string(esphome::App.get_name().c_str()) + ".pending");
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

struct PendingSave {
  std::string path, body, name, tag;
  int number = 0;
  int displayed_g = 0;
  int gross_g = 0;

  bool setup() const { return path == "/device/jar-setups"; }
  bool active() const { return !path.empty(); }

  bool store() const {
    JsonDocument record;
    record["path"] = path;
    record["body"] = body;
    record["name"] = name;
    record["number"] = number;
    record["displayed_g"] = displayed_g;
    std::string bytes;
    serializeJson(record, bytes);
    return !record.overflowed() && access_pending_slot(bytes, true);
  }

  bool restore() {
    std::string bytes;
    if (!access_pending_slot(bytes, false)) return false;
    if (bytes.empty()) return true;
    JsonDocument record, payload;
    if (deserializeJson(record, bytes) || !record["body"].is<std::string>() ||
        deserializeJson(payload, record["body"].as<std::string>()) ||
        !payload["request_id"].is<std::string>() || !payload["gross_g"].is<double>() ||
        !record["name"].is<std::string>() || !record["number"].is<int>() || !record["displayed_g"].is<int>()) return false;
    PendingSave saved;
    saved.path = record["path"] | "";
    saved.number = record["number"].as<int>();
    if (saved.setup()) {
      if (saved.number != 0 || !payload["uid"].is<std::string>()) return false;
    } else {
      const std::string prefix = "/containers/", suffix = "/measurements";
      if (saved.number <= 0 || saved.path.size() <= prefix.size() + suffix.size() ||
          saved.path.compare(0, prefix.size(), prefix) || saved.path.find('/', prefix.size()) != saved.path.size() - suffix.size() ||
          saved.path.compare(saved.path.size() - suffix.size(), suffix.size(), suffix)) return false;
    }
    saved.tag = payload[saved.setup() ? "uid" : "tag_uid"] | "";
    saved.body = record["body"].as<std::string>();
    saved.name = record["name"].as<std::string>();
    saved.displayed_g = record["displayed_g"].as<int>();
    saved.gross_g = payload["gross_g"].as<int>();
    *this = std::move(saved);
    return true;
  }

  static bool erase() {
    std::string empty;
    return access_pending_slot(empty, true);
  }
};

}  // namespace glaze
