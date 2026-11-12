#pragma once

#include "manager.h"
#include "reading.h"
#include <map>

namespace glaze {

inline std::string field(JsonVariantConst data, const char *key) {
  return data[key] | "";
}

inline Grams weight(JsonVariantConst data, const char *key) {
  if (data[key].is<double>()) return data[key].as<double>();
  return {};
}

struct Jar {
  std::string id, name, maker, code, tag;
  int number = 0, revision = 0;
  bool archived = false;
  Grams tare, baseline, threshold;

  static Jar read(JsonVariantConst j) {
    return {field(j, "container_id"), field(j, "glaze_name"), field(j, "manufacturer_name"),
            field(j, "product_code"), field(j, "tag_uid"),
            j["number"] | 0, j["revision"] | 0, j["archived"] | false, weight(j, "tare_g"),
            weight(j, "full_weight_g"), weight(j, "threshold_g")};
  }
  bool valid() const { return !id.empty() && number > 0 && !name.empty() && revision > 0; }
};

enum class List { Manufacturers, Glazes, Presets, Locations, Cones, ProductLines, Classifications, Jars, Unlinked };
struct Option { std::string id, label, code; };

struct Choices {
  std::vector<Option> rows;
  std::map<std::string, Jar> jars;
};

struct CatalogEntry {
  std::shared_ptr<const Choices> current;
  uint32_t loaded_at = 0, generation = 0;
  bool loading = false, background = false, failed = false, invalidated = false;
};

class Inventory {
 public:
  explicit Inventory(Manager &manager) : manager_(manager) {}
  const CatalogEntry &entry(List list) const { return entries_.at(list); }

  const CatalogEntry &open(List list, bool background = false) {
    auto &e = entries_[list];
    if (e.loading) {
      if (!background) {
        e.background = false;
        manager_.promote(options_path(list));
      }
    } else if (!e.current || e.invalidated || esphome::millis() - e.loaded_at >= 60000) {
      start(list, background);
    }
    return e;
  }

  const Jar *jar(const std::string &number) const {
    auto it = entries_.find(List::Jars);
    if (it == entries_.end() || it->second.invalidated || !it->second.current) return nullptr;
    auto &jars = it->second.current->jars;
    auto j = jars.find(number);
    return j == jars.end() ? nullptr : &j->second;
  }

  void changed(std::initializer_list<List> lists) {
    manager_.separate_reads();
    for (auto list : lists) {
      auto it = entries_.find(list);
      if (it == entries_.end()) continue;
      it->second.invalidated = true;
      start(list, true);
    }
  }

 private:
  Manager &manager_;
  std::map<List, CatalogEntry> entries_;
  static std::string options_path(List list) {
    static constexpr const char *names[] = {"manufacturers","glazes","presets","locations","cones",
        "product-lines","classifications","jars","unlinked-jars"};
    return std::string("/device/options/") + names[static_cast<size_t>(list)] + "?";
  }

  void start(List list, bool background) {
    auto &e = entries_[list];
    e.loading = true;
    e.background = background;
    page(list, ++e.generation, 0, std::make_shared<Choices>());
  }

  void page(List list, uint32_t generation, int offset, std::shared_ptr<Choices> next_page) {
    auto path = options_path(list) + (list == List::Jars ? "details=true&" : "") + "offset=" + std::to_string(offset);
    manager_.request("GET", path, {}, [this, list, generation, offset, next_page](const Reply &r) {
      auto &entry = entries_.at(list);
      if (generation != entry.generation) return;
      auto items = r.data["items"].as<JsonArrayConst>();
      auto next = r.data["next_offset"];
      if (r.code != 200 || items.isNull() || (!next.isNull() && (!next.is<int>() || next.as<int>() <= offset))) {
        entry.loading = false;
        entry.failed = true;
        return;
      }
      for (auto item : items) {
        auto number = field(item, "value");
        next_page->rows.push_back({number, field(item, "label"), field(item, "code")});
        if (list == List::Jars && item["detail"].is<JsonObjectConst>()) {
          auto detail = Jar::read(item["detail"]);
          if (detail.valid() && !detail.archived && std::to_string(detail.number) == number)
            next_page->jars.emplace(number, std::move(detail));
        }
      }
      if (!next.isNull()) {
        page(list, generation, next.as<int>(), next_page);
      } else {
        entry.failed = entry.invalidated = false;
        entry.current = next_page;
        entry.loaded_at = esphome::millis();
        entry.loading = false;
      }
    }, entries_.at(list).background);
  }
};

}  // namespace glaze
