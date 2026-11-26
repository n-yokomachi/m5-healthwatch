#include "workout.h"

#include <cstring>

namespace {
void readItems(JsonVariantConst arr, std::vector<Item>& out) {
  for (JsonVariantConst e : arr.as<JsonArrayConst>()) {
    Item it;
    it.name = e["name"] | "";
    const char* kind = e["kind"] | "";
    if (strcmp(kind, "reps") == 0) {
      int n = e["reps"] | 0;
      if (n < 1) continue;
      it.timed = false;
      it.reps = static_cast<uint16_t>(n);
    } else if (strcmp(kind, "time") == 0) {
      it.timed = true;
      for (JsonVariantConst iv : e["intervals"].as<JsonArrayConst>()) {
        const char* level = iv["level"] | "";
        bool hard = strcmp(level, "hard") == 0;
        if (!hard && strcmp(level, "easy") != 0) continue;
        it.intervals.push_back({hard, iv["sec"] | 0u});
      }
      if (it.intervals.empty()) continue;
    } else {
      continue;
    }
    out.push_back(it);
  }
}
}  // namespace

bool parseMenu(JsonVariantConst v, Menu& out) {
  Menu m;
  m.area = v["area"] | "";
  int rounds = v["rounds"] | 0;
  if (rounds < 1) return false;
  m.rounds = static_cast<uint16_t>(rounds);

  readItems(v["items"], m.items);
  readItems(v["after"], m.after);
  if (m.items.empty() && m.after.empty()) return false;
  out = m;
  return true;
}

void Workout::begin(const Menu& menu) {
  menu_ = menu;
  step_ = Step();
  step_.after = menu_.items.empty();
  count_ = 0;
  interval_ = 0;
  itemStartMs_ = itemEndMs_ = firstStartMs_ = endMs_ = 0;
  started_ = running_ = waiting_ = done_ = false;
}

const Item& Workout::current() const {
  return step_.after ? menu_.after[step_.item] : menu_.items[step_.item];
}

bool Workout::following(Step& s) const {
  if (!s.after) {
    if (++s.item < menu_.items.size()) return true;
    if (s.round + 1 < menu_.rounds) {
      s.item = 0;
      ++s.round;
      return true;
    }
    s.after = true;
    s.item = 0;
    return !menu_.after.empty();
  }
  return ++s.item < menu_.after.size();
}

bool Workout::next(Step& out) const {
  out = step_;
  return following(out);
}

Event Workout::intervalEvent() const {
  return current().intervals[interval_].hard ? Event::IntervalHard : Event::IntervalEasy;
}

Event Workout::start(uint32_t nowMs) {
  if (done_ || running_ || (menu_.items.empty() && menu_.after.empty())) return Event::None;
  if (!started_) {
    started_ = true;
    firstStartMs_ = nowMs;
  } else if (waiting_) {
    following(step_);
  }
  waiting_ = false;
  running_ = true;
  count_ = 0;
  interval_ = 0;
  itemStartMs_ = nowMs;
  return current().timed ? intervalEvent() : Event::None;
}

uint32_t Workout::intervalEndMs(size_t idx) const {
  uint32_t sum = 0;
  for (size_t i = 0; i <= idx; ++i) sum += current().intervals[i].sec * 1000;
  return sum;
}

Event Workout::finishItem(uint32_t nowMs) {
  running_ = false;
  itemEndMs_ = nowMs;
  Step s;
  if (!next(s)) {
    done_ = true;
    endMs_ = nowMs;
    return Event::AllDone;
  }
  waiting_ = true;
  return Event::ItemDone;
}

Event Workout::tick(uint32_t nowMs) {
  if (!running_) return Event::None;
  uint32_t elapsed = nowMs - itemStartMs_;
  if (!current().timed) {
    if (elapsed < (count_ + 1u) * REP_INTERVAL_MS) return Event::None;
    ++count_;
    return count_ >= current().reps ? finishItem(nowMs) : Event::Count;
  }
  if (elapsed < intervalEndMs(interval_)) return Event::None;
  if (interval_ + 1 >= current().intervals.size()) return finishItem(nowMs);
  ++interval_;
  return intervalEvent();
}

uint32_t Workout::elapsedMs(uint32_t nowMs) const {
  if (!started_) return 0;
  return (running_ ? nowMs : itemEndMs_) - itemStartMs_;
}

uint32_t Workout::remainingMs(uint32_t nowMs) const {
  if (!running_ || !current().timed) return 0;
  uint32_t end = intervalEndMs(interval_);
  uint32_t el = nowMs - itemStartMs_;
  return el >= end ? 0 : end - el;
}
