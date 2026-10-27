#include "timers.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace esphome::timers {

using tessera::Tone;

// The tiles and cards (tiles.cpp).
tessera::Tile *make_tile(Timers *plugin, Kind kind);
tessera::Card *make_card(Timers *plugin, Kind kind);

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

void Timers::setup() {
  add_tile("timer", [this]() { return make_tile(this, Kind::TIMER); });
  add_tile("stopwatch", [this]() { return make_tile(this, Kind::STOPWATCH); });
  add_card("timer", [this]() { return make_card(this, Kind::TIMER); }, true);
  add_card("stopwatch", [this]() { return make_card(this, Kind::STOPWATCH); }, true);
  add_bar_item("timer", [this]() { return timer_item(); });
  add_bar_item("stopwatch", [this]() { return stopwatch_item(); });

  // What ran before a restart runs on: the slots are kept against the clock of the day, not the chip's.
  saved_ = global_preferences->make_preference<Saved>(fnv1_hash("timers.slots"));
  Saved saved{};
  if (saved_.load(&saved) && saved.version == SAVED_VERSION)
    for (int i = 0; i < SLOTS; ++i) slots_[i] = saved.slots[i];
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
  changed();
  return slots_[pick];
}

void Timers::changed() {
  dirty_ = true;
  tessera::refresh();
}

void Timers::read(Slot &s, JsonObjectConst data, Helper &h) {
  const char *state = data["state"] | "";
  if (!*state) return;
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
    }
    s.finishes_at = 0;
    changed();
  }
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
        if (opens_ == nullptr || opens_->state) tessera::open_card(plugin_id(), "timer", "", s.tile);
      }
      if (s.run == Run::DONE && now - s.done_ms > RINGS_MS) {
        s.run = Run::IDLE;
        changed();
      }
      ringing = ringing || s.run == Run::DONE;
      sounding = sounding || (s.run == Run::DONE && now - s.done_ms < SOUNDS_MS);
    }
  }
#ifdef USE_SPEAKER
  // The alarm again every two seconds while a timer rings, unless its switch is off; Stop, +1m, Cancel or the switch
  // ends it at once.
  if (sound_on_ != nullptr && !sound_on_->state) sounding = false;
  if (sounding && !sounds_.playing() && now_ms_chip - rang_at_ >= 2000) {
    sounds_.play(alarm_);
    rang_at_ = now_ms_chip;
  } else if (!sounding && sounds_.playing()) {
    sounds_.stop();
  }
#endif
  if (done_ != nullptr && (ringing != ringing_ || !done_->has_state())) {
    ringing_ = ringing;
    done_->publish_state(ringing);
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
  const int64_t now = now_ms();
  if (!now) return item;
  bool done = false;
  uint32_t first = UINT32_MAX;
  for (int i = 0; i < SLOTS; ++i) {
    const Slot &s = slots_[i];
    if (s.kind != Kind::TIMER) continue;
    if (s.run == Run::DONE) done = true;
    if (s.run == Run::RUNNING) first = std::min(first, seconds_up(s.mark_ms - now));
    // A timer helper, as its tile last saw it.
    const Helper &h = helpers_[i];
    if (s.run != Run::DONE && h.known && h.available && h.run == Run::RUNNING &&
        static_cast<int64_t>(h.finishes_at) * 1000 > now)
      first = std::min(first, seconds_up(static_cast<int64_t>(h.finishes_at) * 1000 - now));
  }
  if (done) {
    item.shown = true;
    item.icon = ICON_RINGING;
    item.text = text("done");
    item.tone = Tone::ALERT;
  } else if (first != UINT32_MAX) {
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
  page.toggle(text("opens_when_done"), [this] { return opens_ == nullptr || opens_->state; },
              [this](bool on) {
                if (opens_ != nullptr) on ? opens_->turn_on() : opens_->turn_off();
              });
#ifdef USE_SPEAKER
  if (sound_on_ != nullptr)
    page.toggle(text("sound"), [this] { return sound_on_->state; },
                [this](bool on) { on ? sound_on_->turn_on() : sound_on_->turn_off(); });
#endif
  return true;
}

}  // namespace esphome::timers
