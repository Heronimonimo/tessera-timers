#include "timers.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace esphome::timers {

using tessera::Font;
using tessera::Motion;
using tessera::Tone;
namespace ui = tessera::ui;

// The tiles and cards (tiles.cpp).
tessera::Tile *make_tile(Timers *plugin, Kind kind);
tessera::Card *make_card(Timers *plugin, Kind kind);
tessera::Tile *make_overview_tile(Timers *plugin);
tessera::Card *make_overview_card(Timers *plugin);

static constexpr uint32_t MOST_S = 24 * 3600 - 1;           // the add keys stop at 23:59:59
static constexpr int64_t RINGS_MS = 30 * 60 * 1000;          // a timer that is done says so for half an hour at most
static constexpr int64_t SOUNDS_MS = 60 * 1000;              // and rings for a minute, or until Stop
static constexpr uint32_t SAVED_VERSION = 2;
static constexpr uint32_t ICON_PLAY = 0xF040A, ICON_PAUSE = 0xF03E4, ICON_STOP = 0xF04DB, ICON_RESTART = 0xF0709,
                          ICON_CLOSE = 0xF0156, ICON_PLUS = 0xF0415, ICON_TIMER = 0xF051B, ICON_STOPWATCH = 0xF0996,
                          ICON_RINGING = 0xF009E;

struct Saved {
  uint32_t version;
  Slot slots[Timers::SLOTS];
};

std::string clock_of(uint32_t seconds) {
  char text[16];
  const unsigned h = seconds / 3600, m = seconds / 60 % 60, s = seconds % 60;
  if (h) snprintf(text, sizeof text, "%u:%02u:%02u", h, m, s);
  else snprintf(text, sizeof text, "%u:%02u", m, s);
  return text;
}

// Home Assistant writes a timer's duration as "0:08:00", a long one as "1 day, 2:00:00".
static uint32_t duration_of(const char *text) {
  if (!text || !*text) return 0;
  uint32_t days = 0;
  const char *comma = strchr(text, ',');
  if (comma) {
    days = strtoul(text, nullptr, 10);
    text = comma + 1;
  }
  unsigned h = 0, m = 0, s = 0;
  if (sscanf(text, " %u:%u:%u", &h, &m, &s) != 3) return 0;
  return days * 86400 + h * 3600 + m * 60 + s;
}

// Whole seconds, rounded up: a timer shows 0:01 until its very end.
static uint32_t seconds_up(int64_t ms) { return ms <= 0 ? 0 : static_cast<uint32_t>((ms + 999) / 1000); }

static uint32_t added(Key key) {
  switch (key) {
    case Key::ADD_10S: return 10;
    case Key::ADD_1M: return 60;
    case Key::ADD_10M: return 600;
    case Key::ADD_1H: return 3600;
    default: return 0;
  }
}

static void set_hidden(lv_obj_t *obj, bool hidden) {
  if (!obj) return;
  if (hidden) lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

// ============================================================================
// Finished-timer global popup (plugin API 0.18)
// ============================================================================
class TimerDonePopup : public tessera::Popup {
 public:
  explicit TimerDonePopup(Timers *plugin) : plugin_(plugin) {}

  void open(const tessera::PopupContext &c) override {
    width_ = c.width;
    tallest_ = c.height;
    const bool large = ui::large();
    disc_ = c.key > 0 ? c.key : ui::px(large ? 60 : 40);

    ripple_ = ui::block(c.parent, theme::ACCENT);
    lv_obj_set_size(ripple_, disc_, disc_);
    lv_obj_set_pos(ripple_, 0, 0);
    lv_obj_set_style_radius(ripple_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(ripple_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ripple_, ui::px(large ? 3 : 2), 0);
    lv_obj_set_style_border_color(ripple_, ui::tone_color(Tone::ALERT, theme::ACCENT), 0);
    ui::animate(ripple_, Motion::RIPPLE, 1400, 0);

    fill_ = ui::block(c.parent, theme::ACCENT);
    lv_obj_set_size(fill_, disc_, disc_);
    lv_obj_set_pos(fill_, 0, 0);
    lv_obj_set_style_radius(fill_, LV_RADIUS_CIRCLE, 0);
    ui::set_fill(fill_, Tone::ALERT, theme::ACCENT);

    icon_ = ui::label(fill_, Font::ICON_SMALL, theme::ON_ACCENT);
    lv_obj_set_width(icon_, disc_);
    lv_obj_set_style_text_align(icon_, LV_TEXT_ALIGN_CENTER, 0);
    ui::set_text(icon_, ui::icon(ICON_RINGING));
    lv_obj_center(icon_);

    title_ = ui::label(c.parent, Font::HEADLINE, theme::INK);
    const int title_x = disc_ + ui::px(large ? 16 : 10);
    const int title_w = std::max(1, width_ - title_x - c.corner);
    const std::string heading = plugin_->text("timer_finished");
    Font title_font = Font::BODY;
    for (Font f : {Font::HEADLINE, Font::TITLE, Font::BODY_LARGE, Font::BODY}) {
      title_font = f;
      if (ui::text_width(heading, f) <= title_w) break;
    }
    ui::set_font(title_, title_font);
    lv_obj_set_pos(title_, title_x, (disc_ - ui::line_height(title_font)) / 2);
    lv_obj_set_width(title_, title_w);
    ui::set_text(title_, heading);

    body_y_ = disc_ + ui::px(large ? 14 : 10);
    detail_ = ui::text(c.parent, Font::BODY_LARGE, theme::INK_SOFT);
    lv_obj_set_pos(detail_, 0, body_y_);
    lv_obj_set_width(detail_, width_);

    btn_h_ = ui::px(large ? 52 : 38);
    btn_ = ui::block(c.parent, theme::BUTTON_DARK);
    lv_obj_set_size(btn_, width_, btn_h_);
    lv_obj_set_pos(btn_, 0, body_y_);
    btn_label_ = ui::label(btn_, Font::BODY_LARGE, theme::ON_ACCENT);
    lv_obj_set_width(btn_label_, std::max(1, width_ - ui::px(8)));
    lv_obj_set_style_text_align(btn_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(btn_label_);

    refresh_content();
  }

  void on_tick(uint32_t) override { refresh_content(); }

  void on_theme() override {
    if (ripple_) lv_obj_set_style_border_color(ripple_, ui::tone_color(Tone::ALERT, theme::ACCENT), 0);
    if (fill_) ui::set_fill(fill_, Tone::ALERT, theme::ACCENT);
    if (title_) ui::set_color(title_, theme::INK);
    if (detail_) ui::set_color(detail_, theme::INK_SOFT);
    if (btn_) lv_obj_set_style_bg_color(btn_, ui::color(theme::BUTTON_DARK), 0);
    if (btn_label_) ui::set_color(btn_label_, theme::ON_ACCENT);
  }

  void on_tap() override {
    Timers *plugin = plugin_;
    plugin->silence();
  }

  void on_dismiss() override {
    plugin_->popup_closed(this);
    plugin_->silence();
  }

  void refresh_content() {
    if (!detail_ || !btn_) return;
    OverviewItem items[8];
    const int n = plugin_->collect_done(items, 8);
    const bool large = ui::large();
    const int gap = ui::px(large ? 14 : 10);
    std::string lines;
    const int limit = std::min(n, 4);
    for (int i = 0; i < limit; ++i) {
      if (!lines.empty()) lines += '\n';
      lines += items[i].label[0] ? items[i].label : plugin_->text("timer");
      if (items[i].total_s > 0) {
        lines += " · ";
        lines += clock_of(items[i].total_s);
      }
    }
    if (n > limit) {
      lines += "\n+";
      lines += std::to_string(n - limit);
    }
    ui::set_text(detail_, lines);
    const int max_h = std::max(ui::line_height(Font::BODY_LARGE), tallest_ - body_y_ - btn_h_ - gap);
    const int wanted_h = lines.empty() ? 0 : std::min(max_h, limit * ui::line_height(Font::BODY_LARGE) + ui::px(4));
    set_hidden(detail_, lines.empty());
    lv_obj_set_height(detail_, std::max(1, wanted_h));
    const int btn_y = body_y_ + (lines.empty() ? 0 : wanted_h + gap);
    lv_obj_set_pos(btn_, 0, btn_y);
    ui::set_text(btn_label_, plugin_->text(n > 1 ? "dismiss_all" : "dismiss"));
  }

 private:
  Timers *plugin_;
  int width_{0}, tallest_{0}, disc_{0}, body_y_{0}, btn_h_{0};
  lv_obj_t *ripple_{}, *fill_{}, *icon_{}, *title_{}, *detail_{}, *btn_{}, *btn_label_{};
};

// ============================================================================
// Timers plugin component
// ============================================================================
void Timers::setup() {
  add_tile("timer", [this]() { return make_tile(this, Kind::TIMER); });
  add_tile("stopwatch", [this]() { return make_tile(this, Kind::STOPWATCH); });
  add_tile("overview", [this]() { return make_overview_tile(this); });
  add_card("timer", [this]() { return make_card(this, Kind::TIMER); }, true);
  add_card("stopwatch", [this]() { return make_card(this, Kind::STOPWATCH); }, true);
  add_card("overview", [this]() { return make_overview_card(this); }, true);
  add_bar_item("timer", [this]() { return timer_item(); });
  add_bar_item("stopwatch", [this]() { return stopwatch_item(); });

  // What ran before a restart runs on: the slots are kept against the clock of the day, not the chip's.
  saved_ = global_preferences->make_preference<Saved>(fnv1_hash("timers.slots"));
  Saved saved{};
  if (saved_.load(&saved) && saved.version == SAVED_VERSION)
    for (int i = 0; i < SLOTS; ++i) slots_[i] = saved.slots[i];

#ifdef USE_VOICE_ASSISTANT
  if (assistant_ != nullptr) assistant_->set_has_timers(true);
#endif
}

#ifdef USE_VOICE_ASSISTANT
void Timers::attach_assist_started() {
  if (assistant_ == nullptr) return;
  auto *a = new Automation<voice_assistant::Timer>(assistant_->get_timer_started_trigger());
  a->add_action(new AssistLambdaAction<voice_assistant::Timer>([this](const voice_assistant::Timer &) { on_assist_changed(); }));
}

void Timers::attach_assist_updated() {
  if (assistant_ == nullptr) return;
  auto *a = new Automation<voice_assistant::Timer>(assistant_->get_timer_updated_trigger());
  a->add_action(new AssistLambdaAction<voice_assistant::Timer>([this](const voice_assistant::Timer &) { on_assist_changed(); }));
}

void Timers::attach_assist_cancelled() {
  if (assistant_ == nullptr) return;
  auto *a = new Automation<voice_assistant::Timer>(assistant_->get_timer_cancelled_trigger());
  a->add_action(new AssistLambdaAction<voice_assistant::Timer>([this](const voice_assistant::Timer &) { on_assist_changed(); }));
}

void Timers::attach_assist_finished() {
  if (assistant_ == nullptr) return;
  auto *a = new Automation<voice_assistant::Timer>(assistant_->get_timer_finished_trigger());
  a->add_action(new AssistLambdaAction<voice_assistant::Timer>([this](const voice_assistant::Timer &t) { on_assist_finished(t); }));
}

void Timers::attach_assist_tick() {
  if (assistant_ == nullptr) return;
  auto *a = new Automation<const std::vector<voice_assistant::Timer> &>(assistant_->get_timer_tick_trigger());
  a->add_action(new AssistLambdaAction<const std::vector<voice_assistant::Timer> &>(
      [this](const std::vector<voice_assistant::Timer> &) { on_assist_changed(); }));
}

void Timers::on_assist_changed() { tessera::refresh(); }

void Timers::on_assist_finished(const voice_assistant::Timer &timer) {
  int slot_idx = -1;
  for (int i = 0; i < MAX_ASSIST_DONE; ++i) {
    if (assist_done_[i].active && !timer.id.empty() && strcmp(assist_done_[i].id, timer.id.c_str()) == 0) {
      slot_idx = i;
      break;
    }
    if (!assist_done_[i].active && slot_idx < 0) slot_idx = i;
  }
  if (slot_idx < 0) slot_idx = 0;
  AssistDone &entry = assist_done_[slot_idx];
  entry.active = true;
  snprintf(entry.id, sizeof entry.id, "%s", timer.id.c_str());
  snprintf(entry.name, sizeof entry.name, "%s", timer.name.c_str());
  entry.total_s = timer.total_seconds;
  entry.done_chip_ms = millis();
  notify_done(-1);
  tessera::refresh();
}
#endif

void Timers::ask_assist() {
#ifdef USE_VOICE_ASSISTANT
  if (assistant_ != nullptr) assistant_->request_start(false, true);
#endif
}

int64_t Timers::now_ms() {
  const uint32_t epoch = tessera::epoch();
  if (!epoch) return 0;
  // The clock has whole seconds; millis() fills in between, and is set right when the two part by more than 1.5 s.
  const uint32_t ms = millis();
  if (!base_ms_) {
    base_ms_ = static_cast<int64_t>(epoch) * 1000;
    base_millis_ = ms;
  }
  int64_t now = base_ms_ + static_cast<uint32_t>(ms - base_millis_);
  const int64_t drift = now - static_cast<int64_t>(epoch) * 1000;
  if (drift < -1500 || drift > 1500) {
    base_ms_ = static_cast<int64_t>(epoch) * 1000;
    base_millis_ = ms;
    now = base_ms_;
  }
  return now;
}

Slot &Timers::slot(int tile, Kind kind) {
  if (tile < 0) {
    if (slots_[SLOTS].kind != kind) slots_[SLOTS] = Slot{-1, kind};
    return slots_[SLOTS];
  }
  for (int i = 0; i < SLOTS; ++i)
    if (slots_[i].tile == tile && slots_[i].kind == kind) return slots_[i];
  // A new one: a free slot, else one that is not running.
  int pick = -1;
  for (int i = 0; i < SLOTS && pick < 0; ++i)
    if (slots_[i].kind == Kind::NONE) pick = i;
  for (int i = 0; i < SLOTS && pick < 0; ++i)
    if (slots_[i].run == Run::IDLE) pick = i;
  if (pick < 0) pick = 0;
  slots_[pick] = Slot{static_cast<int16_t>(tile), kind};
  helpers_[pick] = Helper{};
  labels_[pick][0] = '\0';
  changed();
  return slots_[pick];
}

void Timers::set_label(const Slot &s, const char *label) {
  if (!label || !*label) return;
  const ptrdiff_t idx = &s - slots_;
  if (idx < 0 || idx > SLOTS) return;
  snprintf(labels_[idx], sizeof labels_[idx], "%s", label);
}

const char *Timers::label_of(int index) const {
  if (index >= 0 && index <= SLOTS && labels_[index][0]) return labels_[index];
  return text("timer");
}

void Timers::changed() {
  dirty_ = true;
  tessera::refresh();
}

void Timers::show_done_popup() {
  if (popup_ != nullptr && tessera::popup_shown(this)) {
    popup_->refresh_content();
    return;
  }
  popup_ = nullptr;
  tessera::show_popup(
      this,
      [this]() -> tessera::Popup * {
        popup_ = new TimerDonePopup(this);
        return popup_;
      },
      tessera::PopupWidth::ALERT);
}

void Timers::notify_done(int16_t tile) {
#ifdef USE_SPEAKER
  alarm_active_ = true;
  alarm_started_ms_ = millis();
  rang_at_ = alarm_started_ms_ - 2000;
#endif
  if (popup_switch_ == nullptr || popup_switch_->state) {
    show_done_popup();
  } else if (tile >= 0 && (opens_ == nullptr || opens_->state)) {
    const char *entity = "";
    for (int i = 0; i < SLOTS; ++i) {
      if (slots_[i].tile == tile && slots_[i].kind == Kind::TIMER) {
        entity = helpers_[i].entity;
        break;
      }
    }
    tessera::open_card(plugin_id(), "timer", entity, tile);
  }
}

void Timers::read(Slot &s, const char *entity, JsonObjectConst data, Helper &h) {
  const char *state = data["state"] | "";
  if (!*state) return;
  snprintf(h.entity, sizeof h.entity, "%s", entity);
  const char *friendly = data["name"] | "";
  if (*friendly) set_label(s, friendly);
  h.known = true;
  h.available = strcmp(state, "unavailable") != 0 && strcmp(state, "unknown") != 0;
  h.run = !strcmp(state, "active") ? Run::RUNNING : !strcmp(state, "paused") ? Run::PAUSED : Run::IDLE;
  JsonObjectConst attributes = data["attributes"];
  h.duration_s = duration_of(attributes["duration"] | "");
  h.remaining_s = duration_of(attributes["remaining"] | "");
  h.finishes_at = attributes["finishes_at"] | 0u;
  helper(s) = h;

  // Home Assistant says a timer is idle both when it rang and when it was cancelled: the end it had tells them apart.
  const uint32_t now_s = static_cast<uint32_t>(now_ms() / 1000);
  if (h.run == Run::RUNNING && h.finishes_at && h.finishes_at != s.finishes_at) {
    s.finishes_at = h.finishes_at;
    if (s.run == Run::DONE) s.run = Run::IDLE;
    changed();
  } else if (h.run == Run::PAUSED && s.finishes_at) {
    s.finishes_at = 0;
    changed();
  } else if (h.run == Run::IDLE && s.finishes_at) {
    if (now_s && now_s + 3 >= s.finishes_at && now_s < s.finishes_at + RINGS_MS / 1000) {
      s.run = Run::DONE;
      s.done_ms = static_cast<int64_t>(s.finishes_at) * 1000;
      notify_done(s.tile);
    }
    s.finishes_at = 0;
    changed();
  }
}

void Timers::stop_alarm_audio() {
#ifdef USE_SPEAKER
  sounds_.stop();
  alarm_active_ = false;
  if (volume_raised_) {
    tessera::audio::restore_volume(this);
    volume_raised_ = false;
  }
  if (audio_claimed_) {
    audio_claimed_ = false;
    tessera::audio::release(tessera::audio::Side::OUT, tessera::audio::Role::VOICE, AUDIO_CLAIM_ID);
  }
#endif
}

void Timers::silence_sound() { stop_alarm_audio(); }

void Timers::popup_off() {
  if (popup_ != nullptr) {
    popup_ = nullptr;
    tessera::close_popup(this);
  }
}

void Timers::popup_closed(TimerDonePopup *who) {
  if (popup_ == who) popup_ = nullptr;
}

bool Timers::any_done() const {
  for (int i = 0; i <= SLOTS; ++i)
    if (slots_[i].kind == Kind::TIMER && slots_[i].run == Run::DONE) return true;
  for (int i = 0; i < MAX_ASSIST_DONE; ++i)
    if (assist_done_[i].active) return true;
  return false;
}

void Timers::silence() {
  bool any = false;
  for (int i = 0; i <= SLOTS; ++i) {
    Slot &s = slots_[i];
    if (s.kind == Kind::TIMER && s.run == Run::DONE) {
      s.run = Run::IDLE;
      any = true;
    }
  }
  for (int i = 0; i < MAX_ASSIST_DONE; ++i) {
    if (assist_done_[i].active) {
      assist_done_[i].active = false;
      any = true;
    }
  }
  stop_alarm_audio();
  popup_off();
  if (any) changed();
  else tessera::refresh();
}

void Timers::start_helper(const char *entity, uint32_t seconds) {
  if (!seconds) {
    tessera::action("timer.start", entity);
    return;
  }
  // timer.start takes its duration with the hours always there: "0:08:00".
  JsonDocument fields;
  char text[16];
  const uint32_t t = std::min(seconds, MOST_S);
  snprintf(text, sizeof text, "%u:%02u:%02u", static_cast<unsigned>(t / 3600), static_cast<unsigned>(t / 60 % 60),
           static_cast<unsigned>(t % 60));
  fields["duration"] = text;
  tessera::action("timer.start", entity, fields.as<JsonVariantConst>());
}

void Timers::press_overview_slot(int slot_index, Key key) {
  if (slot_index < 0 || slot_index > SLOTS) return;
  Slot &s = slots_[slot_index];
  if (s.kind != Kind::TIMER) return;
  const Helper &h = helpers_[slot_index];
  press(s, key, h.entity, h);
}

void Timers::press(Slot &s, Key key, const char *entity, const Helper &h) {
  const int64_t now = now_ms();
  const uint32_t add = added(key);
  const bool on_helper = entity && *entity;

  if (s.kind == Kind::STOPWATCH) {
    if (!now) return;
    const int64_t ran = s.run == Run::RUNNING ? now - s.mark_ms : s.run == Run::PAUSED ? s.held_ms : 0;
    if (key == Key::PRIMARY) {
      if (s.run == Run::RUNNING) {
        s.held_ms = ran;
        s.run = Run::PAUSED;
      } else {
        s.mark_ms = now - ran;
        s.run = Run::RUNNING;
      }
    } else if (key == Key::SECONDARY) {
      if (s.run == Run::RUNNING) {
        for (int i = 2; i > 0; --i) s.lap_ms[i] = s.lap_ms[i - 1];
        s.lap_ms[0] = static_cast<uint32_t>(ran - s.lap_mark);
        s.lap_mark = ran;
        if (s.laps < 255) s.laps++;
      } else {
        const int16_t tile = s.tile;
        s = Slot{tile, Kind::STOPWATCH};
      }
    }
    changed();
    return;
  }

  if (on_helper) {
    // A timer helper: Home Assistant runs it; the screen only keeps the time put together before it starts.
    if (!h.available) return;
    const Run run = s.run == Run::DONE ? Run::DONE : h.run;
    const uint32_t now_s = static_cast<uint32_t>(now / 1000);
    const uint32_t left = run == Run::RUNNING ? (h.finishes_at > now_s ? h.finishes_at - now_s : 0)
                          : run == Run::PAUSED ? h.remaining_s
                                               : 0;
    if (add) {
      if (run == Run::IDLE) s.set_s = std::min(s.set_s + add, MOST_S);
      else start_helper(entity, left + add);   // a running timer starts again with the time added
      if (run == Run::DONE) s.run = Run::IDLE;
    } else if (key == Key::PRIMARY) {
      if (run == Run::DONE) s.run = Run::IDLE;
      else if (run == Run::RUNNING) tessera::action("timer.pause", entity);
      else if (run == Run::PAUSED) tessera::action("timer.start", entity);
      else {
        start_helper(entity, s.set_s);
        s.set_s = 0;
      }
    } else if (key == Key::SECONDARY) {
      if (run == Run::DONE) {
        start_helper(entity, 60);
        s.run = Run::IDLE;
      } else if (run == Run::IDLE) s.set_s = 0;
      else tessera::action("timer.cancel", entity);
    }
    if (!any_done()) {
      stop_alarm_audio();
      popup_off();
    }
    changed();
    return;
  }

  // A timer on the screen.
  if (!now) return;
  auto begin = [&](uint32_t seconds) {
    s.total_s = seconds;
    s.mark_ms = now + static_cast<int64_t>(seconds) * 1000;
    s.run = Run::RUNNING;
  };
  if (add) {
    switch (s.run) {
      case Run::DONE: begin(add); break;   // done: +1m is another minute
      case Run::RUNNING: s.mark_ms += add * 1000LL; s.total_s += add; break;
      case Run::PAUSED: s.held_ms += add * 1000LL; s.total_s += add; break;
      case Run::IDLE: s.set_s = std::min(s.set_s + add, MOST_S); break;
    }
  } else if (key == Key::PRIMARY) {
    switch (s.run) {
      case Run::IDLE: if (s.set_s) begin(s.set_s); break;
      case Run::RUNNING: s.held_ms = std::max<int64_t>(0, s.mark_ms - now); s.run = Run::PAUSED; break;
      case Run::PAUSED: s.mark_ms = now + s.held_ms; s.run = Run::RUNNING; break;
      case Run::DONE: s.run = Run::IDLE; break;
    }
  } else if (key == Key::SECONDARY) {
    switch (s.run) {
      case Run::IDLE: s.set_s = 0; break;
      case Run::RUNNING:
      case Run::PAUSED: s.run = Run::IDLE; break;
      case Run::DONE: begin(60); break;
    }
  }
  if (!any_done()) {
    stop_alarm_audio();
    popup_off();
  }
  changed();
}

View Timers::view(Slot &s, const char *entity, const Helper &h) {
  View v;
  v.kind = s.kind;
  const int64_t now = now_ms();

  if (s.kind == Kind::STOPWATCH) {
    v.run = s.run;
    const int64_t ran = s.run == Run::RUNNING ? (now ? now - s.mark_ms : 0) : s.run == Run::PAUSED ? s.held_ms : 0;
    v.time = clock_of(static_cast<uint32_t>(std::max<int64_t>(0, ran) / 1000));
    v.progress = static_cast<int>(std::max<int64_t>(0, ran) % 60000 / 60);
    v.tone = s.run == Run::RUNNING ? Tone::ACCENT : Tone::NORMAL;
    v.primary = text(s.run == Run::RUNNING ? "pause" : s.run == Run::PAUSED ? "resume" : "start");
    v.primary_icon = s.run == Run::RUNNING ? ICON_PAUSE : ICON_PLAY;
    v.secondary = text(s.run == Run::RUNNING ? "lap" : "reset");
    v.secondary_icon = s.run == Run::RUNNING ? ICON_PLUS : ICON_RESTART;
    v.laps = std::min<uint8_t>(s.laps, 3);
    for (int i = 0; i < v.laps; ++i)
      v.lap[i] = tessera::fill(text("lap_n"), "n", std::to_string(s.laps - i)) + "  " + clock_of(s.lap_ms[i] / 1000);
    v.sub = v.laps ? v.lap[0] : s.run == Run::PAUSED ? text("paused") : "";
    if (!now && s.run == Run::RUNNING) v.sub = text("no_clock");
    return v;
  }

  const bool on_helper = entity && *entity;
  Run run = s.run;
  uint32_t left = 0, total = 0;
  if (on_helper) {
    if (h.known && !h.available) {
      v.time = "-";
      v.sub = text("unavailable");
      v.primary = text("start");
      v.primary_icon = ICON_PLAY;
      v.secondary = text("reset");
      v.secondary_icon = ICON_RESTART;
      return v;
    }
    if (run != Run::DONE) run = h.run;
    const uint32_t now_s = static_cast<uint32_t>(now / 1000);
    if (run == Run::RUNNING) {
      left = h.finishes_at > now_s ? h.finishes_at - now_s : 0;
      total = std::max(h.duration_s, left);
    } else if (run == Run::PAUSED) {
      left = h.remaining_s;
      total = std::max(h.duration_s, left);
    } else if (run == Run::IDLE) {
      left = total = s.set_s ? s.set_s : h.duration_s;
    }
  } else {
    if (run == Run::RUNNING) left = seconds_up(s.mark_ms - now);
    else if (run == Run::PAUSED) left = seconds_up(s.held_ms);
    else if (run == Run::IDLE) left = s.set_s;
    total = std::max(s.total_s, left);
  }

  v.run = run;
  v.time = run == Run::DONE ? std::string(text("done")) : clock_of(left);
  v.progress = run == Run::DONE ? 1000 : run == Run::IDLE || !total ? 0 : static_cast<int>(1000LL * left / total);
  v.tone = run == Run::DONE ? Tone::ALERT : run == Run::IDLE ? Tone::NORMAL : Tone::BUSY;
  switch (run) {
    case Run::IDLE:
      v.sub = text("ready");
      v.primary = text("start");
      v.primary_icon = ICON_PLAY;
      v.secondary = text("reset");
      v.secondary_icon = ICON_RESTART;
      break;
    case Run::RUNNING:
      v.sub = tessera::fill(text("of"), "time", clock_of(total));
      v.primary = text("pause");
      v.primary_icon = ICON_PAUSE;
      v.secondary = text("cancel");
      v.secondary_icon = ICON_CLOSE;
      break;
    case Run::PAUSED:
      v.sub = text("paused");
      v.primary = text("resume");
      v.primary_icon = ICON_PLAY;
      v.secondary = text("cancel");
      v.secondary_icon = ICON_CLOSE;
      break;
    case Run::DONE:
      v.primary = text("stop");
      v.primary_icon = ICON_STOP;
      v.secondary = text("add_1m");
      break;
  }
  if (!on_helper && !now && run != Run::IDLE) v.sub = text("no_clock");
  return v;
}

int Timers::collect_done(OverviewItem *out, int max_items) const {
  int count = 0;
  for (int i = 0; i < MAX_ASSIST_DONE && count < max_items; ++i) {
    if (!assist_done_[i].active) continue;
    OverviewItem &item = out[count++];
    item = OverviewItem{};
    item.source = OverviewItem::ASSIST;
    item.run = Run::DONE;
    item.slot_index = -1;
    snprintf(item.label, sizeof item.label, "%s", assist_done_[i].name[0] ? assist_done_[i].name : text("timer"));
    item.total_s = assist_done_[i].total_s;
    item.remaining_s = 0;
  }
  for (int i = 0; i <= SLOTS && count < max_items; ++i) {
    const Slot &s = slots_[i];
    if (s.kind != Kind::TIMER || s.run != Run::DONE) continue;
    OverviewItem &item = out[count++];
    item = OverviewItem{};
    item.source = helpers_[i].entity[0] ? OverviewItem::HELPER : OverviewItem::SCREEN;
    item.run = Run::DONE;
    item.slot_index = static_cast<int16_t>(i);
    snprintf(item.label, sizeof item.label, "%s", label_of(i));
    item.total_s = helpers_[i].entity[0] ? helpers_[i].duration_s : s.total_s;
    item.remaining_s = 0;
  }
  return count;
}

int Timers::collect_overview(OverviewItem *out, int max_items) {
  int count = collect_done(out, max_items);
  const int64_t now = now_ms();
  const uint32_t now_s = static_cast<uint32_t>(now / 1000);

#ifdef USE_VOICE_ASSISTANT
  if (assistant_ != nullptr) {
    for (const auto &t : assistant_->get_timers()) {
      if (count >= max_items) break;
      OverviewItem &item = out[count++];
      item = OverviewItem{};
      item.source = OverviewItem::ASSIST;
      item.run = t.is_active ? Run::RUNNING : Run::PAUSED;
      item.slot_index = -1;
      snprintf(item.label, sizeof item.label, "%s", t.name.empty() ? text("timer") : t.name.c_str());
      item.remaining_s = t.seconds_left;
      item.total_s = std::max(t.total_seconds, t.seconds_left);
    }
  }
#endif

  for (int i = 0; i < SLOTS && count < max_items; ++i) {
    const Slot &s = slots_[i];
    if (s.kind != Kind::TIMER || s.run == Run::DONE) continue;
    const Helper &h = helpers_[i];
    if (h.entity[0]) {
      if (!h.known || !h.available || h.run == Run::IDLE) continue;
      OverviewItem &item = out[count++];
      item = OverviewItem{};
      item.source = OverviewItem::HELPER;
      item.run = h.run;
      item.slot_index = static_cast<int16_t>(i);
      snprintf(item.label, sizeof item.label, "%s", label_of(i));
      item.remaining_s = h.run == Run::RUNNING ? (now_s && h.finishes_at > now_s ? h.finishes_at - now_s : 0) : h.remaining_s;
      item.total_s = std::max(h.duration_s, item.remaining_s);
    } else if (s.run == Run::RUNNING || s.run == Run::PAUSED) {
      OverviewItem &item = out[count++];
      item = OverviewItem{};
      item.source = OverviewItem::SCREEN;
      item.run = s.run;
      item.slot_index = static_cast<int16_t>(i);
      snprintf(item.label, sizeof item.label, "%s", label_of(i));
      item.remaining_s = s.run == Run::RUNNING ? (now ? seconds_up(s.mark_ms - now) : s.total_s) : seconds_up(s.held_ms);
      item.total_s = std::max(s.total_s, item.remaining_s);
    }
  }

  std::sort(out, out + count, [](const OverviewItem &a, const OverviewItem &b) {
    auto rank = [](Run r) { return r == Run::DONE ? 0 : r == Run::RUNNING ? 1 : 2; };
    if (rank(a.run) != rank(b.run)) return rank(a.run) < rank(b.run);
    return a.remaining_s < b.remaining_s;
  });
  return count;
}

void Timers::on_interval(uint32_t now_ms_chip) {
  const int64_t now = now_ms();
  bool ringing = false, sounding = false;
  if (now) {
    for (int i = 0; i < SLOTS; ++i) {
      Slot &s = slots_[i];
      if (s.kind != Kind::TIMER) continue;
      // Only a timer on the screen runs here; a timer helper's slot is idle or done.
      if (s.run == Run::RUNNING && now >= s.mark_ms) {
        s.run = Run::DONE;
        s.done_ms = s.mark_ms;
        changed();
        notify_done(s.tile);
      }
      if (s.run == Run::DONE && now - s.done_ms > RINGS_MS) {
        s.run = Run::IDLE;
        changed();
      }
      ringing = ringing || s.run == Run::DONE;
      sounding = sounding || (s.run == Run::DONE && now - s.done_ms < SOUNDS_MS);
    }
  }
  for (int i = 0; i < MAX_ASSIST_DONE; ++i) {
    if (!assist_done_[i].active) continue;
    const uint32_t elapsed = now_ms_chip - assist_done_[i].done_chip_ms;
    if (elapsed > static_cast<uint32_t>(RINGS_MS)) {
      assist_done_[i].active = false;
      tessera::refresh();
      continue;
    }
    ringing = true;
    if (elapsed < static_cast<uint32_t>(SOUNDS_MS)) sounding = true;
  }
#ifdef USE_SPEAKER
  if (alarm_active_) {
    if (now_ms_chip - alarm_started_ms_ < static_cast<uint32_t>(SOUNDS_MS)) sounding = sounding || ringing;
    else alarm_active_ = false;
  }
  // The alarm again every two seconds while a timer rings, unless its switch is off; Stop, +1m, Cancel or the switch
  // ends it at once.
  if (sound_on_ != nullptr && !sound_on_->state) sounding = false;
  if (sounding && alarm_.length > 0) {
    if (!audio_claimed_) {
      audio_claimed_ = tessera::audio::claim(tessera::audio::Side::OUT, tessera::audio::Role::VOICE, AUDIO_CLAIM_ID, [this]() {
        audio_claimed_ = false;
        sounds_.stop();
        alarm_active_ = false;
        if (volume_raised_) {
          tessera::audio::restore_volume(this);
          volume_raised_ = false;
        }
      });
    }
    if (audio_claimed_) {
      if (!volume_raised_ && alarm_volume_ != nullptr && alarm_volume_->has_state() && alarm_volume_->state > 0.5f) {
        const float level = std::max(0.1f, std::min(1.0f, alarm_volume_->state / 100.0f));
        volume_raised_ = tessera::audio::raise_volume(this, level);
      }
      if (!sounds_.playing() && now_ms_chip - rang_at_ >= 2000) {
        sounds_.play(alarm_);
        rang_at_ = now_ms_chip;
      }
    }
  } else if (sounds_.playing() || audio_claimed_ || volume_raised_) {
    stop_alarm_audio();
  }
#endif
  if (!ringing && popup_ != nullptr) popup_off();
  if (done_ != nullptr && (ringing != ringing_ || !done_->has_state())) {
    ringing_ = ringing;
    done_->publish_state(ringing);
  }
  if (done_list_ != nullptr) {
    // A timer helper drops out of the list once Stop, +1m or Cancel is tapped on it, or after half an hour. A state
    // of Home Assistant holds 255 characters: a helper that would not fit whole is left out.
    tessera::Text names;
    for (int i = 0; i < SLOTS; ++i) {
      const Slot &s = slots_[i];
      const char *entity = helpers_[i].entity;
      if (s.kind != Kind::TIMER || s.run != Run::DONE || !*entity) continue;
      if (names.size() + (names.empty() ? 0 : 2) + strlen(entity) > 255) continue;
      if (!names.empty()) names += ", ";
      names += entity;
    }
    if (names != done_names_ || !done_list_->has_state()) {
      done_names_ = names;
      done_list_->publish_state(std::string(names.data(), names.size()));
    }
  }
  // Kept at most once a second, so a run of taps writes the flash once.
  if (dirty_ && now_ms_chip - saved_at_ >= 1000) {
    Saved saved{};
    saved.version = SAVED_VERSION;
    for (int i = 0; i < SLOTS; ++i) saved.slots[i] = slots_[i];
    saved_.save(&saved);
    dirty_ = false;
    saved_at_ = now_ms_chip;
  }
}

// "6 min", "1 h 05", "< 1 min": the bar asks every five seconds, so it says whole minutes. A timer rounds up (it says
// 1 min until its last minute), a stopwatch down.
std::string Timers::minutes_text(uint32_t seconds, bool up) {
  const uint32_t minutes = up ? (seconds + 59) / 60 : seconds / 60;
  if (!minutes || (up && seconds < 60)) return text("under_minute");
  if (minutes < 60) return tessera::format(text("minutes"), static_cast<long>(minutes));
  char rest[4];
  snprintf(rest, sizeof rest, "%02u", static_cast<unsigned>(minutes % 60));
  return tessera::fill(tessera::fill(text("hours"), "h", std::to_string(minutes / 60)).c_str(), "m", rest);
}

tessera::BarItem Timers::timer_item() {
  tessera::BarItem item;
  if (any_done()) {
    item.shown = true;
    item.icon = ICON_RINGING;
    item.text = text("done");
    item.tone = Tone::ALERT;
    return item;
  }
  const int64_t now = now_ms();
  uint32_t first = UINT32_MAX;
#ifdef USE_VOICE_ASSISTANT
  if (assistant_ != nullptr) {
    for (const auto &t : assistant_->get_timers()) {
      if (t.is_active) first = std::min(first, t.seconds_left);
    }
  }
#endif
  if (now) {
    for (int i = 0; i < SLOTS; ++i) {
      const Slot &s = slots_[i];
      if (s.kind != Kind::TIMER) continue;
      if (s.run == Run::RUNNING) first = std::min(first, seconds_up(s.mark_ms - now));
      // A timer helper, as its tile last saw it.
      const Helper &h = helpers_[i];
      if (s.run != Run::DONE && h.known && h.available && h.run == Run::RUNNING &&
          static_cast<int64_t>(h.finishes_at) * 1000 > now)
        first = std::min(first, seconds_up(static_cast<int64_t>(h.finishes_at) * 1000 - now));
    }
  }
  if (first != UINT32_MAX) {
    item.shown = true;
    item.icon = ICON_TIMER;
    item.text = minutes_text(first, true);
    item.tone = Tone::BUSY;
  }
  return item;
}

tessera::BarItem Timers::stopwatch_item() {
  tessera::BarItem item;
  const int64_t now = now_ms();
  if (!now) return item;
  int64_t longest = -1;
  for (int i = 0; i < SLOTS; ++i)
    if (slots_[i].kind == Kind::STOPWATCH && slots_[i].run == Run::RUNNING)
      longest = std::max(longest, now - slots_[i].mark_ms);
  if (longest >= 0) {
    item.shown = true;
    item.icon = ICON_STOPWATCH;
    item.text = minutes_text(static_cast<uint32_t>(longest / 1000), false);
    item.tone = Tone::ACCENT;
  }
  return item;
}

bool Timers::settings(tessera::SettingsPage &page) {
  page.icon = "\U000F051B";
  if (popup_switch_ != nullptr) {
    page.toggle(text("popup_when_done"), [this] { return popup_switch_->state; },
                [this](bool on) { on ? popup_switch_->turn_on() : popup_switch_->turn_off(); });
  }
  page.toggle(text("opens_when_done"), [this] { return opens_ == nullptr || opens_->state; },
              [this](bool on) {
                if (opens_ != nullptr) on ? opens_->turn_on() : opens_->turn_off();
              });
#ifdef USE_SPEAKER
  if (sound_on_ != nullptr)
    page.toggle(text("sound"), [this] { return sound_on_->state; },
                [this](bool on) { on ? sound_on_->turn_on() : sound_on_->turn_off(); });
  if (alarm_volume_ != nullptr) {
    page.number(
        text("alarm_volume"), 0, 100, 10, "%",
        [this] { return alarm_volume_->has_state() ? static_cast<int>(alarm_volume_->state + 0.5f) : 80; },
        [this](int v) { alarm_volume_->make_call().set_value(static_cast<float>(v)).perform(); });
  }
#endif
  return true;
}

}  // namespace esphome::timers
