#include "timers.h"
#include <algorithm>
#include <cstdio>

namespace esphome::timers {

using tessera::CardColor;
using tessera::Font;
using tessera::Tone;
namespace ui = tessera::ui;

static constexpr uint32_t ICON_TIMER = 0xF051B;
static constexpr uint32_t ICON_RINGING = 0xF009E;
static constexpr uint32_t ICON_PAUSE = 0xF03E4;
static constexpr uint32_t ICON_PLAY = 0xF040A;
static constexpr uint32_t ICON_CLOSE = 0xF0156;
static constexpr uint32_t ICON_MIC = 0xF036C;

static const char *card_of(Kind kind) { return kind == Kind::TIMER ? "timer" : "stopwatch"; }

static void set_hidden(lv_obj_t *obj, bool hidden) {
  if (!obj) return;
  if (hidden) lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

static lv_obj_t *make_button(lv_obj_t *parent, int x, int y, int w, int h, theme::Role bg, lv_obj_t **label_out,
                             Font font, theme::Role fg, const std::string &text) {
  lv_obj_t *box = ui::block(parent, bg);
  lv_obj_set_pos(box, x, y);
  lv_obj_set_size(box, std::max(1, w), std::max(1, h));
  lv_obj_t *lbl = ui::label(box, font, fg);
  lv_obj_set_width(lbl, std::max(1, w - ui::px(6)));
  lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
  ui::set_text(lbl, text);
  lv_obj_center(lbl);
  if (label_out) *label_out = lbl;
  return box;
}

// A tile: a face in the tile's room. What runs lives in the plugin's slot of this tile, so a tile made again when its
// page comes back (a board without PSRAM) shows the same timer.
class TimerTile : public tessera::Tile {
 public:
  TimerTile(Timers *plugin, Kind kind) : plugin_(plugin), kind_(kind) {}

  void create(const tessera::TileContext &c) override {
    tile_ = c.tile;
    snprintf(entity_, sizeof entity_, "%s", kind_ == Kind::TIMER && c.entity ? c.entity : "");
    if (kind_ == Kind::TIMER && c.name && *c.name) plugin_->set_label(plugin_->slot(tile_, kind_), c.name);
    face_.create(c.parent, c.width, c.height, kind_, plugin_, c.name, false);
  }

  void on_state(JsonObjectConst data) override {
    if (entity_[0]) plugin_->read(plugin_->slot(tile_, kind_), entity_, data, helper_);
  }

  void on_tick(uint32_t epoch) override { paint(); }
  void on_theme() override { face_.on_theme(); }

  void on_tap() override {
    const Key key = face_.hit(tessera::tap_point());
    if (key == Key::OPEN || key == Key::NONE) {
      tessera::open_card(plugin_->plugin_id(), card_of(kind_), entity_, tile_);
      return;
    }
    plugin_->press(plugin_->slot(tile_, kind_), key, entity_, helper_);
    paint();
  }

 private:
  void paint() { face_.paint(plugin_->view(plugin_->slot(tile_, kind_), entity_, helper_)); }

  Timers *plugin_;
  Kind kind_;
  int tile_ = -1;
  char entity_[96] = {};
  Helper helper_;
  Face face_;
};

// The card: the same face with every key, over the whole glass.
class TimerCard : public tessera::Card {
 public:
  TimerCard(Timers *plugin, Kind kind) : plugin_(plugin), kind_(kind) {}

  void open(const tessera::CardContext &c) override {
    tile_ = c.tile;
    snprintf(entity_, sizeof entity_, "%s", kind_ == Kind::TIMER && c.entity ? c.entity : "");
    face_.create(c.parent, c.width, c.height, kind_, plugin_, "", true);
    helper_ = plugin_->helper_of(plugin_->slot(tile_, kind_));
    paint();
  }

  void on_state(JsonObjectConst data) override {
    if (entity_[0]) plugin_->read(plugin_->slot(tile_, kind_), entity_, data, helper_);
    paint();
  }

  void on_tick(uint32_t epoch) override { paint(); }
  void on_theme() override { face_.on_theme(); }

  void on_tap() override {
    const Key key = face_.hit(tessera::tap_point());
    if (key == Key::OPEN || key == Key::NONE) return;
    plugin_->press(plugin_->slot(tile_, kind_), key, entity_, helper_);
    paint();
  }

 private:
  void paint() { face_.paint(plugin_->view(plugin_->slot(tile_, kind_), entity_, helper_)); }

  Timers *plugin_;
  Kind kind_;
  int tile_ = -1;
  char entity_[96] = {};
  Helper helper_;
  Face face_;
};

// ============================================================================
// All timers overview tile (screen, HA helpers, and Assist voice timers)
// ============================================================================
class OverviewTile : public tessera::Tile {
 public:
  static constexpr int MAX_ROWS = 5;

  explicit OverviewTile(Timers *plugin) : plugin_(plugin) {}

  void create(const tessera::TileContext &c) override {
    tile_ = c.tile;
    width_ = c.width;
    height_ = c.height;
    compact_ = c.columns == 1 && c.rows == 1;
    const bool large = ui::large();

    if (compact_) {
      icon_ = ui::label(c.parent, Font::ICON, theme::MUTED);
      lv_obj_set_style_text_align(icon_, LV_TEXT_ALIGN_CENTER, 0);
      primary_ = ui::label(c.parent, Font::VALUE, theme::INK);
      lv_obj_set_style_text_align(primary_, LV_TEXT_ALIGN_CENTER, 0);
      secondary_ = ui::label(c.parent, Font::BODY, theme::MUTED);
      lv_obj_set_style_text_align(secondary_, LV_TEXT_ALIGN_CENTER, 0);
      lv_label_set_long_mode(secondary_, LV_LABEL_LONG_DOT);

      const int ih = ui::line_height(Font::ICON);
      const int vh = ui::line_height(Font::VALUE);
      const int bh = ui::line_height(Font::BODY);
      const int top = std::max(0, (height_ - (ih + vh + bh)) / 2);
      lv_obj_set_pos(icon_, 0, top);
      lv_obj_set_size(icon_, width_, ih);
      lv_obj_set_pos(primary_, 0, top + ih);
      lv_obj_set_size(primary_, width_, vh);
      lv_obj_set_pos(secondary_, 0, top + ih + vh);
      lv_obj_set_size(secondary_, width_, bh);
    } else {
      const int head_h = ui::line_height(Font::TITLE);
      const int icon_w = ui::line_height(Font::ICON_SMALL) + ui::px(4);
      icon_ = ui::label(c.parent, Font::ICON_SMALL, theme::MUTED);
      lv_obj_set_pos(icon_, 0, std::max(0, (head_h - ui::line_height(Font::ICON_SMALL)) / 2));
      lv_obj_set_size(icon_, icon_w, ui::line_height(Font::ICON_SMALL));

      const int badge_w = ui::px(large ? 86 : 64);
      badge_ = ui::label(c.parent, Font::BODY, theme::MUTED);
      lv_obj_set_style_text_align(badge_, LV_TEXT_ALIGN_RIGHT, 0);
      lv_obj_set_pos(badge_, std::max(0, width_ - badge_w), std::max(0, (head_h - ui::line_height(Font::BODY)) / 2));
      lv_obj_set_size(badge_, badge_w, ui::line_height(Font::BODY));

      const int hx = icon_w + ui::px(large ? 6 : 4);
      header_ = ui::label(c.parent, Font::TITLE, theme::INK);
      lv_obj_set_pos(header_, hx, 0);
      lv_obj_set_size(header_, std::max(1, width_ - hx - badge_w - ui::px(4)), head_h);
      ui::set_text(header_, plugin_->text("overview"));

      const int row_top = head_h + ui::px(large ? 10 : 6);
      const int avail_h = std::max(1, height_ - row_top);
      const int bar_h = ui::px(large ? 4 : 3);
      const int row_gap = ui::px(large ? 8 : 5);
      const int line_h = ui::line_height(Font::BODY_LARGE);
      const int row_step = line_h + bar_h + row_gap;
      visible_rows_ = std::max(1, std::min(MAX_ROWS, (avail_h + row_gap) / std::max(1, row_step)));
      const int time_w = ui::px(large ? 96 : 68);

      for (int i = 0; i < MAX_ROWS; ++i) {
        const int y = row_top + i * row_step;
        Row &r = rows_[i];
        r.name = ui::label(c.parent, Font::BODY_LARGE, theme::INK);
        lv_label_set_long_mode(r.name, LV_LABEL_LONG_DOT);
        lv_obj_set_pos(r.name, 0, y);
        lv_obj_set_size(r.name, std::max(1, width_ - time_w - ui::px(6)), line_h);

        r.time = ui::label(c.parent, Font::BODY_LARGE, theme::INK);
        lv_obj_set_style_text_align(r.time, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_pos(r.time, std::max(0, width_ - time_w), y);
        lv_obj_set_size(r.time, time_w, line_h);

        r.track = ui::block(c.parent, theme::TRACK);
        lv_obj_set_pos(r.track, 0, y + line_h + ui::px(2));
        lv_obj_set_size(r.track, width_, bar_h);

        r.fill = ui::block(r.track, theme::ACCENT);
        lv_obj_set_pos(r.fill, 0, 0);
        lv_obj_set_size(r.fill, 0, bar_h);
      }

      empty_ = ui::label(c.parent, Font::BODY, theme::MUTED);
      lv_obj_set_style_text_align(empty_, LV_TEXT_ALIGN_CENTER, 0);
      lv_obj_set_pos(empty_, 0, row_top + std::max(0, (avail_h - ui::line_height(Font::BODY)) / 2));
      lv_obj_set_size(empty_, width_, ui::line_height(Font::BODY));
      ui::set_text(empty_, plugin_->text("no_timers"));
    }
    paint();
  }

  void on_state(JsonObjectConst) override { paint(); }
  void on_tick(uint32_t) override { paint(); }
  void on_theme() override {
    if (header_) ui::set_color(header_, theme::INK);
    if (primary_) ui::set_color(primary_, theme::INK);
    if (secondary_) ui::set_color(secondary_, theme::MUTED);
    if (empty_) ui::set_color(empty_, theme::MUTED);
    for (auto &r : rows_) {
      if (r.name) ui::set_color(r.name, theme::INK);
      if (r.track) lv_obj_set_style_bg_color(r.track, ui::color(theme::TRACK), 0);
    }
    paint();
  }

  void on_tap() override {
    if (plugin_->any_done()) {
      plugin_->silence();
      paint();
      return;
    }
    tessera::open_card(plugin_->plugin_id(), "overview", "", tile_, plugin_->text("overview"));
  }

 private:
  struct Row {
    lv_obj_t *name{}, *time{}, *track{}, *fill{};
  };

  void paint() {
    OverviewItem items[MAX_ROWS + 4];
    const int count = plugin_->collect_overview(items, MAX_ROWS + 4);
    const bool done = count > 0 && items[0].run == Run::DONE;
    ui::set_card_color(done ? CardColor::ORANGE : CardColor::NONE);

    if (compact_) {
      if (count == 0) {
        ui::set_text(icon_, ui::icon(ICON_TIMER));
        ui::set_tone(icon_, Tone::NORMAL, theme::MUTED);
        ui::set_font(primary_, Font::TITLE);
        ui::set_text(primary_, plugin_->text("no_timers"));
        ui::set_text(secondary_, "");
        return;
      }
      const OverviewItem &first = items[0];
      ui::set_text(icon_, ui::icon(first.run == Run::DONE ? ICON_RINGING : (first.run == Run::RUNNING ? ICON_TIMER : ICON_PAUSE)));
      ui::set_tone(icon_, first.run == Run::DONE ? Tone::ALERT : (first.run == Run::RUNNING ? Tone::BUSY : Tone::NORMAL),
                   theme::MUTED);
      const std::string main_txt = first.run == Run::DONE ? plugin_->text("done") : clock_of(first.remaining_s);
      Font f = Font::VALUE;
      if (ui::text_width(main_txt, f) > width_) f = Font::HEADLINE;
      if (ui::text_width(main_txt, f) > width_) f = Font::TITLE;
      ui::set_font(primary_, f);
      ui::set_text(primary_, main_txt);

      std::string sub = first.label[0] ? first.label : plugin_->text("timer");
      if (count > 1) sub += " (+" + std::to_string(count - 1) + ")";
      ui::set_text(secondary_, sub);
      return;
    }

    ui::set_text(icon_, ui::icon(done ? ICON_RINGING : ICON_TIMER));
    ui::set_tone(icon_, done ? Tone::ALERT : (count ? Tone::BUSY : Tone::NORMAL), theme::MUTED);

    if (count == 0) {
      ui::set_text(badge_, "");
      set_hidden(empty_, false);
      for (int i = 0; i < MAX_ROWS; ++i) {
        set_hidden(rows_[i].name, true);
        set_hidden(rows_[i].time, true);
        set_hidden(rows_[i].track, true);
      }
      return;
    }

    set_hidden(empty_, true);
    if (done) {
      ui::set_text(badge_, plugin_->text("done"));
      ui::set_tone(badge_, Tone::ALERT, theme::MUTED);
    } else if (count == 1) {
      ui::set_text(badge_, plugin_->text("active_one"));
      ui::set_tone(badge_, Tone::BUSY, theme::MUTED);
    } else {
      ui::set_text(badge_, tessera::format(plugin_->text("active_many"), static_cast<long>(count)));
      ui::set_tone(badge_, Tone::BUSY, theme::MUTED);
    }

    for (int i = 0; i < MAX_ROWS; ++i) {
      Row &r = rows_[i];
      if (i >= visible_rows_ || i >= count) {
        set_hidden(r.name, true);
        set_hidden(r.time, true);
        set_hidden(r.track, true);
        continue;
      }
      const OverviewItem &t = items[i];
      set_hidden(r.name, false);
      set_hidden(r.time, false);
      set_hidden(r.track, false);

      ui::set_text(r.name, t.label[0] ? t.label : plugin_->text("timer"));
      if (t.run == Run::DONE) {
        ui::set_text(r.time, plugin_->text("done"));
        ui::set_tone(r.time, Tone::ALERT, theme::INK);
        ui::set_fill(r.fill, Tone::ALERT, theme::ACCENT);
        lv_obj_set_width(r.fill, width_);
      } else {
        std::string txt = clock_of(t.remaining_s);
        if (t.run == Run::PAUSED) txt = std::string(plugin_->text("paused")) + " " + txt;
        ui::set_text(r.time, txt);
        ui::set_tone(r.time, t.run == Run::RUNNING ? Tone::BUSY : Tone::NORMAL, theme::MUTED);
        ui::set_fill(r.fill, t.run == Run::RUNNING ? Tone::BUSY : Tone::NORMAL, theme::MUTED);
        const int fw = t.total_s > 0
                           ? std::max(0, std::min(width_, static_cast<int>(static_cast<uint64_t>(width_) * t.remaining_s / t.total_s)))
                           : width_;
        lv_obj_set_width(r.fill, fw);
      }
    }
  }

  Timers *plugin_;
  int tile_{-1}, width_{0}, height_{0}, visible_rows_{1};
  bool compact_{false};
  lv_obj_t *icon_{}, *header_{}, *badge_{}, *primary_{}, *secondary_{}, *empty_{};
  Row rows_[MAX_ROWS]{};
};

// ============================================================================
// All timers detail card (per-timer controls + Ask Assist + Stop all)
// ============================================================================
class OverviewCard : public tessera::Card {
 public:
  static constexpr int MAX_ROWS = 6;

  explicit OverviewCard(Timers *plugin) : plugin_(plugin) {}

  void open(const tessera::CardContext &c) override {
    width_ = c.width;
    height_ = c.height;
    const bool large = ui::large();
    const int gap = ui::px(large ? 10 : 8);
    const int btn_h = ui::px(large ? 48 : 36);
    const int hint_h = ui::line_height(Font::BODY);
    const int footer_y = std::max(0, height_ - btn_h);
    const int hint_y = std::max(0, footer_y - hint_h - ui::px(6));

    hint_ = ui::label(c.parent, Font::BODY, theme::MUTED);
    lv_obj_set_pos(hint_, 0, hint_y);
    lv_obj_set_size(hint_, width_, hint_h);
    lv_obj_set_style_text_align(hint_, LV_TEXT_ALIGN_CENTER, 0);
    ui::set_text(hint_, plugin_->text("voice_hint"));

    ask_btn_ = make_button(c.parent, 0, footer_y, width_, btn_h, theme::ACCENT, &ask_lbl_, Font::BODY_LARGE,
                           theme::ON_ACCENT, plugin_->text("ask_assist"));
    dismiss_btn_ = make_button(c.parent, 0, footer_y, width_, btn_h, theme::BUTTON_DARK, &dismiss_lbl_,
                               Font::BODY_LARGE, theme::ON_ACCENT, plugin_->text("dismiss_all"));
    btn_h_ = btn_h;
    footer_y_ = footer_y;
    gap_ = gap;

    const int list_h = std::max(1, (plugin_->has_assistant() ? hint_y : footer_y) - gap);
    const int card_pad = ui::px(large ? 10 : 6);
    const int bar_h = ui::px(large ? 5 : 4);
    const int line_h = ui::line_height(Font::TITLE);
    const int row_h = line_h + bar_h + card_pad * 2;
    const int row_step = row_h + ui::px(large ? 8 : 6);
    visible_rows_ = std::max(1, std::min(MAX_ROWS, (list_h + ui::px(6)) / std::max(1, row_step)));
    const int act_d = std::max(ui::px(28), line_h);
    const int time_w = ui::px(large ? 116 : 84);

    for (int i = 0; i < MAX_ROWS; ++i) {
      Row &r = rows_[i];
      const int y = i * row_step;
      r.box = ui::block(c.parent, theme::CARD);
      lv_obj_set_pos(r.box, 0, y);
      lv_obj_set_size(r.box, width_, row_h);

      const int iw = ui::line_height(Font::ICON_SMALL);
      r.icon = ui::label(r.box, Font::ICON_SMALL, theme::ACCENT);
      lv_obj_set_pos(r.icon, card_pad, card_pad + std::max(0, (line_h - iw) / 2));
      lv_obj_set_size(r.icon, iw + ui::px(4), iw);

      const int cancel_x = width_ - card_pad - act_d;
      const int toggle_x = cancel_x - ui::px(6) - act_d;
      r.toggle = make_button(r.box, toggle_x, card_pad, act_d, act_d, theme::BUTTON, &r.toggle_lbl, Font::ICON_SMALL,
                             theme::SLATE, ui::icon(ICON_PAUSE));
      lv_obj_set_style_radius(r.toggle, LV_RADIUS_CIRCLE, 0);
      r.toggle_rect = {toggle_x, y + card_pad, act_d, act_d};

      r.cancel = make_button(r.box, cancel_x, card_pad, act_d, act_d, theme::BUTTON, &r.cancel_lbl, Font::ICON_SMALL,
                             theme::SLATE, ui::icon(ICON_CLOSE));
      lv_obj_set_style_radius(r.cancel, LV_RADIUS_CIRCLE, 0);
      r.cancel_rect = {cancel_x, y + card_pad, act_d, act_d};

      r.time_x_with_btns = toggle_x - ui::px(6) - time_w;
      r.time_x_no_btns = width_ - card_pad - time_w;
      r.time = ui::label(r.box, Font::TITLE, theme::INK);
      lv_obj_set_style_text_align(r.time, LV_TEXT_ALIGN_RIGHT, 0);
      lv_obj_set_pos(r.time, r.time_x_with_btns, card_pad);
      lv_obj_set_size(r.time, time_w, line_h);

      r.name_x = card_pad + iw + ui::px(8);
      r.name = ui::label(r.box, Font::TITLE, theme::INK);
      lv_label_set_long_mode(r.name, LV_LABEL_LONG_DOT);
      lv_obj_set_pos(r.name, r.name_x, card_pad);
      lv_obj_set_size(r.name, std::max(1, r.time_x_with_btns - r.name_x - ui::px(6)), line_h);

      r.track_w = std::max(1, width_ - 2 * card_pad);
      r.track = ui::block(r.box, theme::TRACK);
      lv_obj_set_pos(r.track, card_pad, card_pad + line_h + ui::px(4));
      lv_obj_set_size(r.track, r.track_w, bar_h);

      r.fill = ui::block(r.track, theme::ACCENT);
      lv_obj_set_pos(r.fill, 0, 0);
      lv_obj_set_size(r.fill, 0, bar_h);
    }

    empty_ = ui::label(c.parent, Font::HEADLINE, theme::MUTED);
    lv_obj_set_style_text_align(empty_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(empty_, 0, std::max(0, (list_h - ui::line_height(Font::HEADLINE)) / 2));
    lv_obj_set_size(empty_, width_, ui::line_height(Font::HEADLINE));
    ui::set_text(empty_, plugin_->text("no_timers"));

    paint();
  }

  void on_tick(uint32_t) override { paint(); }

  void on_theme() override {
    if (hint_) ui::set_color(hint_, theme::MUTED);
    if (empty_) ui::set_color(empty_, theme::MUTED);
    if (ask_btn_) lv_obj_set_style_bg_color(ask_btn_, ui::color(theme::ACCENT), 0);
    if (ask_lbl_) ui::set_color(ask_lbl_, theme::ON_ACCENT);
    if (dismiss_btn_) lv_obj_set_style_bg_color(dismiss_btn_, ui::color(theme::BUTTON_DARK), 0);
    if (dismiss_lbl_) ui::set_color(dismiss_lbl_, theme::ON_ACCENT);
    for (auto &r : rows_) {
      if (r.box) lv_obj_set_style_bg_color(r.box, ui::color(theme::CARD), 0);
      if (r.name) ui::set_color(r.name, theme::INK);
      if (r.track) lv_obj_set_style_bg_color(r.track, ui::color(theme::TRACK), 0);
      if (r.toggle) lv_obj_set_style_bg_color(r.toggle, ui::color(theme::BUTTON), 0);
      if (r.toggle_lbl) ui::set_color(r.toggle_lbl, theme::SLATE);
      if (r.cancel) lv_obj_set_style_bg_color(r.cancel, ui::color(theme::BUTTON), 0);
      if (r.cancel_lbl) ui::set_color(r.cancel_lbl, theme::SLATE);
    }
    paint();
  }

  void on_tap() override {
    const tessera::Point p = tessera::tap_point();
    const int slack = ui::mm(1);
    auto hits = [&](const Rect &r) {
      return r.w > 0 && p.x >= r.x - slack && p.x < r.x + r.w + slack && p.y >= r.y - slack && p.y < r.y + r.h + slack;
    };
    if (dismiss_rect_.w > 0 && hits(dismiss_rect_)) {
      plugin_->silence();
      paint();
      return;
    }
    if (ask_rect_.w > 0 && hits(ask_rect_)) {
      Timers *plugin = plugin_;
      tessera::close_card();
      plugin->ask_assist();
      return;
    }
    for (int i = 0; i < count_; ++i) {
      if (items_[i].slot_index < 0) continue;
      if (hits(rows_[i].toggle_rect)) {
        plugin_->press_overview_slot(items_[i].slot_index, Key::PRIMARY);
        paint();
        return;
      }
      if (items_[i].run != Run::DONE && hits(rows_[i].cancel_rect)) {
        plugin_->press_overview_slot(items_[i].slot_index, Key::SECONDARY);
        paint();
        return;
      }
    }
  }

 private:
  struct Rect {
    int x = 0, y = 0, w = 0, h = 0;
  };
  struct Row {
    lv_obj_t *box{}, *icon{}, *name{}, *time{}, *track{}, *fill{}, *toggle{}, *toggle_lbl{}, *cancel{}, *cancel_lbl{};
    Rect toggle_rect, cancel_rect;
    int name_x{0}, time_x_with_btns{0}, time_x_no_btns{0}, track_w{1};
  };

  void paint() {
    count_ = std::min(visible_rows_, plugin_->collect_overview(items_, MAX_ROWS));
    const bool has_assist = plugin_->has_assistant();
    const bool has_done = plugin_->any_done();
    set_hidden(hint_, !has_assist);
    set_hidden(ask_btn_, !has_assist);
    set_hidden(dismiss_btn_, !has_done);

    if (has_assist && has_done) {
      const int half_w = (width_ - gap_) / 2;
      ask_rect_ = {0, footer_y_, half_w, btn_h_};
      dismiss_rect_ = {half_w + gap_, footer_y_, width_ - half_w - gap_, btn_h_};
    } else if (has_assist) {
      ask_rect_ = {0, footer_y_, width_, btn_h_};
      dismiss_rect_ = {};
    } else if (has_done) {
      ask_rect_ = {};
      dismiss_rect_ = {0, footer_y_, width_, btn_h_};
    } else {
      ask_rect_ = {};
      dismiss_rect_ = {};
    }

    if (ask_rect_.w > 0) {
      lv_obj_set_pos(ask_btn_, ask_rect_.x, ask_rect_.y);
      lv_obj_set_size(ask_btn_, ask_rect_.w, ask_rect_.h);
      lv_obj_set_width(ask_lbl_, std::max(1, ask_rect_.w - ui::px(6)));
      lv_obj_center(ask_lbl_);
    }
    if (dismiss_rect_.w > 0) {
      lv_obj_set_pos(dismiss_btn_, dismiss_rect_.x, dismiss_rect_.y);
      lv_obj_set_size(dismiss_btn_, dismiss_rect_.w, dismiss_rect_.h);
      lv_obj_set_width(dismiss_lbl_, std::max(1, dismiss_rect_.w - ui::px(6)));
      lv_obj_center(dismiss_lbl_);
    }

    set_hidden(empty_, count_ > 0);
    for (int i = 0; i < MAX_ROWS; ++i) {
      Row &r = rows_[i];
      if (i >= count_) {
        set_hidden(r.box, true);
        continue;
      }
      const OverviewItem &t = items_[i];
      set_hidden(r.box, false);
      const bool controllable = t.slot_index >= 0;
      set_hidden(r.toggle, !controllable);
      set_hidden(r.cancel, !controllable || t.run == Run::DONE);

      const int tx = controllable ? r.time_x_with_btns : r.time_x_no_btns;
      lv_obj_set_x(r.time, tx);
      lv_obj_set_width(r.name, std::max(1, tx - r.name_x - ui::px(6)));
      ui::set_text(r.name, t.label[0] ? t.label : plugin_->text("timer"));

      if (t.run == Run::DONE) {
        ui::set_text(r.icon, ui::icon(ICON_RINGING));
        ui::set_tone(r.icon, Tone::ALERT, theme::ACCENT);
        ui::set_text(r.time, plugin_->text("done"));
        ui::set_tone(r.time, Tone::ALERT, theme::INK);
        ui::set_fill(r.fill, Tone::ALERT, theme::ACCENT);
        lv_obj_set_width(r.fill, r.track_w);
        if (controllable) ui::set_text(r.toggle_lbl, ui::icon(ICON_CLOSE));
      } else {
        const uint32_t ic = t.source == OverviewItem::ASSIST ? ICON_MIC : (t.run == Run::RUNNING ? ICON_TIMER : ICON_PAUSE);
        ui::set_text(r.icon, ui::icon(ic));
        ui::set_tone(r.icon, t.run == Run::RUNNING ? Tone::BUSY : Tone::NORMAL, theme::MUTED);
        std::string txt = clock_of(t.remaining_s);
        if (t.run == Run::PAUSED) txt = std::string(plugin_->text("paused")) + " · " + txt;
        ui::set_text(r.time, txt);
        ui::set_tone(r.time, t.run == Run::RUNNING ? Tone::BUSY : Tone::NORMAL, theme::MUTED);
        ui::set_fill(r.fill, t.run == Run::RUNNING ? Tone::BUSY : Tone::NORMAL, theme::MUTED);
        const int fw = t.total_s > 0
                           ? std::max(0, std::min(r.track_w, static_cast<int>(static_cast<uint64_t>(r.track_w) * t.remaining_s / t.total_s)))
                           : r.track_w;
        lv_obj_set_width(r.fill, fw);
        if (controllable) ui::set_text(r.toggle_lbl, ui::icon(t.run == Run::RUNNING ? ICON_PAUSE : ICON_PLAY));
      }
    }
  }

  Timers *plugin_;
  int width_{0}, height_{0}, visible_rows_{1}, btn_h_{0}, footer_y_{0}, gap_{0}, count_{0};
  Rect ask_rect_, dismiss_rect_;
  OverviewItem items_[MAX_ROWS]{};
  lv_obj_t *hint_{}, *empty_{}, *ask_btn_{}, *ask_lbl_{}, *dismiss_btn_{}, *dismiss_lbl_{};
  Row rows_[MAX_ROWS]{};
};

tessera::Tile *make_tile(Timers *plugin, Kind kind) { return new TimerTile(plugin, kind); }
tessera::Card *make_card(Timers *plugin, Kind kind) { return new TimerCard(plugin, kind); }
tessera::Tile *make_overview_tile(Timers *plugin) { return new OverviewTile(plugin); }
tessera::Card *make_overview_card(Timers *plugin) { return new OverviewCard(plugin); }

}  // namespace esphome::timers
