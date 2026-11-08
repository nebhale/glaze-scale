#pragma once

#include <iterator>
#include <lvgl.h>

namespace glaze {

struct Palette {
  const char *name;
  const char *slug;
  uint32_t background, card_top, card_bottom, edge, ink, muted, accent;
  uint32_t track = 0x384151;
};

enum class ReadingFont { Montserrat, Bodoni, Jersey };

struct Theme {
  ReadingFont numerals;
  int border;
  uint32_t warning;
  Palette original, lilac;
};

// Original palettes adapted from Axellum's Tab5 themes. See palette-LICENSE.txt.
// Each pair shares the numeral font, border width and warning colour.
constexpr Theme themes[] = {
    {ReadingFont::Bodoni, 1, 0xFFAE3D,
     {"Platine et or", "platine", 0x08080B, 0x22232D, 0x0E0F14, 0x625D54, 0xFAFAFD, 0xB8B7C8, 0xD5B26A},
     {"Platine et or Lilac", "platine-lilac", 0x0C090F, 0x302737, 0x151019, 0x716176, 0xFBF7FC, 0xC7BACF, 0xDDC1E7, 0x453A49}},
    {ReadingFont::Montserrat, 1, 0xE8B84F,
     {"Graphite", "graphite", 0x0F1114, 0x181B20, 0x181B20, 0x414650, 0xF4F6FA, 0xAFB6C2, 0x6AB0FF},
     {"Graphite Lilac", "graphite-lilac", 0x131116, 0x201C25, 0x201C25, 0x4D4455, 0xF6F3F8, 0xBCB3C5, 0xBB9ED7, 0x403847}},
    {ReadingFont::Montserrat, 1, 0xF3B73B,
     {"Craie et ardoise", "craie", 0x0F141B, 0x1B2331, 0x121924, 0x445164, 0xF7F9FC, 0xB5C1D0, 0x5CD3E3},
     {"Craie et ardoise Lilac", "craie-lilac", 0x17141E, 0x2C2638, 0x1E1927, 0x554C65, 0xF8F5FB, 0xC6BDD4, 0xCAB8ED, 0x433C50}},
    {ReadingFont::Montserrat, 1, 0xFBBF24,
     {"Ardoise", "ardoise", 0x0B1120, 0x2C3A52, 0x131C2C, 0x566780, 0xFFFFFF, 0xB6C5D8, 0x22D3EE},
     {"Glaze Manager Lilac", "lilac", 0x160B20, 0x3F2C52, 0x20132C, 0x6B5680, 0xF8F5FB, 0xC7B6D8, 0xCFB7E0, 0x453851}},
    {ReadingFont::Jersey, 3, 0xFFCD75,
     {"Pixel", "pixel", 0x1A1C2C, 0x11132A, 0x11132A, 0x566C86, 0xF4F4F4, 0xA5BDCE, 0x73EFF7},
     {"Pixel Lilac", "pixel-lilac", 0x271833, 0x1A1026, 0x1A1026, 0x795892, 0xFAF2FF, 0xC8B0DA, 0xD798FF, 0x493555}},
};

constexpr size_t theme_count = std::size(themes);
constexpr size_t palette_count = 2 * theme_count;
constexpr size_t ardoise_index = 3;
constexpr size_t production_palette = theme_count + ardoise_index;

// T visits the original set followed by the lilac set. P switches the same theme.
constexpr const Theme &theme_at(size_t index) { return themes[index % theme_count]; }

constexpr const Palette &palette_at(size_t index) {
  const auto &theme = theme_at(index);
  return index < theme_count ? theme.original : theme.lilac;
}

inline void apply_palette(size_t index,
                          lv_style_t *page, lv_style_t *panel, lv_style_t *muted,
                          lv_style_t *accent, lv_style_t *warning,
                          lv_style_t *primary, lv_style_t *key, lv_style_t *track) {
  const auto &theme = theme_at(index);
  const auto &p = palette_at(index);
  lv_style_set_bg_color(page, lv_color_hex(p.background));
  lv_style_set_text_color(page, lv_color_hex(p.ink));
  lv_style_set_bg_color(panel, lv_color_hex(p.card_top));
  lv_style_set_bg_grad_color(panel, lv_color_hex(p.card_bottom));
  lv_style_set_bg_grad_dir(panel, p.card_top == p.card_bottom ? LV_GRAD_DIR_NONE : LV_GRAD_DIR_VER);
  lv_style_set_border_color(panel, lv_color_hex(p.edge));
  lv_style_set_border_width(panel, theme.border);
  lv_style_set_text_color(panel, lv_color_hex(p.ink));
  lv_style_set_text_color(muted, lv_color_hex(p.muted));
  lv_style_set_text_color(accent, lv_color_hex(p.accent));
  lv_style_set_text_color(warning, lv_color_hex(theme.warning));
  lv_style_set_bg_color(track, lv_color_hex(p.track));
  lv_style_set_bg_color(primary, lv_color_hex(p.accent));
  lv_style_set_text_color(primary, lv_color_hex(p.background));
  lv_style_set_bg_color(key, lv_color_hex(p.card_top));
  lv_style_set_text_color(key, lv_color_hex(p.ink));
  lv_style_set_border_color(key, lv_color_hex(p.edge));
  lv_obj_report_style_change(nullptr);
}

}  // namespace glaze
