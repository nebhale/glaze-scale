#pragma once

#include "reading.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "esphome/core/preferences.h"
#include <lvgl.h>

namespace glaze {

// Seconds, in the same order as the Display dropdown.
constexpr std::array<uint16_t, 5> screen_timeouts{30, 60, 120, 300, 600};

class DisplayPower {
 public:
  void begin(void (*backlight)(int)) {
    preference_ = esphome::global_preferences->make_preference<Settings>(esphome::fnv1_hash("glaze.display.v1"));
    Settings saved;
    if (preference_.load(&saved) && saved.brightness >= 10 && saved.brightness <= 100 &&
        std::find(screen_timeouts.begin(), screen_timeouts.end(), saved.timeout) != screen_timeouts.end()) {
      settings_ = saved;
    }
    backlight_ = backlight;
    // The system layer stays above keyboards, dropdowns, and page transitions.
    shade_ = lv_obj_create(lv_layer_sys());
    lv_obj_remove_style_all(shade_);
    lv_obj_set_size(shade_, lv_pct(100), lv_pct(100));
    lv_obj_remove_flag(shade_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(shade_, lv_color_black(), 0);
    lv_obj_add_event_cb(shade_, [](lv_event_t *event) {
      static_cast<DisplayPower *>(lv_event_get_user_data(event))->activity();
    }, LV_EVENT_CLICKED, this);
    apply();
    activity();
  }

  void set_brightness(int value) {
    settings_.brightness = std::clamp(value, 10, 100);
    if (shade_) apply();
  }

  void set_timeout(size_t option) {
    if (option >= screen_timeouts.size()) return;
    settings_.timeout = screen_timeouts[option];
    save();
    activity();
  }

  size_t timeout_option() const {
    return std::find(screen_timeouts.begin(), screen_timeouts.end(), settings_.timeout) - screen_timeouts.begin();
  }

  void save() {
    if (!preference_.save(&settings_) || !esphome::global_preferences->sync())
      ESP_LOGW("display", "Could not store display settings");
  }

  void activity() {
    if (!shade_) return;
    lv_display_trigger_activity(nullptr);
    if (asleep_) {
      asleep_ = false;
      apply();
    }
  }

  void update(Grams stable_weight) {
    if (!shade_) return;
    // Ignore +/-1 g of jitter. A stationary jar can time out; adding weight wakes it.
    if (stable_weight && std::abs(*stable_weight - last_load_) >= load_detection_g) {
      if (*stable_weight > last_load_) activity();
      last_load_ = *stable_weight;
    }
    if (!asleep_ && lv_display_get_inactive_time(nullptr) >= settings_.timeout * 1000UL) {
      asleep_ = true;
      apply();
    }
  }

  bool asleep() const { return asleep_; }
  int brightness() const { return settings_.brightness; }

 private:
  lv_obj_t *shade_ = nullptr;
  void (*backlight_)(int) = nullptr;
  double last_load_ = 0;
  struct Settings {
    uint16_t brightness = 100;
    uint16_t timeout = 120;
  };
  Settings settings_;
  esphome::ESPPreferenceObject preference_;
  bool asleep_ = false;

  void apply() {
    const int level = asleep_ ? 0 : settings_.brightness;
    if (backlight_) backlight_(level);
    // The desktop has no PWM backlight. Dim only its simulated panel.
    int opacity = backlight_ ? (asleep_ ? LV_OPA_COVER : LV_OPA_TRANSP) : (100 - level) * LV_OPA_COVER / 100;
    lv_obj_set_style_bg_opa(shade_, opacity, 0);
    lv_obj_set_flag(shade_, LV_OBJ_FLAG_CLICKABLE, asleep_);
    lv_obj_set_flag(shade_, LV_OBJ_FLAG_HIDDEN, opacity == LV_OPA_TRANSP);
  }
};

inline DisplayPower display_power;
}  // namespace glaze
