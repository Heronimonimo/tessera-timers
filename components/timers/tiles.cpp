#include "timers.h"
#include <cstdio>

namespace esphome::timers {

static const char *card_of(Kind kind) { return kind == Kind::TIMER ? "timer" : "stopwatch"; }

// A tile: a face in the tile's room. What runs lives in the plugin's slot of this tile, so a tile made again when its
// page comes back (a board without PSRAM) shows the same timer.
class TimerTile : public tessera::Tile {
 public:
  TimerTile(Timers *plugin, Kind kind) : plugin_(plugin), kind_(kind) {}

  void create(const tessera::TileContext &c) override {
    tile_ = c.tile;
    snprintf(entity_, sizeof entity_, "%s", kind_ == Kind::TIMER && c.entity ? c.entity : "");
    face_.create(c.parent, c.width, c.height, kind_, plugin_, c.name, false);
  }

  void on_state(JsonObjectConst data) override {
    if (entity_[0]) plugin_->read(plugin_->slot(tile_, kind_), data, helper_);
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
    if (entity_[0]) plugin_->read(plugin_->slot(tile_, kind_), data, helper_);
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

tessera::Tile *make_tile(Timers *plugin, Kind kind) { return new TimerTile(plugin, kind); }
tessera::Card *make_card(Timers *plugin, Kind kind) { return new TimerCard(plugin, kind); }

}  // namespace esphome::timers
