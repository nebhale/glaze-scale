#pragma once

#include "storage_slot.h"
#include <ArduinoJson.h>
#include <string>

namespace glaze {

inline constexpr StorageSlot pending_slot{"pending_v1", ".pending"};

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
    return !record.overflowed() && pending_slot.access(bytes, true);
  }

  bool restore() {
    std::string bytes;
    if (!pending_slot.access(bytes, false)) return false;
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
    return pending_slot.access(empty, true);
  }
};

}  // namespace glaze
