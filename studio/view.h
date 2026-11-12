#pragma once

#include "calibration.h"
#include "palettes.h"
#include "station.h"

namespace glaze {

inline Station station;

struct JarHeading {
  lv_obj_t *title, *number, *maker;
};

struct NewForm {
  lv_obj_t *page, *create, *notice;
};

struct Widgets {
  lv_obj_t *home, *menu, *edit, *wifi_indicator, *wifi, *wifi_warning, *service, *service_icon, *service_warning, *manager_dialog;
  lv_obj_t *amount, *basis, *status, *bar;
  lv_obj_t *tare_button, *save_button, *jar_picker;
  NewForm new_jar, new_glaze, new_manufacturer, new_container;
  lv_obj_t *menu_title, *body, *menu_action, *action_label, *edit_title, *edit_context, *edit_caption, *entry, *suggestions, *keys;
  lv_obj_t *link_card;
  JarHeading home_heading, link_heading;
  lv_style_t *panel, *control_corners;
};

inline void label(lv_obj_t *obj, const std::string &text) {
  if (text != lv_label_get_text(obj)) lv_label_set_text(obj, text.c_str());
}
inline void visible(lv_obj_t *obj, bool show) { lv_obj_set_flag(obj, LV_OBJ_FLAG_HIDDEN, !show); }
inline void button(lv_obj_t *obj, lv_obj_t *caption, const std::string &text, bool enabled = true) {
  visible(obj, !text.empty());
  label(caption, text);
  lv_obj_set_state(obj, LV_STATE_DISABLED, !enabled);
}

class View {
 public:
  bool settings_open = false;
  bool needs_calibration = false;

  void attach(Widgets widgets) {
    w = widgets;
    lv_dropdown_set_text_static(w.jar_picker, "");
  }
  void update_connections(bool wifi_connected) {
    if (!w.home) return;
    visible(w.wifi_warning, !wifi_connected);
    lv_obj_set_state(w.wifi, LV_STATE_CHECKED, !wifi_connected);
    lv_obj_set_flag(w.wifi_indicator, LV_OBJ_FLAG_CLICKABLE, !wifi_connected);
    const auto status = station.manager.status;
    const bool unavailable = status == ServiceStatus::Unavailable;
    lv_obj_set_state(w.service_icon, LV_STATE_DISABLED, !unavailable && (!wifi_connected || status == ServiceStatus::Unchecked));
    lv_obj_set_state(w.service_icon, LV_STATE_CHECKED, unavailable);
    visible(w.service_warning, unavailable);
    lv_obj_set_flag(w.service, LV_OBJ_FLAG_CLICKABLE, unavailable);
    if (!unavailable || station.screen != Screen::Home || settings_open) visible(w.manager_dialog, false);
  }
  void render(uint32_t now) {
    if (!w.home || settings_open) return;
    const auto *form = current_form();
    bool entered = !previous_ || *previous_ != station.screen;
    if (entered) {
      auto page = form ? form->page : station.screen == Screen::Home ? w.home : station.screen == Screen::Edit ? w.edit : w.menu;
      if (lv_screen_active() != page) lv_screen_load(page);
      previous_ = station.screen;
      dirty_ = true;
    }
    if (form) {
      lv_obj_set_state(form->create, LV_STATE_DISABLED, !can_create_form(now));
      visible(form->notice, !station.notice.empty());
      label(form->notice, station.notice);
    } else if (station.screen == Screen::Home) home(now);
    else if (station.screen == Screen::Edit) render_edit(entered);
    else menu();
    shown_jar_id_ = station.jar.id;
    shown_uid_ = station.uid;
    shown_revision_ = station.jar.revision;
    shown_state_ = station.state;
    shown_gross_ = station.reading.gross(now);
    dirty_ = false;
  }
  void tare(Calibration &calibration) {
    auto mean = station.reading.settled_mean(esphome::millis());
    auto &[zero, counts_per_gram] = calibration;
    if (!mean || !steady_home_reading() || counts_per_gram == 0) return;
    const float next_zero = zero + *mean * counts_per_gram;
    if (!std::isfinite(next_zero)) return;
    zero = next_zero;
    station.platform_tared();
    render(esphome::millis());
  }
  void save() {
    if (steady_home_reading() && same_session()) confirm();
  }
  void load_jars() {
    if (!can_choose_jar() || refreshing_jar_list_) return;
    picker_uid_ = station.uid;
    const auto &entry = station.inventory.open(picker_list());
    waiting_for_jars_ = !entry.current;
    if (entry.current) display_jars(entry);
    else {
      jar_numbers_.clear();
      lv_dropdown_set_options(w.jar_picker, "Loading jars...");
      lv_dropdown_set_selected_highlight(w.jar_picker, false);
    }
  }
  void choose_jar() {
    if (!can_choose_jar() || picker_uid_ != station.uid || !matches_screen() || waiting_for_jars_) return;
    size_t i = lv_dropdown_get_selected(w.jar_picker);
    if (i >= jar_numbers_.size()) return;
    if (jar_numbers_[i] == "retry") lv_dropdown_open(w.jar_picker);
    else if (jar_numbers_[i] == "new") station.begin_setup();
    else if (station.state == State::Unknown) station.select_unlinked_jar(jar_numbers_[i]);
    else {
      station.select_jar(jar_numbers_[i]);
      // Give LVGL the cached heading and reading before its next display refresh.
      render(esphome::millis());
    }
  }
  void menu_action() {
    if (!same_session()) return;
    if (station.screen == Screen::Setup) confirm();
    else if (station.screen == Screen::NewGlaze) station.use_glaze();
    else if (station.screen == Screen::NewManufacturer) station.create_manufacturer();
    else if (station.screen == Screen::NewContainer) station.create_container();
    else if (station.screen == Screen::Link) station.bind();
    dirty_ = true;
  }
  void pick(List list) { if (same_session()) station.pick(list); }
  void edit(Edit field) { if (same_session()) station.edit(field); }
  void back() { station.back(); dirty_ = true; }
  void finish_edit() {
    if (same_session()) station.entered(lv_textarea_get_text(w.entry));
    dirty_ = true;
  }
 private:
  Widgets w{};
  std::optional<Screen> previous_;
  bool dirty_ = true;
  std::string shown_uid_, shown_jar_id_, shown_jar_name_, shown_link_name_;
  std::vector<std::string> jar_numbers_;
  std::string jar_options_, picker_uid_, suggestion_prefix_;
  size_t suggestion_count_ = 0;
  bool catalog_loading_ = false;
  bool waiting_for_jars_ = false;
  bool refreshing_jar_list_ = false;
  State shown_state_ = State::Idle;
  int shown_revision_ = 0, y_ = 0;
  Grams shown_gross_;
  enum class Row { Option, NewGlaze, NewContainer, NewManufacturer, ClearClassification,
                   ClearManufacturer, ClearPreset, Reload };

  const NewForm *current_form() const {
    switch (station.screen) {
      case Screen::Setup: return &w.new_jar;
      case Screen::NewGlaze: return &w.new_glaze;
      case Screen::NewManufacturer: return &w.new_manufacturer;
      case Screen::NewContainer: return &w.new_container;
      default: return nullptr;
    }
  }
  bool can_create_form(uint32_t now) const {
    switch (station.screen) {
      case Screen::Setup: return station.can_create(now);
      case Screen::NewGlaze: return station.can_edit_setup() && station.draft.ready();
      case Screen::NewManufacturer: return !station.form_busy && station.can_edit_setup() && !station.manufacturer_name.empty();
      case Screen::NewContainer: return !station.form_busy && station.can_edit_setup() && !station.container_name.empty();
      default: return false;
    }
  }
  struct Click { View *owner; Row row; size_t index; };
  std::vector<Click> clicks_;

  void add_jar_option(const std::string &number, std::string caption) {
    std::replace_if(caption.begin(), caption.end(), [](char c) { return c == '\n' || c == '\r'; }, ' ');
    if (!jar_numbers_.empty()) jar_options_ += '\n';
    jar_options_ += caption;
    jar_numbers_.push_back(number);
  }
  List picker_list() const { return station.state == State::Unknown ? List::Unlinked : List::Jars; }
  void display_jars(const CatalogEntry &entry) {
    jar_numbers_.clear();
    jar_options_.clear();
    if (station.manual) add_jar_option("", "Clear selection");
    if (entry.current) {
      for (const auto &option : entry.current->rows) add_jar_option(option.id, option.label);
    } else add_jar_option("retry", "Jars unavailable. Retry");
    add_jar_option("new", picker_list() == List::Unlinked ? "New jar..." : "Create jar...");
    waiting_for_jars_ = false;
    lv_dropdown_set_options(w.jar_picker, jar_options_.c_str());
    jar_options_.clear();
    auto selected = std::find(jar_numbers_.begin(), jar_numbers_.end(), std::to_string(station.jar.number));
    bool found = station.manual && selected != jar_numbers_.end();
    lv_dropdown_set_selected(w.jar_picker, found ? selected - jar_numbers_.begin() : 0);
    lv_dropdown_set_selected_highlight(w.jar_picker, found);
    // LVGL measures and positions the open list when it receives the open event.
    refreshing_jar_list_ = true;
    lv_dropdown_open(w.jar_picker);
    refreshing_jar_list_ = false;
  }
  bool can_choose_jar() const {
    return !settings_open && station.screen == Screen::Home && (station.uid.empty() || (station.present && station.state == State::Unknown)) && station.state != State::Pending;
  }
  bool matches_screen() const {
    return shown_jar_id_ == station.jar.id && shown_uid_ == station.uid && shown_state_ == station.state &&
           shown_revision_ == station.jar.revision && previous_ == station.screen;
  }
  bool same_session() const { return (station.present || station.manual) && matches_screen(); }
  bool steady_home_reading() const {
    const auto now = esphome::millis();
    return !settings_open && station.screen == Screen::Home && matches_screen() &&
           station.reading.stable(now) && shown_gross_ == station.reading.gross(now);
  }
  void confirm() {
    uint32_t now = esphome::millis();
    if (shown_gross_ == station.reading.gross(now)) station.confirm(now);
  }
  void update_jar_heading(JarHeading widgets, const Jar &jar, const std::string &name,
                          const std::string &id, std::string &shown_name, const std::string *subtitle = nullptr) {
    auto *heading = lv_obj_get_parent(widgets.number);
    if (id != lv_label_get_text(widgets.number)) {
      label(widgets.number, id);
      lv_obj_update_layout(heading);
      const int gap = lv_obj_get_style_pad_column(heading, LV_PART_MAIN);
      const int available = lv_obj_get_content_width(heading) - lv_obj_get_width(widgets.number) - gap;
      lv_obj_set_style_max_width(widgets.title, available, LV_PART_MAIN);
    }
    // LVGL replaces the end of a long label with dots in its own text buffer.
    if (name != shown_name) {
      shown_name = name;
      lv_label_set_text(widgets.title, name.c_str());
    }
    if (subtitle) { label(widgets.maker, *subtitle); return; }
    std::string metadata = jar.maker;
    if (!jar.code.empty()) metadata += (metadata.empty() ? "" : " • ") + jar.code;
    label(widgets.maker, metadata);
  }
  void home(uint32_t now) {
    auto &s = station;
    const bool known = s.jar.valid();
    auto gross = s.reading.gross(now);
    auto net = s.net(gross);
    auto percent = s.percent(gross);
    bool saved = s.state == State::Saved;
    std::string heading = s.state == State::Unknown ? "Unlinked jar" : s.jar.name;
    std::string description, status;
    const bool stable = s.reading.stable(now);
    bool warning = false;
    if (!gross) status = "Waiting for scale";
    if (gross && *gross > 2000) { status = "Over capacity"; warning = true; }
    if (known) {
      if (s.jar.archived) { status = "Archived"; warning = true; }
      else if (s.below_tare(gross)) {
        status = "Below tare"; warning = true;
      } else if (net && s.jar.threshold && *net < *s.jar.threshold && stable) {
        status = "Low stock"; warning = true;
      }
      if (saved && gross == s.saved_gross) description = "Saved";
    } else if (s.state == State::Lookup) {
      heading = "Looking up tag"; description = "Contacting Glaze Manager";
    } else if (needs_calibration) {
      description = "Open Settings to calibrate the scale.";
    }
    if (!s.notice.empty()) description = s.notice;
    const auto &pending = s.pending_save();
    if (s.state == State::Pending && pending.active()) {
      if (!known) heading = pending.setup() ? "Creating new jar" : "Saving jar #" + std::to_string(pending.number);
      status.clear();
      warning = false;
      description = grams(pending.displayed_g) + " • " + s.notice;
    } else if (!s.present && !s.uid.empty()) description = "Tag lost. Checking platform.";
    auto hero = s.display_weight(gross);
    const int number = saved ? s.saved_number : s.jar.number;
    const bool identified = (s.present || s.manual) && number > 0;
    update_jar_heading(w.home_heading, s.jar, heading, identified ? "#" + std::to_string(number) : "",
                       shown_jar_name_, pending.active() && !known ? &pending.name : nullptr);
    const bool show_picker = can_choose_jar();
    if ((!show_picker || picker_uid_ != s.uid) && lv_dropdown_is_open(w.jar_picker)) lv_dropdown_close(w.jar_picker);
    visible(w.jar_picker, show_picker);
    if (!lv_dropdown_is_open(w.jar_picker)) waiting_for_jars_ = false;
    else if (waiting_for_jars_) {
      const auto &entry = s.inventory.entry(picker_list());
      if (!entry.loading) display_jars(entry);
    }
    std::string basis;
    if (known) {
      auto starting = s.display_weight(s.jar.baseline);
      if (starting) basis = "of " + grams(starting) + " remaining";
      else if (s.jar.tare) basis = "remaining";
    }
    label(w.basis, basis);
    label(w.amount, hero ? grams(hero) : "--");
    if (!description.empty()) {
      if (!status.empty()) status += " • ";
      status += description;
    }
    label(w.status, status);
    lv_obj_set_style_text_color(w.status, lv_color_hex(warning ? theme_at(production_palette).warning :
                                                       palette_at(production_palette).accent), 0);
    visible(w.bar, known && percent.has_value());
    if (known && percent) lv_bar_set_value(w.bar, std::clamp<int>(std::lround(*percent), 0, 100), LV_ANIM_OFF);
    lv_obj_set_state(w.tare_button, LV_STATE_DISABLED, !stable);
    lv_obj_set_state(w.save_button, LV_STATE_DISABLED, !s.can_save(now));
  }
  void line(const std::string &text) {
    auto *obj = lv_label_create(w.body);
    lv_obj_set_width(obj, lv_pct(100));
    lv_label_set_text(obj, text.c_str());
    lv_obj_set_pos(obj, 0, y_);
    lv_obj_update_layout(obj);
    y_ += lv_obj_get_height(obj) + 8;
  }
  lv_obj_t *form_button(lv_obj_t *parent) {
    auto *obj = lv_button_create(parent);
    lv_obj_add_style(obj, w.panel, LV_PART_MAIN);
    lv_obj_add_style(obj, w.control_corners, LV_PART_MAIN);
    return obj;
  }
  void row(const std::string &text, Row kind, size_t index = 0) {
    auto *obj = form_button(w.body);
    lv_obj_set_size(obj, lv_pct(100), 44);
    lv_obj_set_pos(obj, 0, y_);
    y_ += 50;
    lv_obj_set_style_pad_hor(obj, 12, LV_PART_MAIN);
    auto *caption = lv_label_create(obj);
    lv_obj_set_width(caption, lv_pct(100));
    lv_label_set_long_mode(caption, LV_LABEL_LONG_MODE_DOTS);
    lv_label_set_text(caption, text.c_str());
    lv_obj_center(caption);
    clicks_.push_back({this, kind, index});
    auto &click = clicks_.back();
    lv_obj_add_event_cb(obj, [](lv_event_t *event) {
      auto *c = static_cast<Click *>(lv_event_get_user_data(event));
      c->owner->select(c->row, c->index);
    }, LV_EVENT_CLICKED, &click);
  }
  void select(Row kind, size_t index) {
    if (!same_session()) return;
    auto &s = station;
    switch (kind) {
      case Row::NewGlaze: s.begin_glaze(); break;
      case Row::ClearClassification: s.draft.classification = {}; s.screen = Screen::NewGlaze; break;
      case Row::ClearManufacturer: s.draft.manufacturer = {}; s.screen = Screen::NewGlaze; break;
      case Row::NewManufacturer: s.begin_manufacturer(); break;
      case Row::NewContainer: s.begin_container(); break;
      case Row::Option: s.choose(index); break;
      case Row::ClearPreset: s.preset = {}; s.screen = Screen::Setup; break;
      case Row::Reload: s.pick(s.list); break;
    }
    dirty_ = true;
  }
  void menu() {
    auto &s = station;
    if (s.screen == Screen::Link) button(w.menu_action, w.action_label, "Link", s.present && !s.form_busy);
    else visible(w.menu_action, false);
    const bool loading = s.catalog_loading;
    if (!dirty_ && loading == catalog_loading_) return;
    catalog_loading_ = loading;
    visible(w.link_card, s.screen == Screen::Link);
    lv_obj_clean(w.body);
    clicks_.clear();
    // Event callbacks reference these entries until we rebuild the list.
    clicks_.reserve(s.options.size() + 2);
    y_ = 0;
    lv_obj_set_height(w.body, s.screen == Screen::Pick ? 252 : 200);
    if (s.screen == Screen::Pick) {
      const char *titles[] = {"Manufacturer", "Glaze", "Container", "Location", "Cone", "Product line", "Classification"};
      label(w.menu_title, titles[static_cast<int>(s.list)]);
      if (loading) line("Loading...");
      else if (s.catalog_failed) row("Retry", Row::Reload);
      else {
        if (s.list == List::Presets && !s.preset.id.empty()) row("Clear selection", Row::ClearPreset);
        if (s.list == List::Manufacturers && !s.draft.manufacturer.id.empty()) row("Clear selection", Row::ClearManufacturer);
        if (s.list == List::Classifications && !s.draft.classification.id.empty()) row("Clear selection", Row::ClearClassification);
        for (size_t i = 0; i < s.options.size(); ++i) {
          const auto &o = s.options[i];
          row(o.code.empty() ? o.label : o.label + " (" + o.code + ")", Row::Option, i);
        }
        if (s.list == List::Glazes) row("New glaze...", Row::NewGlaze);
        else if (s.list == List::Manufacturers) row("New manufacturer...", Row::NewManufacturer);
        else if (s.list == List::Presets) row("New container...", Row::NewContainer);
        else if (s.options.empty()) line("No choices available");
      }
    } else {
      label(w.menu_title, "Link to jar");
      update_jar_heading(w.link_heading, s.candidate, s.candidate.name, "#" + std::to_string(s.candidate.number),
                         shown_link_name_);
      y_ = lv_obj_get_height(w.link_card) + 8;
    }
    if (!s.notice.empty()) line(s.notice);
    lv_obj_scroll_to_y(w.body, 0, LV_ANIM_OFF);
  }
  void render_suggestions(bool entered) {
    std::string prefix = lv_textarea_get_text(w.entry);
    if (!entered && prefix == suggestion_prefix_ && suggestion_count_ == station.options.size()) return;
    suggestion_prefix_ = prefix;
    suggestion_count_ = station.options.size();
    lv_obj_clean(w.suggestions);
    for (const auto &option : station.options) {
      if (strncasecmp(option.label.c_str(), prefix.c_str(), prefix.size()) != 0 || option.label == prefix) continue;
      auto *chip = form_button(w.suggestions);
      lv_obj_set_height(chip, 40);
      lv_obj_set_width(chip, LV_SIZE_CONTENT);
      lv_obj_set_style_pad_hor(chip, 12, 0);
      auto *name = lv_label_create(chip);
      lv_label_set_text(name, option.label.c_str());
      lv_obj_center(name);
      lv_obj_add_event_cb(chip, [](lv_event_t *event) {
        auto *self = static_cast<View *>(lv_event_get_user_data(event));
        if (!self->same_session()) return;
        auto *button = lv_event_get_current_target_obj(event);
        station.entered(lv_label_get_text(lv_obj_get_child(button, 0)));
      }, LV_EVENT_CLICKED, this);
      if (lv_obj_get_child_count(w.suggestions) == 4) break;
    }
    lv_obj_scroll_to_x(w.suggestions, 0, LV_ANIM_OFF);
  }
  void render_edit(bool entered) {
    auto &s = station;
    const bool autocomplete = s.editor == Edit::Location || s.editor == Edit::Cone || s.editor == Edit::ProductLine;
    const bool cost = s.editor == Edit::Cost;
    const bool weight = s.editor == Edit::ContainerWeight;
    const char *title = "", *caption = "", *value = "";
    switch (s.editor) {
      case Edit::Location: title = "Location"; value = s.location.c_str(); break;
      case Edit::Cost: title = "Cost"; caption = "$"; value = s.cost.c_str(); break;
      case Edit::GlazeName:
      case Edit::ManufacturerName: title = "Name"; break;
      case Edit::ProductCode: title = "Product code"; break;
      case Edit::Cone: title = "Cone"; break;
      case Edit::ProductLine: title = "Product line"; break;
      case Edit::ContainerName: title = "Name"; value = s.container_name.c_str(); break;
      case Edit::ContainerWeight: title = "Weight in grams"; value = s.container_weight.c_str(); break;
    }
    if (auto *text = s.draft_text(s.editor)) value = text->c_str();
    label(w.edit_title, title);
    label(w.edit_caption, caption);
    label(w.edit_context, s.notice);
    visible(w.suggestions, autocomplete);
    if (entered) {
      int label_width = cost ? 24 : 0;
      lv_obj_set_x(w.entry, lv_obj_get_x(w.suggestions) + label_width);
      lv_obj_set_width(w.entry, lv_obj_get_width(w.suggestions) - label_width);
      lv_textarea_set_accepted_chars_static(w.entry, cost ? "0123456789." : weight ? "0123456789" : nullptr);
      lv_textarea_set_max_length(w.entry, weight ? 8 : 200);
      lv_textarea_set_text(w.entry, value);
      if (cost || weight) {
        static const char *const decimal_keys[] = {"1", "2", "3", LV_SYMBOL_BACKSPACE, "\n",
          "4", "5", "6", LV_SYMBOL_KEYBOARD, "\n", "7", "8", "9", LV_SYMBOL_OK, "\n", "0", ".", ""};
        static const char *const whole_keys[] = {"1", "2", "3", LV_SYMBOL_BACKSPACE, "\n",
          "4", "5", "6", LV_SYMBOL_KEYBOARD, "\n", "7", "8", "9", LV_SYMBOL_OK, "\n", "0", ""};
        constexpr auto key = LV_BUTTONMATRIX_CTRL_WIDTH_1;
        static const lv_buttonmatrix_ctrl_t widths[] = {key,key,key,key, key,key,key,key, key,key,key,key,
          LV_BUTTONMATRIX_CTRL_WIDTH_2, LV_BUTTONMATRIX_CTRL_WIDTH_2};
        lv_keyboard_set_map(w.keys, LV_KEYBOARD_MODE_USER_1, weight ? whole_keys : decimal_keys, widths);
      }
      lv_keyboard_set_mode(w.keys, (cost || weight) ? LV_KEYBOARD_MODE_USER_1 : LV_KEYBOARD_MODE_TEXT_LOWER);
    }
    if (autocomplete) render_suggestions(entered);
  }

};
inline View view;
}  // namespace glaze
