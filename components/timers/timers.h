#pragma once
#include <cstdint>
#include "esphome/core/defines.h"
#include "esphome/core/component.h"
#include "esphome/core/preferences.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/number/number.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/smart_display/plugin_api.h"
#include "esphome/components/smart_display/plugin_sound.h"   // empty on a screen without a speaker
#ifdef USE_VOICE_ASSISTANT
#include <functional>
#include <utility>
#include "esphome/core/automation.h"
#include "esphome/components/voice_assistant/voice_assistant.h"
#endif
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
  char entity[96] = {};  // the timer helper, for the sensor that names the timers that are done
  bool known = false, available = false;
  Run run = Run::IDLE;
  uint32_t duration_s = 0, remaining_s = 0, finishes_at = 0;
};

struct AssistDone {
  bool active = false;
  char id[48] = {};
  char name[64] = {};
  uint32_t total_s = 0;
  uint32_t done_chip_ms = 0;
};

struct OverviewItem {
  enum Source : uint8_t { ASSIST, SCREEN, HELPER } source = SCREEN;
  Run run = Run::IDLE;
  int16_t slot_index = -1;
  char label[64] = {};
  uint32_t remaining_s = 0;
  uint32_t total_s = 0;
};

class TimerDonePopup;

#ifdef USE_VOICE_ASSISTANT
template<typename... Ts> class AssistLambdaAction final : public Action<Ts...> {
 public:
  explicit AssistLambdaAction(std::function<void(const Ts &...)> fn) : fn_(std::move(fn)) {}
  void play(const Ts &...x) override { this->fn_(x...); }

 private:
  std::function<void(const Ts &...)> fn_;
};
#endif

class Timers : public Component, public tessera::Plugin {
 public:
  static constexpr int SLOTS = 16;
  static constexpr int MAX_ASSIST_DONE = 8;

  void setup() override;
  float get_setup_priority() const override { return setup_priority::DATA; }
  void on_interval(uint32_t now_ms) override;
  void before_update() override { silence(); }
  bool settings(tessera::SettingsPage &page) override;

  void set_done(binary_sensor::BinarySensor *s) { done_ = s; }
  void set_popup(switch_::Switch *s) { popup_switch_ = s; }
  void set_opens(switch_::Switch *s) { opens_ = s; }
  void set_done_list(text_sensor::TextSensor *s) { done_list_ = s; }
#ifdef USE_SPEAKER
  // The part parts/sound.yaml, on a screen with a speaker: a timer that is done rings.
  void set_speaker(speaker::Speaker *s) { sounds_.set_speaker(s); }
  void set_alarm(tessera::Sound s) { alarm_ = s; }
  void set_sound_on(switch_::Switch *s) { sound_on_ = s; }
  void set_alarm_volume(number::Number *n) { alarm_volume_ = n; }
  void loop() override { sounds_.loop(); }
#endif
#ifdef USE_VOICE_ASSISTANT
  void set_assistant(voice_assistant::VoiceAssistant *a) { assistant_ = a; }
  void attach_assist_started();
  void attach_assist_updated();
  void attach_assist_cancelled();
  void attach_assist_finished();
  void attach_assist_tick();
  void on_assist_changed();
  void on_assist_finished(const voice_assistant::Timer &timer);
#endif
  bool has_assistant() const {
#ifdef USE_VOICE_ASSISTANT
    return assistant_ != nullptr;
#else
    return false;
#endif
  }
  void ask_assist();

  // The screen's clock in milliseconds since 1970, 0 until Home Assistant set it.
  int64_t now_ms();
  // The slot of a tile (its index in the layout), made when it has none.
  Slot &slot(int tile, Kind kind);
  void set_label(const Slot &s, const char *label);
  // Read the timer helper `entity` from a tile's data into `h`, and see whether it rang since the slot last saw it.
  // The slot keeps a copy for the card the tile opens.
  void read(Slot &s, const char *entity, JsonObjectConst data, Helper &h);
  const Helper &helper_of(const Slot &s) { return helper(s); }
  // Every timer that is done stops ringing and goes back to idle, as Stop on each would: the button Silence timers,
  // for an automation that silenced its own alarm another way. Running timers run on.
  void silence();
  void silence_sound();
  void popup_off();
  void popup_closed(TimerDonePopup *who);
  bool any_done() const;
  // A key tapped on a face of the slot; `entity` is the timer helper ("" for a timer on the screen).
  void press(Slot &s, Key key, const char *entity, const Helper &h);
  void press_overview_slot(int slot_index, Key key);
  // What a face of the slot shows now.
  View view(Slot &s, const char *entity, const Helper &h);
  int collect_overview(OverviewItem *out, int max_items);
  int collect_done(OverviewItem *out, int max_items) const;

 protected:
  static constexpr uint32_t AUDIO_CLAIM_ID = 1;

  void changed();
  void notify_done(int16_t tile);
  void show_done_popup();
  void stop_alarm_audio();
  // The top bar's items: the timer that ends first (or one that is done), the stopwatch that runs longest.
  tessera::BarItem timer_item();
  tessera::BarItem stopwatch_item();
  std::string minutes_text(uint32_t seconds, bool up);
  void start_helper(const char *entity, uint32_t seconds);
  Helper &helper(const Slot &s) { return helpers_[&s - slots_]; }
  const char *label_of(int index) const;

  // SLOTS kept in flash, and one more for a card opened without a tile.
  Slot slots_[SLOTS + 1];
  Helper helpers_[SLOTS + 1];
  char labels_[SLOTS + 1][64] = {};
  AssistDone assist_done_[MAX_ASSIST_DONE];
  ESPPreferenceObject saved_;
  bool dirty_ = false, ringing_ = false;
  uint32_t saved_at_ = 0;
  int64_t base_ms_ = 0;
  uint32_t base_millis_ = 0;
  binary_sensor::BinarySensor *done_{};
  switch_::Switch *popup_switch_{};
  switch_::Switch *opens_{};
  TimerDonePopup *popup_{};
  // The timer helpers that are done, for an automation that has to tell several apart: "timer.eggs, timer.rice".
  text_sensor::TextSensor *done_list_{};
  tessera::Text done_names_;
#ifdef USE_SPEAKER
  tessera::Sound alarm_;
  switch_::Switch *sound_on_{};
  number::Number *alarm_volume_{};
  tessera::SoundPlayer sounds_;
  uint32_t rang_at_ = 0;
  uint32_t alarm_started_ms_ = 0;
  bool alarm_active_ = false;
  bool audio_claimed_ = false;
  bool volume_raised_ = false;
#endif
#ifdef USE_VOICE_ASSISTANT
  voice_assistant::VoiceAssistant *assistant_{};
#endif
};

// "8:05" or "1:12:30".
std::string clock_of(uint32_t seconds);

}  // namespace esphome::timers
