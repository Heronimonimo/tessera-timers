#pragma once
#include "esphome/core/component.h"
#include "esphome/core/preferences.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/smart_display/plugin_api.h"
#include "face.h"

namespace esphome::timers {

// One timer or stopwatch, by the tile of the layout it belongs to. A tile object comes and goes with its page on a
// board without PSRAM, so what runs lives here, in the plugin; it is plain data, kept in flash as it is.
struct Slot {
  int16_t tile = -1;
  Kind kind = Kind::NONE;
  Run run = Run::IDLE;
  uint32_t set_s = 0;        // a timer: the time put together with the add keys, before it starts
  uint32_t total_s = 0;      // a timer: how long this run is, with what was added while it ran (the whole ring)
  int64_t mark_ms = 0;       // running: a timer's end; a stopwatch's start, as if it had never paused
  int64_t held_ms = 0;       // paused: what a timer has left; the time a stopwatch ran
  int64_t done_ms = 0;       // a timer: when its time was up
  uint32_t finishes_at = 0;  // a timer helper: its end while it runs, to tell its end from a cancel
  uint8_t laps = 0;          // a stopwatch: the laps taken
  uint32_t lap_ms[3] = {};   // their times, the newest first
  int64_t lap_mark = 0;      // the time the stopwatch had at its last lap
};

// A timer helper of Home Assistant, as the tile's data says it is now.
struct Helper {
  bool known = false, available = false;
  Run run = Run::IDLE;
  uint32_t duration_s = 0, remaining_s = 0, finishes_at = 0;
};

class Timers : public Component, public tessera::Plugin {
 public:
  static constexpr int SLOTS = 16;

  void setup() override;
  float get_setup_priority() const override { return setup_priority::DATA; }
  void on_interval(uint32_t now_ms) override;
  bool settings(tessera::SettingsPage &page) override;

  void set_done(binary_sensor::BinarySensor *s) { done_ = s; }
  void set_opens(switch_::Switch *s) { opens_ = s; }

  // The screen's clock in milliseconds since 1970, 0 until Home Assistant set it.
  int64_t now_ms();
  // The slot of a tile (its index in the layout), made when it has none.
  Slot &slot(int tile, Kind kind);
  // Read a timer helper from a tile's data into `h`, and see whether it rang since the slot last saw it. The slot
  // keeps a copy for the card the tile opens.
  void read(Slot &s, JsonObjectConst data, Helper &h);
  const Helper &helper_of(const Slot &s) { return helper(s); }
  // A key tapped on a face of the slot; `entity` is the timer helper ("" for a timer on the screen).
  void press(Slot &s, Key key, const char *entity, const Helper &h);
  // What a face of the slot shows now.
  View view(Slot &s, const char *entity, const Helper &h);

 protected:
  void changed();
  void start_helper(const char *entity, uint32_t seconds);
  Helper &helper(const Slot &s) { return helpers_[&s - slots_]; }

  // SLOTS kept in flash, and one more for a card opened without a tile.
  Slot slots_[SLOTS + 1];
  Helper helpers_[SLOTS + 1];
  ESPPreferenceObject saved_;
  bool dirty_ = false, ringing_ = false;
  uint32_t saved_at_ = 0;
  int64_t base_ms_ = 0;
  uint32_t base_millis_ = 0;
  binary_sensor::BinarySensor *done_{};
  switch_::Switch *opens_{};
};

// "8:05" or "1:12:30".
std::string clock_of(uint32_t seconds);

}  // namespace esphome::timers
