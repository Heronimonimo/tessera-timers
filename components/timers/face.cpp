#include "face.h"
#include <algorithm>

namespace esphome::timers {

using tessera::Font;
using tessera::Tone;
namespace ui = tessera::ui;

static int clamp(int v, int lo, int hi) { return std::max(lo, std::min(hi, v)); }

// The largest of the screen's text fonts that `text` fits in, across and high.
static Font fitting(const std::string &text, int width, int height) {
  for (Font f : {Font::VALUE, Font::HEADLINE, Font::TITLE, Font::BODY_LARGE})
    if (ui::text_width(text, f) <= width && ui::line_height(f) <= height) return f;
  return Font::BODY;
}

static void show(lv_obj_t *obj, bool shown) {
  if (!obj || lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN) != shown) return;
  if (shown) lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

static lv_obj_t *centred_label(lv_obj_t *parent, Font font, theme::Role role) {
  lv_obj_t *label = ui::label(parent, font, role);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  return label;
}

void Face::create(lv_obj_t *parent, int width, int height, Kind kind, const tessera::Plugin *plugin, const char *name,
                  bool card) {
  plugin_ = plugin;
  parent_ = parent;
  kind_ = kind;
  card_ = card;
  const int W = width, H = height;
  // The gap between keys, and the smallest key: the 7 mm a finger needs (Tessera's touch_min).
  const int g = ui::px(8), km = ui::mm(7);
  const bool timer = kind == Kind::TIMER;

  // The shape: the richest one whose keys all keep 7 mm in this room, with a ring of at least 12 to 14 mm.
  const int column_min = 2 * ui::mm(11) + g;
  if (W * 100 >= H * 115 && H >= (timer ? 3 * km + 2 * g : 2 * km + g) && W - column_min - 2 * g >= ui::mm(14))
    shape_ = Shape::SIDE;
  else if (timer && W >= 4 * ui::mm(9) + 3 * g && H >= 2 * km + 3 * g + ui::mm(20))
    shape_ = Shape::STACK;
  else if (!timer && W >= 2 * km + g && H >= km + 2 * g + ui::mm(16))
    shape_ = Shape::STACK;
  else if (timer && W >= 2 * km + g && H >= 2 * km + 3 * g + ui::mm(12))
    shape_ = Shape::COMPACT;
  else if (W >= H + ui::mm(10) + 2 * km + 2 * g)
    shape_ = Shape::STRIP;
  else
    shape_ = Shape::PLAIN;

  // The ring: where it stands and how large, per shape; the time goes inside it unless a shape puts it beside.
  int cx = W / 2, cy = H / 2, diameter = std::min(W, H);
  bool inside = true;
  int beside_x = 0, beside_w = 0;
  switch (shape_) {
    case Shape::SIDE: {
      int column = std::max(std::min(W * 45 / 100, 2 * ui::mm(34) + g), column_min);
      const int ring_w = W - column - 2 * g;
      // On a large card the ring stops at 70 mm, and the ring and the keys stand together in the middle.
      diameter = std::min({ring_w, H, ui::mm(70)});
      const int gap = std::min(ring_w - diameter, 4 * g) + 2 * g;
      const int left = (W - (diameter + gap + column)) / 2;
      cx = left + diameter / 2;
      const int x0 = left + diameter + gap, kw = (column - g) / 2;
      if (timer) {
        const int kh = clamp((H - 2 * g) / 3, km, ui::mm(13));
        const int y0 = (H - (3 * kh + 2 * g)) / 2;
        add_button(Key::ADD_10S, x0, y0, kw, kh, false, false);
        add_button(Key::ADD_1M, x0 + kw + g, y0, kw, kh, false, false);
        add_button(Key::ADD_10M, x0, y0 + kh + g, kw, kh, false, false);
        add_button(Key::ADD_1H, x0 + kw + g, y0 + kh + g, kw, kh, false, false);
        add_button(Key::SECONDARY, x0, y0 + 2 * (kh + g), kw, kh, false, false);
        add_button(Key::PRIMARY, x0 + kw + g, y0 + 2 * (kh + g), kw, kh, false, true);
      } else {
        const int kh = clamp(H / 4, km, ui::mm(13)), lh = ui::line_height(Font::BODY);
        const int lines = clamp((H - kh - g) / std::max(1, lh), 0, 3);
        const int y0 = (H - (lines * lh + g + kh)) / 2;
        lap_x_ = x0;
        lap_y_ = y0;
        lap_w_ = column;
        for (int i = 0; i < lines; ++i) {
          lap_[i] = centred_label(parent, Font::BODY, theme::MUTED);
          lv_obj_set_size(lap_[i], column, lh);
          lv_obj_set_pos(lap_[i], x0, y0 + i * lh);
        }
        const int ya = y0 + lines * lh + g;
        add_button(Key::SECONDARY, x0, ya, kw, kh, false, false);
        add_button(Key::PRIMARY, x0 + kw + g, ya, kw, kh, false, true);
      }
      break;
    }
    case Shape::STACK:
    case Shape::COMPACT: {
      const int kh = shape_ == Shape::COMPACT ? km : clamp(H * 12 / 100, km, ui::mm(13));
      const int aw = std::min(W, 4 * ui::mm(32) + 3 * g), x0 = (W - aw) / 2;
      const int ya = H - kh;
      int top = ya - 2 * g;
      if (timer) {
        const int yk = ya - g - kh;
        top = yk - 2 * g;
        if (shape_ == Shape::STACK) {
          const int kw = (aw - 3 * g) / 4;
          const Key keys[4] = {Key::ADD_10S, Key::ADD_1M, Key::ADD_10M, Key::ADD_1H};
          for (int i = 0; i < 4; ++i) add_button(keys[i], x0 + i * (kw + g), yk, kw, kh, false, false);
        } else {
          const int kw = (aw - g) / 2;
          add_button(Key::ADD_1M, x0, yk, kw, kh, false, false);
          add_button(Key::ADD_10M, x0 + kw + g, yk, kw, kh, false, false);
        }
      }
      const int aw2 = (aw - g) / 2;
      add_button(Key::SECONDARY, x0, ya, aw2, kh, false, false);
      add_button(Key::PRIMARY, x0 + aw2 + g, ya, aw2, kh, false, true);
      diameter = std::min(W, top);
      cy = top / 2;
      break;
    }
    case Shape::STRIP: {
      // The ring gives the time room beside it: at most 45 % of what the keys leave.
      const int kd = clamp(H * 80 / 100, km, ui::mm(11));
      const int x2 = W - kd, x1 = x2 - g - kd, y = (H - kd) / 2;
      diameter = std::min(H, (x1 - 2 * g) * 45 / 100);
      cx = diameter / 2;
      add_button(timer ? Key::ADD_1M : Key::SECONDARY, x1, y, kd, kd, true, false);
      add_button(Key::PRIMARY, x2, y, kd, kd, true, true);
      inside = false;
      beside_x = diameter + g;
      beside_w = x1 - g - beside_x;
      break;
    }
    case Shape::PLAIN:
      if (W >= H * 16 / 10) {
        diameter = H;
        cx = H / 2;
        inside = false;
        beside_x = diameter + g;
        beside_w = W - beside_x;
      }
      break;
  }
  diameter = std::max(1, diameter);

  // A ring that runs clockwise from the top: its track in the ground's soft grey, its arc in the timer's tone.
  const int stroke = clamp(diameter * 9 / 100, ui::px(4), ui::mm(3));
  arc_ = lv_arc_create(parent);
  lv_obj_remove_style_all(arc_);
  lv_obj_remove_flag(arc_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(arc_, diameter, diameter);
  lv_obj_set_pos(arc_, cx - diameter / 2, cy - diameter / 2);
  lv_arc_set_rotation(arc_, 270);
  lv_arc_set_bg_angles(arc_, 0, 360);
  lv_arc_set_range(arc_, 0, 1000);
  lv_arc_set_value(arc_, 0);
  for (lv_part_t part : {LV_PART_MAIN, LV_PART_INDICATOR}) {
    lv_obj_set_style_arc_width(arc_, stroke, part);
    lv_obj_set_style_arc_rounded(arc_, true, part);
    lv_obj_set_style_arc_opa(arc_, LV_OPA_COVER, part);
  }

  if (inside) {
    const int inner = diameter - 2 * stroke;
    text_w_ = inner * 82 / 100;
    text_h_ = inner * 72 / 100;
    text_x_ = cx - text_w_ / 2;
    text_y_ = cy - text_h_ / 2;
  } else {
    text_x_ = beside_x;
    text_w_ = std::max(0, beside_w);
    text_y_ = 0;
    text_h_ = H;
    has_name_ = name != nullptr && name[0] != '\0';
  }
  if (has_name_) {
    name_ = centred_label(parent, Font::BODY, theme::MUTED);
    ui::set_text(name_, name);
  }
  time_ = centred_label(parent, Font::VALUE, theme::INK);
  sub_ = centred_label(parent, Font::BODY, theme::MUTED);
  on_theme();
}

Face::Button *Face::add_button(Key key, int x, int y, int w, int h, bool round, bool main) {
  if (count_ >= BUTTONS) return nullptr;
  Button &b = buttons_[count_++];
  b.key = key;
  b.x = x;
  b.y = y;
  b.w = w;
  b.h = h;
  b.round = round;
  b.main = main;
  // The two keys that act are pills (round keys on a strip), the add keys a key's own corners.
  b.box = ui::block(parent_, main ? theme::ACCENT : theme::BUTTON);
  lv_obj_set_size(b.box, w, h);
  lv_obj_set_pos(b.box, x, y);
  if (round || key == Key::PRIMARY || key == Key::SECONDARY) lv_obj_set_style_radius(b.box, LV_RADIUS_CIRCLE, 0);
  b.label = centred_label(b.box, Font::BODY, main ? theme::ON_ACCENT : theme::SLATE);
  lv_obj_set_width(b.label, w);
  return &b;
}

void Face::paint_button(Button &b, const char *words, uint32_t icon) {
  Font font;
  std::string text;
  if (b.round && icon) {
    text = ui::icon(icon);
    font = ui::line_height(Font::ICON) <= b.h * 75 / 100 ? Font::ICON : Font::ICON_SMALL;
  } else {
    text = words;
    font = ui::text_width(text, Font::BODY_LARGE) <= b.w * 88 / 100 &&
                   ui::line_height(Font::BODY_LARGE) <= b.h * 80 / 100
               ? Font::BODY_LARGE
               : Font::BODY;
  }
  ui::set_font(b.label, font);
  ui::set_text(b.label, text);
  lv_obj_set_pos(b.label, 0, (b.h - ui::line_height(font)) / 2);
}

void Face::paint(const View &v) {
  // The keys: the add keys say what they add, the two others what they do now.
  for (int i = 0; i < count_; ++i) {
    Button &b = buttons_[i];
    switch (b.key) {
      case Key::ADD_10S: paint_button(b, plugin_->text("add_10s"), 0); break;
      case Key::ADD_1M: paint_button(b, plugin_->text("add_1m"), 0); break;
      case Key::ADD_10M: paint_button(b, plugin_->text("add_10m"), 0); break;
      case Key::ADD_1H: paint_button(b, plugin_->text("add_1h"), 0); break;
      case Key::PRIMARY: paint_button(b, v.primary, v.primary_icon); break;
      case Key::SECONDARY: paint_button(b, v.secondary, v.secondary_icon); break;
      default: break;
    }
  }

  if (v.tone != tone_) {
    tone_ = v.tone;
    lv_obj_set_style_arc_color(arc_, ui::tone_color(tone_, theme::ACCENT), LV_PART_INDICATOR);
  }
  if (v.progress != progress_) {
    progress_ = v.progress;
    lv_arc_set_value(arc_, clamp(progress_, 0, 1000));
  }

  // The time as large as its box takes, with the name over it and a line under it.
  const int line = ui::line_height(Font::BODY);
  // The line under the time and the name over it only when they fit whole: a time cut with dots says nothing.
  const bool sub_fits = !v.sub.empty() && ui::text_width(v.sub, Font::BODY) <= text_w_;
  const bool name_fits = has_name_ && ui::text_width(lv_label_get_text(name_), Font::BODY) <= text_w_ &&
                         text_h_ >= 3 * line;
  const int sub_h = sub_fits ? line : 0, name_h = name_fits ? line : 0;
  const Font font = fitting(v.time, text_w_, std::max(line, text_h_ - sub_h - name_h));
  const int time_h = ui::line_height(font);
  int y = text_y_ + std::max(0, (text_h_ - name_h - time_h - sub_h) / 2);
  show(name_, name_fits);
  if (name_fits) {
    lv_obj_set_size(name_, text_w_, line);
    lv_obj_set_pos(name_, text_x_, y);
    y += name_h;
  }
  ui::set_font(time_, font);
  ui::set_text(time_, v.time);
  ui::set_tone(time_, v.tone == Tone::ALERT ? Tone::ALERT : Tone::NORMAL, theme::INK);
  lv_obj_set_size(time_, text_w_, time_h);
  lv_obj_set_pos(time_, text_x_, y);
  y += time_h;
  show(sub_, sub_fits);
  ui::set_text(sub_, v.sub);
  lv_obj_set_size(sub_, text_w_, line);
  lv_obj_set_pos(sub_, text_x_, y);

  for (int i = 0; i < 3; ++i) {
    if (!lap_[i]) continue;
    ui::set_text(lap_[i], i < v.laps ? v.lap[i] : std::string());
    ui::set_color(lap_[i], i == 0 ? theme::INK : theme::MUTED);
  }
}

void Face::on_theme() {
  lv_obj_set_style_arc_color(arc_, ui::color(card_ ? theme::CARD : theme::TRACK), LV_PART_MAIN);
  lv_obj_set_style_arc_color(arc_, ui::tone_color(tone_, theme::ACCENT), LV_PART_INDICATOR);
  if (name_) ui::set_color(name_, theme::MUTED);
  if (time_) ui::set_tone(time_, tone_ == Tone::ALERT ? Tone::ALERT : Tone::NORMAL, theme::INK);
  if (sub_) ui::set_color(sub_, theme::MUTED);
  for (int i = 0; i < 3; ++i)
    if (lap_[i]) ui::set_color(lap_[i], i == 0 ? theme::INK : theme::MUTED);
  for (int i = 0; i < count_; ++i) {
    Button &b = buttons_[i];
    if (!b.box) continue;
    lv_obj_set_style_bg_color(b.box, ui::color(b.main ? theme::ACCENT : theme::BUTTON), 0);
    ui::set_color(b.label, b.main ? theme::ON_ACCENT : theme::SLATE);
  }
}

Key Face::hit(tessera::Point p) const {
  if (p.x < 0 || p.y < 0) return Key::OPEN;
  // A key takes a finger a millimetre beside it too: the gaps between the keys are 2 mm.
  const int slack = ui::mm(1);
  for (int i = 0; i < count_; ++i) {
    const Button &b = buttons_[i];
    if (p.x >= b.x - slack && p.x < b.x + b.w + slack && p.y >= b.y - slack && p.y < b.y + b.h + slack) return b.key;
  }
  return Key::OPEN;
}

}  // namespace esphome::timers
