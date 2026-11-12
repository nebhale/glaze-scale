#pragma once

#include <cinttypes>

#include "inventory.h"
#include "pending_save.h"
#include "reading.h"

namespace glaze {

enum class Screen { Home, Setup, NewContainer, NewGlaze, NewManufacturer, Pick, Edit, Link };
enum class State { Idle, Lookup, Unknown, ManualSetup, Ready, Pending, Saved };
enum class Edit { Location, Cost, GlazeName, ContainerName, ContainerWeight, ProductCode, Cone, ProductLine, ManufacturerName };
struct GlazeDraft {
  std::string name, code, cone, line;
  Option manufacturer, classification;

  bool ready() const { return !name.empty(); }

  void write(JsonObject out) const {
    out["name"] = name;
    if (!manufacturer.id.empty()) out["manufacturer_id"] = manufacturer.id;
    if (!code.empty()) out["product_code"] = code;
    if (!cone.empty()) out["cone"] = cone;
    if (!line.empty()) out["product_line"] = line;
    if (!classification.id.empty()) out["classification"] = classification.id;
  }
};

class Station {
 public:
  Manager manager;
  Inventory inventory{manager};
  Reading reading;
  Screen screen = Screen::Home;
  State state = State::Idle;
  Jar jar, candidate;
  std::string uid, notice;
  List list = List::Glazes;
  Edit editor = Edit::Location;
  bool present = false;
  bool manual = false;
  Option glaze, preset;
  GlazeDraft draft, selected_draft;
  std::string manufacturer_name;
  std::string location, cost, container_name, container_weight;
  std::vector<Option> options;
  bool catalog_loading = false;
  bool form_busy = false;
  bool catalog_failed = false;
  int saved_number = 0;
  Grams saved_gross;
  std::string device;

  void restore_save() {
    slot_read_ = pending_.restore();
    if (!slot_read_) {
      state = State::Pending;
      notice = "Cannot read saved request";
      backoff();
      return;
    }
    slot_written_ = pending_.active();
    state = slot_written_ ? State::Pending : (present ? State::Lookup : State::Idle);
    notice = slot_written_ ? "Awaiting confirmation" : "";
    failures_ = 0;
    waiting_ = false;
  }

  const PendingSave &pending_save() const { return pending_; }

  void tag(const std::string &tag_value, uint32_t now) {
    std::string normalized;
    for (auto c : tag_value) {
      if (c == '-' || c == ':' || c == ' ') continue;
      if (!isxdigit(static_cast<unsigned char>(c))) { removed(now); return; }
      normalized += toupper(static_cast<unsigned char>(c));
    }
    if (normalized.size() < 4 || normalized.size() > 32) { removed(now); return; }
    if (!present && now - lost_at_ >= 650 && !uid.empty()) clear();
    if (normalized != uid) {
      clear();
      uid = normalized;
      if (state != State::Pending) state = State::Lookup;
      report_scan_ = true;
    }
    present = true;
  }
  void removed(uint32_t now) {
    if (present) { lost_at_ = now; reading.reset(); }
    present = false;
  }
  bool selected() const { return (manual || present) && jar.valid() && !jar.archived; }
  bool can_edit_setup() const { return state == State::ManualSetup || (present && state == State::Unknown); }
  bool has_glaze() const { return !glaze.label.empty(); }

  void platform_tared() {
    manual_load_seen_ = false;
    reading.reset();
  }

  void select_jar(const std::string &number) {
    if (present || !uid.empty() || state == State::Pending) return;
    clear_selection();
    if (number.empty()) return;
    const auto gross = reading.gross(esphome::millis());
    auto select = [this, number, gross](const Jar &detail) {
      if (!detail.valid() || detail.archived || std::to_string(detail.number) != number) {
        notice = "Jar unavailable. Choose a jar again.";
        return;
      }
      jar = detail;
      manual = true;
      manual_load_seen_ = gross >= load_detection_g || reading.gross(esphome::millis()) >= load_detection_g;
      state = State::Ready;
    };
    if (const Jar *cached = inventory.jar(number)) { select(*cached); return; }
    const auto session = session_;
    manager.request("GET", "/device/jars/" + url_part(number), {}, [this, session, select](const Reply &r) {
      if (session == session_ && !present && state == State::Idle)
        select(r.code == 200 ? Jar::read(r.data.as<JsonVariantConst>()) : Jar{});
    });
  }
  bool can_weigh(uint32_t now) const {
    auto gross = reading.gross(now);
    return (present || manual) && reading.stable(now) && gross && *gross >= 0 && *gross <= 2000;
  }
  bool can_create(uint32_t now) const {
    return can_edit_setup() && has_glaze() && can_weigh(now) &&
           (state != State::ManualSetup || reading.gross(now) >= load_detection_g);
  }
  bool below_tare(Grams gross) const {
    return gross && jar.tare && *gross < *jar.tare;
  }
  bool can_save(uint32_t now) const {
    const auto gross = reading.gross(now);
    if (!selected() || !can_weigh(now) || below_tare(gross)) return false;
    return state == State::Ready || (state == State::Saved && gross != saved_gross);
  }
  Grams net(Grams gross) const {
    if (!jar.tare || !gross) return {};
    return std::max(0., *gross - *jar.tare);
  }
  Grams display_weight(Grams gross) const {
    return jar.tare ? net(gross) : gross;
  }
  Grams percent(Grams gross) const {
    auto amount = display_weight(gross), starting = display_weight(jar.baseline);
    if (!amount || !starting || *starting <= 0) return {};
    return 100. * std::max(0., *amount) / *starting;
  }
  void tick(uint32_t now) {
    if (manual) {
      const auto gross = reading.gross(now);
      if (gross >= load_detection_g) manual_load_seen_ = true;
      if (state != State::ManualSetup && manual_load_seen_ && gross == 0 && reading.stable(now)) clear_selection();
    }
    if (!present && !uid.empty() && now - lost_at_ >= 650) clear();
    if (state == State::Pending) {
      if (!due(now)) return;
      if (!slot_read_) restore_save();
      else submit();
      return;
    }
    if (!present && !manual) return;
    if (catalog_loading && can_edit_setup() && (screen == Screen::Pick || screen == Screen::Edit)) {
      show_catalog();
    }
    if (!due(now)) return;
    bool awaiting_binding = state == State::Unknown && screen == Screen::Home;
    if (state == State::Lookup || awaiting_binding) lookup();
  }

  void confirm(uint32_t now) {
    if (!slot_read_ || pending_.active() || !can_weigh(now)) return;
    bool setup = screen == Screen::Setup && can_create(now);
    if (!setup && (screen != Screen::Home || !can_save(now))) return;
    JsonDocument data;
    char nonce[33];
    snprintf(nonce, sizeof nonce, "%08" PRIx32 "%08" PRIx32 "%08" PRIx32 "%08" PRIx32, esphome::random_uint32(),
             esphome::random_uint32(), esphome::random_uint32(), esphome::random_uint32());
    data["request_id"] = nonce;
    data["device_id"] = device;
    data["gross_g"] = *reading.gross(now);
    if (setup) {
      pending_.path = "/device/jar-setups";
      if (present) data["uid"] = uid;
      if (glaze.id.empty()) {
        selected_draft.write(data["new_glaze"].to<JsonObject>());
      } else data["glaze_id"] = glaze.id;
      if (!preset.id.empty()) data["preset_id"] = preset.id;
      if (!location.empty()) data["location"] = location;
      if (!cost.empty()) data["cost_usd"] = cost;
    } else {
      pending_.path = "/containers/" + url_part(jar.id) + "/measurements";
      if (manual) data["manual_selection"] = true;
      else data["tag_uid"] = uid;
      data["source"] = "device";
      data["expected_revision"] = jar.revision;
    }
    pending_.body = json_body(data);
    pending_.name = setup ? glaze.label : jar.name;
    pending_.number = setup ? 0 : jar.number;
    pending_.tag = uid;
    pending_.gross_g = data["gross_g"].as<int>();
    pending_.displayed_g = setup ? pending_.gross_g : static_cast<int>(*display_weight(pending_.gross_g));
    // The request ID is durable before the first POST, including a retry after power loss.
    slot_written_ = pending_.store();
    state = State::Pending;
    screen = Screen::Home;
    notice = slot_written_ ? "Awaiting confirmation" : "Cannot store save. Retrying.";
    waiting_ = false;
    failures_ = 0;
  }

  void begin_setup() {
    if (screen != Screen::Home || state == State::Pending) return;
    ++navigation_;
    if (uid.empty()) {
      clear_selection();
      manual = true;
      manual_load_seen_ = reading.gross(esphome::millis()) >= load_detection_g;
      state = State::ManualSetup;
    }
    if (!can_edit_setup()) return;
    screen = Screen::Setup;
    notice.clear();
  }
  void begin_glaze() {
    if (!can_edit_setup() || screen != Screen::Pick || list != List::Glazes) return;
    draft = selected_draft;
    screen = Screen::NewGlaze;
    notice.clear();
  }
  void use_glaze() {
    if (!can_edit_setup() || screen != Screen::NewGlaze || !draft.ready()) return;
    selected_draft = draft;
    glaze = {"", draft.name, draft.code};
    screen = Screen::Setup;
    notice.clear();
  }
  void begin_manufacturer() {
    if (!can_edit_setup() || screen != Screen::Pick || list != List::Manufacturers) return;
    manufacturer_name.clear();
    screen = Screen::NewManufacturer;
    notice.clear();
  }
  void create_manufacturer() {
    if (form_busy || !can_edit_setup() || screen != Screen::NewManufacturer || manufacturer_name.empty()) return;
    JsonDocument data;
    data["name"] = manufacturer_name;
    form_busy = true;
    const auto session = session_, navigation = navigation_;
    manager.request("POST", "/manufacturers", json_body(data), [this, session, navigation](const Reply &r) {
      form_busy = false;
      inventory.changed({List::Manufacturers});
      if (session != session_ || navigation != navigation_ || !can_edit_setup()) return;
      auto result = r.data.as<JsonVariantConst>();
      auto id = field(result, "id"), name = field(result, "name");
      if (r.code == 201 && !id.empty() && !name.empty()) {
        draft.manufacturer = {id, name, ""};
        screen = Screen::NewGlaze;
        notice.clear();
      } else if (r.code == 0 || r.code >= 500 || r.code == 201) {
        pick(List::Manufacturers, true);
        notice = "Check the list for your manufacturer.";
      } else {
        notice = "Could not create the manufacturer.";
      }
    });
  }
  void begin_container() {
    if (!can_edit_setup() || screen != Screen::Pick || list != List::Presets) return;
    container_name.clear();
    container_weight.clear();
    notice.clear();
    screen = Screen::NewContainer;
  }
  void create_container() {
    if (form_busy || !can_edit_setup() || screen != Screen::NewContainer || container_name.empty()) return;
    JsonDocument data;
    data["name"] = container_name;
    if (!container_weight.empty()) data["tare_g"] = std::strtol(container_weight.c_str(), nullptr, 10);
    form_busy = true;
    const auto session = session_, navigation = navigation_;
    manager.request("POST", "/presets", json_body(data), [this, session, navigation](const Reply &r) {
      form_busy = false;
      inventory.changed({List::Presets});
      if (session != session_ || navigation != navigation_ || !can_edit_setup()) return;
      auto result = r.data.as<JsonVariantConst>();
      auto id = field(result, "id"), name = field(result, "name");
      if (r.code == 201 && !id.empty() && !name.empty()) {
        preset = {id, name, ""};
        screen = Screen::Setup;
        notice.clear();
      } else if (r.code == 0 || r.code >= 500 || r.code == 201) {
        // The write may have succeeded. Reload the catalog before offering another create.
        pick(List::Presets, true);
        notice = "Check the list for your container.";
      } else {
        notice = "Could not create the container.";
      }
    });
  }
  void pick(List kind, bool require_fresh = false) {
    if (!can_edit_setup()) return;
    ++navigation_;
    catalog_loading = false;
    list = kind;
    switch (kind) {
      case List::Locations: edit(Edit::Location); break;
      case List::Cones: edit(Edit::Cone); break;
      case List::ProductLines: edit(Edit::ProductLine); break;
      default: screen = Screen::Pick; break;
    }
    options.clear();
    notice.clear();
    catalog_failed = false;
    if (kind == List::Classifications) {
      options = {{"glaze", "Glaze", ""}, {"underglaze", "Underglaze", ""}, {"other", "Other", ""}};
      catalog_loading = false;
    } else {
      const auto &e = inventory.open(kind);
      if (e.current && !require_fresh) options = e.current->rows;
      else catalog_loading = true;
    }
  }
  void choose(size_t index) {
    if (!can_edit_setup() || screen != Screen::Pick || index >= options.size()) return;
    const auto choice = options[index];
    if (list == List::Manufacturers || list == List::Classifications) {
      if (list == List::Manufacturers) draft.manufacturer = choice;
      else draft.classification = choice;
      screen = Screen::NewGlaze;
      notice.clear();
      return;
    }
    if (list == List::Glazes) { glaze = choice; selected_draft = {}; }
    if (list == List::Presets) preset = choice;
    screen = Screen::Setup;
    notice.clear();
  }
  void edit(Edit kind) {
    if (!can_edit_setup()) return;
    catalog_loading = false;
    ++navigation_;
    editor = kind;
    screen = Screen::Edit;
    notice.clear();
  }
  std::string *draft_text(Edit kind) {
    switch (kind) {
      case Edit::GlazeName: return &draft.name;
      case Edit::ProductCode: return &draft.code;
      case Edit::Cone: return &draft.cone;
      case Edit::ProductLine: return &draft.line;
      case Edit::ManufacturerName: return &manufacturer_name;
      default: return nullptr;
    }
  }
  void entered(const std::string &value) {
    if (!can_edit_setup() || screen != Screen::Edit) return;
    if (auto *text = draft_text(editor)) {
      const auto first = value.find_first_not_of(" \t\r\n");
      *text = first == std::string::npos ? "" : value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
      screen = editor == Edit::ManufacturerName ? Screen::NewManufacturer : Screen::NewGlaze;
      notice.clear();
      return;
    }
    if (editor == Edit::ContainerName || editor == Edit::ContainerWeight) {
      if (editor == Edit::ContainerName) {
        const auto first = value.find_first_not_of(" \t\r\n");
        container_name = first == std::string::npos ? "" : value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
      } else {
        if (!value.empty() && (value.find_first_not_of("0123456789") != std::string::npos ||
            value.size() > 8 || std::strtol(value.c_str(), nullptr, 10) > 10000000)) {
          notice = "Use 0 to 10000000 g"; return;
        }
        container_weight = value.empty() ? "" : std::to_string(std::strtol(value.c_str(), nullptr, 10));
      }
      screen = Screen::NewContainer;
      notice.clear();
      return;
    }
    if (editor == Edit::Location) location = value;
    if (editor == Edit::Cost) {
      auto dot = value.find('.');
      if (!value.empty() && (value.find_first_not_of("0123456789.") != std::string::npos ||
          value.find_first_of("0123456789") == std::string::npos ||
          (dot != std::string::npos && (value.find('.', dot + 1) != std::string::npos || value.size()-dot-1 > 2)) ||
          value.substr(0, dot).size() > 10)) { notice = "Enter an amount with up to two decimals"; return; }
      cost = value;
    }
    screen = Screen::Setup;
    notice.clear();
  }
  void select_unlinked_jar(const std::string &number) {
    if (!present || state != State::Unknown || screen != Screen::Home) return;
    const auto session = session_, choice = ++navigation_;
    manager.request("GET", "/device/jars/" + url_part(number), {}, [this, number, session, choice](const Reply &r) {
      if (session != session_ || choice != navigation_ || !present || state != State::Unknown || screen != Screen::Home) return;
      candidate = Jar::read(r.data.as<JsonVariantConst>());
      if (r.code != 200 || !candidate.valid() || candidate.archived || !candidate.tag.empty() ||
          std::to_string(candidate.number) != number) {
        notice = "Jar unavailable. Choose again.";
        return;
      }
      screen = Screen::Link;
      notice.clear();
    });
  }
  void bind() {
    if (form_busy || !present || state != State::Unknown || !candidate.valid() || screen != Screen::Link) return;
    JsonDocument data;
    data["container_id"] = candidate.id;
    data["expected_revision"] = candidate.revision;
    data["replace"] = false;
    data["move"] = false;
    form_busy = true;
    const auto session = session_;
    manager.request("PUT", "/tags/" + uid + "/binding", json_body(data), [this, session](const Reply &r) {
      form_busy = false;
      inventory.changed({List::Jars, List::Unlinked});
      if (session != session_ || !present) return;
      refresh(r.code == 200 ? "Tag linked. Review the reading." :
              r.code == 409 ? "Binding changed. Check the jar again." : "Checking tag after link attempt.");
    });
  }
  void back() {
    ++navigation_;
    notice.clear();
    catalog_loading = false;
    switch (screen) {
      case Screen::Setup:
        if (state == State::ManualSetup) clear_selection();
        else screen = Screen::Home;
        break;
      case Screen::Edit:
        if (editor == Edit::ManufacturerName) screen = Screen::NewManufacturer;
        else if (draft_text(editor)) screen = Screen::NewGlaze;
        else if (editor == Edit::ContainerName || editor == Edit::ContainerWeight) screen = Screen::NewContainer;
        else screen = Screen::Setup;
        break;
      case Screen::NewGlaze: pick(List::Glazes); break;
      case Screen::NewManufacturer: pick(List::Manufacturers); break;
      case Screen::NewContainer: pick(List::Presets); break;
      case Screen::Pick:
        if (list == List::Manufacturers || list == List::Classifications) screen = Screen::NewGlaze;
        else screen = Screen::Setup;
        break;
      default: screen = Screen::Home; break;
    }
  }

 private:
  void show_catalog() {
    const auto &entry = inventory.entry(list);
    if (entry.loading) return;
    catalog_loading = false;
    catalog_failed = entry.failed || !entry.current;
    if (catalog_failed) notice = screen == Screen::Edit ? "Suggestions unavailable" : "List unavailable";
    else options = entry.current->rows;
  }

  uint32_t session_ = 0, navigation_ = 0;
  bool lookup_busy_ = false, submitting_ = false;
  uint32_t lost_at_ = 0, scheduled_ = 0;
  unsigned failures_ = 0;
  bool waiting_ = false, report_scan_ = false;
  bool manual_load_seen_ = false;
  PendingSave pending_;
  bool slot_read_ = false, slot_written_ = false;
  bool due(uint32_t now) const { return !waiting_ || static_cast<int32_t>(now - scheduled_) >= 0; }
  void after(uint32_t delay_ms) { scheduled_ = esphome::millis() + delay_ms; waiting_ = true; }
  void backoff() { after(1000u << std::min(failures_, 3u)); if (failures_ < 3) ++failures_; }
  void clear() {
    reading.reset();
    clear_selection();
  }
  void clear_selection() {
    ++session_;
    lookup_busy_ = false;
    manual = false;
    manual_load_seen_ = false;
    uid.clear();
    screen = Screen::Home;
    jar = {}; candidate = {}; draft = {}; selected_draft = {}; manufacturer_name.clear(); glaze = {}; preset = {};
    location.clear(); cost.clear(); options.clear(); catalog_loading = false; catalog_failed = false;
    saved_gross.reset(); saved_number = 0;
    report_scan_ = false;
    if (pending_.active() || !slot_read_) {
      state = State::Pending;
    } else {
      state = State::Idle;
      notice.clear(); failures_ = 0; waiting_ = false;
    }
  }
  void refresh(const char *message) {
    if (uid.empty()) {
      clear();
      notice = message;
      return;
    }
    state = State::Lookup; screen = Screen::Home; jar = {}; candidate = {};
    notice = message;
    waiting_ = false; report_scan_ = false; failures_ = 0;
  }
  void lookup() {
    JsonDocument scan;
    scan["uid"] = uid; scan["device_id"] = device;
    if (lookup_busy_) return;
    lookup_busy_ = true;
    const auto session = session_;
    manager.request(report_scan_ ? "POST" : "GET", report_scan_ ? "/scans" : "/tags/" + uid,
        report_scan_ ? json_body(scan) : "", [this, session](const Reply &r) {
      if (session != session_) return;
      lookup_busy_ = false;
      if (!present || screen != Screen::Home || (state != State::Lookup && state != State::Unknown)) return;
      accept_lookup(r);
    });
  }
  void accept_lookup(const Reply &r) {
    auto root = r.data.as<JsonObjectConst>();
    if (r.code != 200 || field(root, "uid") != uid || root["container"].isUnbound() ||
        (!root["container"].isNull() && !root["container"].is<JsonObjectConst>())) {
      notice = "Manager unavailable. Retrying."; backoff(); return;
    }
    report_scan_ = false;
    if (r.data["container"].isNull()) {
      state = State::Unknown; after(3000);
    } else {
      jar = Jar::read(r.data["container"]);
      if (!jar.valid()) { refresh("Invalid jar response"); backoff(); return; }
      state = State::Ready; screen = Screen::Home; waiting_ = false;
      refresh_jar();
    }
    if (notice == "Manager unavailable. Retrying.") notice.clear();
    failures_ = 0;
  }
  void refresh_jar() {
    const auto session = session_;
    manager.request("GET", "/device/jars/" + std::to_string(jar.number), {},
        [this, session, id = jar.id, revision = jar.revision](const Reply &r) {
      if (r.code != 200 || session != session_ || jar.id != id || jar.revision != revision ||
          state == State::Pending || (!manual && !present)) return;
      auto detail = Jar::read(r.data.as<JsonVariantConst>());
      if (!detail.valid() || detail.id != id || (!manual && detail.tag != uid) || detail.revision != revision) {
        refresh("Jar changed. Review the new reading.");
        return;
      }
      jar = detail;
    });
  }
  void submit() {
    if (!slot_written_) {
      slot_written_ = pending_.store();
      if (!slot_written_) { notice = "Cannot store save. Retrying."; backoff(); return; }
    }
    if (submitting_) return;
    submitting_ = true;
    manager.request("POST", pending_.path, pending_.body, [this](const Reply &r) {
      submitting_ = false;
      accept_submission(r);
    });
  }
  void accept_submission(const Reply &r) {
    const bool setup = pending_.setup();
    const auto container_id = field(r.data.as<JsonVariantConst>(), "container_id");
    const bool confirmed = r.code == 200 && !field(r.data.as<JsonVariantConst>(), "measurement_id").empty() &&
        !container_id.empty() && (r.data["revision"] | 0) > 0 &&
        (setup ? (r.data["number"] | 0) > 0 : pending_.path == "/containers/" + url_part(container_id) + "/measurements") &&
        weight(r.data["stock"], "gross_g") == pending_.gross_g;
    // Only an explicit API rejection or a matching receipt releases the slot.
    const bool rejected = r.code == 400 || r.code == 404 || r.code == 409 || r.code == 410 || r.code == 422;
    if (!confirmed && !rejected) { notice = "Awaiting confirmation"; backoff(); return; }
    if (!PendingSave::erase()) { notice = "Cannot clear saved request. Retrying."; backoff(); return; }

    if (setup) inventory.changed({List::Jars, List::Unlinked, List::Glazes, List::Locations, List::Cones, List::ProductLines});
    else inventory.changed({List::Jars});
    const auto finished = std::move(pending_);
    pending_ = {};
    slot_written_ = false;
    failures_ = 0; waiting_ = false;
    const bool same_jar = manual || (present && uid == finished.tag);
    const int number = setup && confirmed ? r.data["number"].as<int>() : finished.number;
    if (!same_jar) {
      // A different tag may already be on the platform. Look it up after releasing the slot.
      if (present) state = State::Lookup;
      else clear_selection();
      if (confirmed) notice = (setup ? "Created jar #" : "Saved jar #") + std::to_string(number);
      else notice = (setup ? "New jar" : "Jar #" + std::to_string(number)) + " rejected. Review in Manager.";
      return;
    }
    if (rejected) {
      refresh(r.code == 409 ? "Jar changed. Review and confirm again." : "Save rejected. Review the jar.");
      return;
    }
    saved_number = number;
    saved_gross = weight(r.data["stock"], "gross_g");
    jar.id = container_id;
    jar.number = number;
    jar.name = finished.name;
    jar.revision = r.data["revision"].as<int>();
    if (setup) {
      if (*saved_gross > 0) jar.baseline = saved_gross;
      jar.maker = selected_draft.manufacturer.label;
      jar.code = glaze.code;
    }
    state = State::Saved; notice.clear();
    refresh_jar();
  }
};
}  // namespace glaze
