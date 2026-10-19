#pragma once
#include "esphome/components/smart_display/plugin_api.h"
#include <string>

namespace esphome::timers {

enum class Kind : uint8_t { NONE, TIMER, STOPWATCH };
enum class Run : uint8_t { IDLE, RUNNING, PAUSED, DONE };

// What a finger can tap on a face. OPEN is the rest of a tile: it opens the card with every key.
enum class Key : uint8_t { NONE, ADD_10S, ADD_1M, ADD_10M, ADD_1H, PRIMARY, SECONDARY, OPEN };

// What a face shows now, made by the plugin from a slot (and a timer helper's state) every second.
struct View {
  Kind kind = Kind::TIMER;
  Run run = Run::IDLE;
  std::string time, sub;
  int progress = 0;                          // the ring, 0 to 1000
  tessera::Tone tone = tessera::Tone::NORMAL;
  const char *primary = "", *secondary = "";  // the words on the two keys under the add keys
  uint32_t primary_icon = 0, secondary_icon = 0;   // the same on a round key (0: its words)
  uint8_t laps = 0;                           // a stopwatch's laps, the newest first
  std::string lap[3];
};

// The ring, the time and the keys of a timer or a stopwatch, laid out for the room it gets: a tile of any size or a
// card. The shape follows the room in millimetres of glass, so every key stays at least 8 mm across:
//   SIDE    the ring beside the keys (a 2x2 tile, a card lying down),
//   STACK   the ring above a row of four add keys and the two keys (a card standing up),
//   COMPACT the ring above +1m and +10m and the two keys (a 1x2 tile),
//   STRIP   the ring, the time and two round keys in a row (a 2x1 tile),
//   PLAIN   the ring and the time (a 1x1 tile): a tap opens the card.
enum class Shape : uint8_t { PLAIN, STRIP, COMPACT, STACK, SIDE };

class Face {
 public:
  // Make every part in `parent`, once (create() of a tile, open() of a card). `name`: the words over the time where a
  // shape has room for them ("" for none); `card`: the room is a card's, on the page's ground.
  void create(lv_obj_t *parent, int width, int height, Kind kind, const tessera::Plugin *plugin, const char *name,
              bool card);
  void paint(const View &view);
  void on_theme();
  // The key under a point of the room (tessera::tap_point()), OPEN for the rest.
  Key hit(tessera::Point point) const;
  Shape shape() const { return shape_; }

 private:
  struct Button {
    lv_obj_t *box = nullptr, *label = nullptr;
    Key key = Key::NONE;
    int x = 0, y = 0, w = 0, h = 0;
    bool round = false, main = false;
    uint32_t icon = 0;
  };
  static constexpr int BUTTONS = 6;

  Button *add_button(Key key, int x, int y, int w, int h, bool round, bool main);
  void paint_button(Button &b, const char *words, uint32_t icon);
  void place_text(lv_obj_t *label, const std::string &text, int x, int y, int w, int max_h, bool largest);

  const tessera::Plugin *plugin_ = nullptr;
  lv_obj_t *parent_ = nullptr;
  Kind kind_ = Kind::TIMER;
  Shape shape_ = Shape::PLAIN;
  bool card_ = false;
  lv_obj_t *arc_ = nullptr, *time_ = nullptr, *sub_ = nullptr, *name_ = nullptr;
  lv_obj_t *lap_[3] = {};
  Button buttons_[BUTTONS];
  int count_ = 0;
  // Where the time and the line under it go: a box of `text_w_` across at (text_x_, text_y_), `text_h_` high, centred
  // in it; inside the ring or beside it.
  int text_x_ = 0, text_y_ = 0, text_w_ = 0, text_h_ = 0;
  int lap_x_ = 0, lap_y_ = 0, lap_w_ = 0;
  tessera::Tone tone_ = tessera::Tone::NORMAL;
  int progress_ = -1;
  bool has_name_ = false;
};

}  // namespace esphome::timers
