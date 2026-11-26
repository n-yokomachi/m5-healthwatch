#pragma once
// Workout progress logic for HealthWatch. No Arduino dependency so it can be tested on the host.
#include <ArduinoJson.h>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

constexpr uint32_t REP_INTERVAL_MS = 3000;

struct Interval {
  bool hard;
  uint32_t sec;
};

struct Item {
  std::string name;
  bool timed = false;
  uint16_t reps = 0;
  std::vector<Interval> intervals;
};

struct Menu {
  std::string area;
  uint16_t rounds = 0;
  std::vector<Item> items;  // done `rounds` times
  std::vector<Item> after;  // done once each after the rounds (may be empty)
};

// where an item sits in the workout: round `round` of `items`, or `after`
struct Step {
  bool after = false;
  uint16_t round = 0;  // stays at the last round in `after`
  size_t item = 0;     // index into `items`, or into `after` when `after` is set
};

enum class Event { None, Count, IntervalHard, IntervalEasy, ItemDone, AllDone };

// Reads the menu JSON sent by the agent ("items" and the optional "after"). Items with an unknown
// kind, a rep item without reps, and time items left without a valid interval are dropped.
// Returns false when neither list has an item left or rounds < 1.
bool parseMenu(JsonVariantConst v, Menu& out);

class Workout {
 public:
  void begin(const Menu& menu);
  Event start(uint32_t nowMs);  // starts the current item (or the next one after ItemDone)
  Event tick(uint32_t nowMs);

  uint16_t round() const { return step_.round; }  // 0-based; the last round while inAfter()
  size_t itemIndex() const { return step_.item; }  // into menu.items, or menu.after while inAfter()
  bool inAfter() const { return step_.after; }
  const Item& current() const;
  bool next(Step& out) const;  // the item after the current one; false when it is the last
  uint16_t count() const { return count_; }
  uint32_t elapsedMs(uint32_t nowMs) const;
  size_t intervalIndex() const { return interval_; }
  uint32_t remainingMs(uint32_t nowMs) const;  // time left in the current interval (0 for rep items)
  bool waiting() const { return waiting_; }
  bool done() const { return done_; }
  uint32_t totalMs() const { return done_ ? endMs_ - firstStartMs_ : 0; }

 private:
  bool following(Step& s) const;
  uint32_t intervalEndMs(size_t idx) const;  // offset from item start
  Event finishItem(uint32_t nowMs);
  Event intervalEvent() const;

  Menu menu_;
  Step step_;
  uint16_t count_ = 0;
  size_t interval_ = 0;
  uint32_t itemStartMs_ = 0;
  uint32_t itemEndMs_ = 0;
  uint32_t firstStartMs_ = 0;
  uint32_t endMs_ = 0;
  bool started_ = false;
  bool running_ = false;
  bool waiting_ = false;
  bool done_ = false;
};
