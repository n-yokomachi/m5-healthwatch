#include <unity.h>
#include <ArduinoJson.h>
#include "hw/workout.h"

static Item reps(const char* name, uint16_t n) {
  Item i;
  i.name = name;
  i.timed = false;
  i.reps = n;
  return i;
}

static Item timed(const char* name, std::vector<Interval> iv) {
  Item i;
  i.name = name;
  i.timed = true;
  i.reps = 0;
  i.intervals = iv;
  return i;
}

static Menu menuOf(std::vector<Item> items, uint16_t rounds, std::vector<Item> after = {}) {
  Menu m;
  m.area = "test";
  m.rounds = rounds;
  m.items = items;
  m.after = after;
  return m;
}

void setUp() {}
void tearDown() {}

void test_reps_count_every_interval() {
  Workout w; w.begin(menuOf({reps("スクワット", 2)}, 1));
  w.start(0);
  TEST_ASSERT_EQUAL(Event::None, w.tick(2999));
  TEST_ASSERT_EQUAL(Event::Count, w.tick(3000)); TEST_ASSERT_EQUAL(1, w.count());
  TEST_ASSERT_EQUAL(Event::AllDone, w.tick(6000)); TEST_ASSERT_EQUAL(2, w.count());
  TEST_ASSERT_EQUAL(6000, w.totalMs());
  TEST_ASSERT_TRUE(w.done());
}

void test_item_done_waits_for_start() {
  Workout w; w.begin(menuOf({reps("A", 1), reps("B", 1)}, 2));
  w.start(0); TEST_ASSERT_EQUAL(Event::ItemDone, w.tick(3000)); TEST_ASSERT_EQUAL(1, w.count());
  TEST_ASSERT_TRUE(w.waiting()); TEST_ASSERT_EQUAL(Event::None, w.tick(9000));
  w.start(10000); TEST_ASSERT_EQUAL(1, w.itemIndex());
  TEST_ASSERT_FALSE(w.waiting());
  TEST_ASSERT_EQUAL(0, w.count());
}

void test_round_advances_after_last_item() {
  Workout w; w.begin(menuOf({reps("A", 1), reps("B", 1)}, 2));
  w.start(0); w.tick(3000);
  w.start(4000); TEST_ASSERT_EQUAL(1, w.itemIndex()); TEST_ASSERT_EQUAL(0, w.round());
  TEST_ASSERT_EQUAL(Event::ItemDone, w.tick(7000));
  w.start(8000); TEST_ASSERT_EQUAL(0, w.itemIndex()); TEST_ASSERT_EQUAL(1, w.round());
  w.tick(11000);
  w.start(12000);
  TEST_ASSERT_EQUAL(Event::AllDone, w.tick(15000));
  TEST_ASSERT_TRUE(w.done());
  TEST_ASSERT_EQUAL(15000, w.totalMs());
}

void test_intervals_switch_and_finish() {
  Workout w; w.begin(menuOf({timed("バイク", {{true, 120}, {false, 60}})}, 1));
  TEST_ASSERT_EQUAL(Event::IntervalHard, w.start(0));
  TEST_ASSERT_EQUAL(0, w.intervalIndex());
  TEST_ASSERT_EQUAL(120000, w.remainingMs(0));
  TEST_ASSERT_EQUAL(Event::None, w.tick(119999));
  TEST_ASSERT_EQUAL(Event::IntervalEasy, w.tick(120000));
  TEST_ASSERT_EQUAL(1, w.intervalIndex());
  TEST_ASSERT_EQUAL(30000, w.remainingMs(150000));
  TEST_ASSERT_EQUAL(150000, w.elapsedMs(150000));
  TEST_ASSERT_EQUAL(Event::AllDone, w.tick(180000));
  TEST_ASSERT_EQUAL(180000, w.totalMs());
}

void test_start_on_easy_interval_reports_easy() {
  Workout w; w.begin(menuOf({timed("バイク", {{false, 30}})}, 1));
  TEST_ASSERT_EQUAL(Event::IntervalEasy, w.start(0));
}

void test_total_includes_waiting() {
  Workout w; w.begin(menuOf({reps("A", 1), reps("B", 1)}, 1));
  w.start(0); w.tick(3000);
  w.start(10000);
  TEST_ASSERT_EQUAL(Event::AllDone, w.tick(13000));
  TEST_ASSERT_EQUAL(13000, w.totalMs());
}

void test_parse_menu_reads_agent_json() {
  JsonDocument d;
  deserializeJson(d, "{\"area\":\"上半身と体幹\",\"rounds\":3,\"items\":["
    "{\"name\":\"腕立て伏せ\",\"kind\":\"reps\",\"reps\":10},"
    "{\"name\":\"エアロバイク\",\"kind\":\"time\",\"intervals\":["
    "{\"level\":\"hard\",\"sec\":120},{\"level\":\"easy\",\"sec\":60}]}]}");
  Menu m;
  TEST_ASSERT_TRUE(parseMenu(d.as<JsonVariantConst>(), m));
  TEST_ASSERT_EQUAL_STRING("上半身と体幹", m.area.c_str());
  TEST_ASSERT_EQUAL(3, m.rounds);
  TEST_ASSERT_EQUAL(2, m.items.size());
  TEST_ASSERT_FALSE(m.items[0].timed);
  TEST_ASSERT_EQUAL(10, m.items[0].reps);
  TEST_ASSERT_TRUE(m.items[1].timed);
  TEST_ASSERT_EQUAL(2, m.items[1].intervals.size());
  TEST_ASSERT_TRUE(m.items[1].intervals[0].hard);
  TEST_ASSERT_EQUAL(120, m.items[1].intervals[0].sec);
  TEST_ASSERT_FALSE(m.items[1].intervals[1].hard);
}

void test_parse_menu_drops_unknown_kind_and_empty_intervals() {
  JsonDocument d;
  deserializeJson(d, "{\"area\":\"x\",\"rounds\":1,\"items\":["
    "{\"name\":\"プランク\",\"kind\":\"hold\",\"sec\":30},"
    "{\"name\":\"バイク\",\"kind\":\"time\",\"intervals\":[]},"
    "{\"name\":\"スクワット\",\"kind\":\"reps\",\"reps\":5}]}");
  Menu m;
  TEST_ASSERT_TRUE(parseMenu(d.as<JsonVariantConst>(), m));
  TEST_ASSERT_EQUAL(1, m.items.size());
  TEST_ASSERT_EQUAL_STRING("スクワット", m.items[0].name.c_str());
}

void test_parse_menu_drops_unknown_level_and_item_left_empty() {
  JsonDocument d;
  deserializeJson(d, "{\"area\":\"x\",\"rounds\":1,\"items\":["
    "{\"name\":\"A\",\"kind\":\"time\",\"intervals\":[{\"level\":\"mid\",\"sec\":10}]},"
    "{\"name\":\"B\",\"kind\":\"time\",\"intervals\":[{\"level\":\"mid\",\"sec\":10},{\"level\":\"hard\",\"sec\":20}]}]}");
  Menu m;
  TEST_ASSERT_TRUE(parseMenu(d.as<JsonVariantConst>(), m));
  TEST_ASSERT_EQUAL(1, m.items.size());
  TEST_ASSERT_EQUAL_STRING("B", m.items[0].name.c_str());
  TEST_ASSERT_EQUAL(1, m.items[0].intervals.size());
}

void test_parse_menu_rejects_no_items() {
  JsonDocument d;
  deserializeJson(d, "{\"area\":\"x\",\"rounds\":1,\"items\":[]}");
  Menu m;
  TEST_ASSERT_FALSE(parseMenu(d.as<JsonVariantConst>(), m));
}

void test_parse_menu_rejects_zero_rounds() {
  JsonDocument d;
  deserializeJson(d, "{\"area\":\"x\",\"rounds\":0,\"items\":[{\"name\":\"A\",\"kind\":\"reps\",\"reps\":5}]}");
  Menu m;
  TEST_ASSERT_FALSE(parseMenu(d.as<JsonVariantConst>(), m));
}

void test_after_runs_once_after_rounds() {
  Workout w; w.begin(menuOf({reps("A", 1)}, 2, {reps("X", 1), reps("Y", 1)}));
  w.start(0); TEST_ASSERT_FALSE(w.inAfter());
  TEST_ASSERT_EQUAL(Event::ItemDone, w.tick(3000));
  w.start(4000); TEST_ASSERT_EQUAL(1, w.round()); TEST_ASSERT_FALSE(w.inAfter());
  Step s; TEST_ASSERT_TRUE(w.next(s)); TEST_ASSERT_TRUE(s.after); TEST_ASSERT_EQUAL(0, s.item);
  TEST_ASSERT_EQUAL(Event::ItemDone, w.tick(7000));
  TEST_ASSERT_TRUE(w.waiting());
  w.start(8000);
  TEST_ASSERT_TRUE(w.inAfter()); TEST_ASSERT_EQUAL(0, w.itemIndex());
  TEST_ASSERT_EQUAL_STRING("X", w.current().name.c_str());
  TEST_ASSERT_EQUAL(Event::ItemDone, w.tick(11000));
  w.start(12000);
  TEST_ASSERT_EQUAL_STRING("Y", w.current().name.c_str());
  TEST_ASSERT_FALSE(w.next(s));
  TEST_ASSERT_EQUAL(Event::AllDone, w.tick(15000));
  TEST_ASSERT_TRUE(w.done());
  TEST_ASSERT_EQUAL(15000, w.totalMs());
}

void test_after_only_menu() {
  Workout w; w.begin(menuOf({}, 1, {timed("バイク", {{true, 120}, {false, 60}})}));
  TEST_ASSERT_EQUAL(Event::IntervalHard, w.start(0));
  TEST_ASSERT_TRUE(w.inAfter());
  TEST_ASSERT_EQUAL_STRING("バイク", w.current().name.c_str());
  TEST_ASSERT_EQUAL(Event::IntervalEasy, w.tick(120000));
  TEST_ASSERT_EQUAL(1, w.intervalIndex());
  TEST_ASSERT_EQUAL(30000, w.remainingMs(150000));
  TEST_ASSERT_EQUAL(Event::AllDone, w.tick(180000));
  TEST_ASSERT_EQUAL(180000, w.totalMs());
}

void test_after_timed_item_intervals() {
  Workout w; w.begin(menuOf({reps("A", 1)}, 1, {timed("バイク", {{false, 30}, {true, 60}})}));
  w.start(0); TEST_ASSERT_EQUAL(Event::ItemDone, w.tick(3000));
  TEST_ASSERT_EQUAL(Event::IntervalEasy, w.start(5000));
  TEST_ASSERT_TRUE(w.inAfter());
  TEST_ASSERT_EQUAL(Event::None, w.tick(34999));
  TEST_ASSERT_EQUAL(Event::IntervalHard, w.tick(35000));
  TEST_ASSERT_EQUAL(60000, w.remainingMs(35000));
  TEST_ASSERT_EQUAL(Event::AllDone, w.tick(95000));
  TEST_ASSERT_EQUAL(95000, w.totalMs());
}

void test_no_after_ends_at_last_round() {
  Workout w; w.begin(menuOf({reps("A", 1)}, 1));
  Step s; TEST_ASSERT_FALSE(w.next(s));
  w.start(0); TEST_ASSERT_EQUAL(Event::AllDone, w.tick(3000));
}

void test_parse_menu_reads_after() {
  JsonDocument d;
  deserializeJson(d, "{\"area\":\"脚\",\"rounds\":3,\"items\":["
    "{\"name\":\"スクワット\",\"kind\":\"reps\",\"reps\":15}],"
    "\"after\":[{\"name\":\"プランク\",\"kind\":\"hold\"},"
    "{\"name\":\"エアロバイク\",\"kind\":\"time\",\"intervals\":["
    "{\"level\":\"hard\",\"sec\":120},{\"level\":\"easy\",\"sec\":60}]}]}");
  Menu m;
  TEST_ASSERT_TRUE(parseMenu(d.as<JsonVariantConst>(), m));
  TEST_ASSERT_EQUAL(1, m.items.size());
  TEST_ASSERT_EQUAL(1, m.after.size());
  TEST_ASSERT_EQUAL_STRING("エアロバイク", m.after[0].name.c_str());
  TEST_ASSERT_TRUE(m.after[0].timed);
  TEST_ASSERT_EQUAL(2, m.after[0].intervals.size());
}

void test_parse_menu_accepts_after_only() {
  JsonDocument d;
  deserializeJson(d, "{\"area\":\"有酸素\",\"rounds\":1,\"items\":[],"
    "\"after\":[{\"name\":\"エアロバイク\",\"kind\":\"time\",\"intervals\":[{\"level\":\"easy\",\"sec\":600}]}]}");
  Menu m;
  TEST_ASSERT_TRUE(parseMenu(d.as<JsonVariantConst>(), m));
  TEST_ASSERT_EQUAL(0, m.items.size());
  TEST_ASSERT_EQUAL(1, m.after.size());
}

void test_parse_menu_rejects_when_after_left_empty() {
  JsonDocument d;
  deserializeJson(d, "{\"area\":\"x\",\"rounds\":1,\"items\":[],"
    "\"after\":[{\"name\":\"バイク\",\"kind\":\"time\",\"intervals\":[]}]}");
  Menu m;
  TEST_ASSERT_FALSE(parseMenu(d.as<JsonVariantConst>(), m));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_reps_count_every_interval);
  RUN_TEST(test_item_done_waits_for_start);
  RUN_TEST(test_round_advances_after_last_item);
  RUN_TEST(test_intervals_switch_and_finish);
  RUN_TEST(test_start_on_easy_interval_reports_easy);
  RUN_TEST(test_total_includes_waiting);
  RUN_TEST(test_parse_menu_reads_agent_json);
  RUN_TEST(test_parse_menu_drops_unknown_kind_and_empty_intervals);
  RUN_TEST(test_parse_menu_drops_unknown_level_and_item_left_empty);
  RUN_TEST(test_parse_menu_rejects_no_items);
  RUN_TEST(test_parse_menu_rejects_zero_rounds);
  RUN_TEST(test_after_runs_once_after_rounds);
  RUN_TEST(test_after_only_menu);
  RUN_TEST(test_after_timed_item_intervals);
  RUN_TEST(test_no_after_ends_at_last_round);
  RUN_TEST(test_parse_menu_reads_after);
  RUN_TEST(test_parse_menu_accepts_after_only);
  RUN_TEST(test_parse_menu_rejects_when_after_left_empty);
  return UNITY_END();
}
