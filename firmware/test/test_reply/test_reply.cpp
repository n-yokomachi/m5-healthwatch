#include <unity.h>
#include <cstring>
#include "hw/reply.h"

static const char* SUGGEST = R"({"stage":"suggest","heard":"","reply":"今日は脚にしない？","audio":"AQACAA=="})";
static const char* SUGGEST_HEAD = R"({"stage":"suggest","heard":"","reply":"今日は脚にしない？"})";

static void feed(ReplySink& s, const char* body, size_t chunk = 0) {
  size_t n = strlen(body);
  if (chunk == 0) chunk = n;
  for (size_t i = 0; i < n; i += chunk) {
    size_t len = (n - i < chunk) ? n - i : chunk;
    TEST_ASSERT_EQUAL(len, s.feed(reinterpret_cast<const uint8_t*>(body + i), len));
  }
}

void test_splits_head_and_audio() {
  int16_t buf[4];
  ReplySink s(buf, 4);
  feed(s, SUGGEST);
  TEST_ASSERT_TRUE(s.finish());
  TEST_ASSERT_EQUAL_STRING(SUGGEST_HEAD, s.head().c_str());
  TEST_ASSERT_EQUAL(2, s.samples());
  TEST_ASSERT_EQUAL(1, buf[0]);
  TEST_ASSERT_EQUAL(2, buf[1]);
  TEST_ASSERT_FALSE(s.truncated());
}

void test_handles_one_byte_chunks() {
  int16_t buf[4];
  ReplySink s(buf, 4);
  feed(s, SUGGEST, 1);
  TEST_ASSERT_TRUE(s.finish());
  TEST_ASSERT_EQUAL_STRING(SUGGEST_HEAD, s.head().c_str());
  TEST_ASSERT_EQUAL(2, s.samples());
  TEST_ASSERT_EQUAL(1, buf[0]);
  TEST_ASSERT_EQUAL(2, buf[1]);
}

void test_menu_braces_and_audio_word_in_text() {
  // "AQID" = 01 02 03, "BAUG" = 04 05 06 -> samples 0x0201, 0x0403, 0x0605
  const char* body =
      R"({"stage":"menu","reply":"\"audio\" ok","menu":{"area":"x","items":[{"name":"a"}]},"audio":"AQIDBAUG"})";
  int16_t buf[8];
  ReplySink s(buf, 8);
  feed(s, body, 3);
  TEST_ASSERT_TRUE(s.finish());
  TEST_ASSERT_EQUAL_STRING(
      R"({"stage":"menu","reply":"\"audio\" ok","menu":{"area":"x","items":[{"name":"a"}]}})",
      s.head().c_str());
  TEST_ASSERT_EQUAL(3, s.samples());
  TEST_ASSERT_EQUAL(0x0201, buf[0]);
  TEST_ASSERT_EQUAL(0x0403, buf[1]);
  TEST_ASSERT_EQUAL(0x0605, buf[2]);
}

void test_single_padding() {
  // "AQACAAM=" = 01 00 02 00 03 -> 2 samples, the odd trailing byte is dropped
  int16_t buf[4];
  ReplySink s(buf, 4);
  feed(s, R"({"a":1,"audio":"AQACAAM="})");
  TEST_ASSERT_TRUE(s.finish());
  TEST_ASSERT_EQUAL(2, s.samples());
  TEST_ASSERT_EQUAL(2, buf[1]);
}

void test_truncates_audio_beyond_buffer() {
  int16_t buf[1];
  ReplySink s(buf, 1);
  feed(s, SUGGEST);
  TEST_ASSERT_TRUE(s.finish());
  TEST_ASSERT_EQUAL(1, s.samples());
  TEST_ASSERT_EQUAL(1, buf[0]);
  TEST_ASSERT_TRUE(s.truncated());
}

void test_missing_audio_fails() {
  int16_t buf[4];
  ReplySink s(buf, 4);
  feed(s, R"({"stage":"suggest","reply":"x"})");
  TEST_ASSERT_FALSE(s.finish());
}

void test_unterminated_audio_fails() {
  int16_t buf[4];
  ReplySink s(buf, 4);
  feed(s, R"({"a":1,"audio":"AQAC)");
  TEST_ASSERT_FALSE(s.finish());
}

void test_oversized_head_fails() {
  int16_t buf[4];
  ReplySink s(buf, 4);
  std::string big(REPLY_HEAD_MAX + 100, 'x');
  feed(s, big.c_str());
  feed(s, R"(,"audio":"AQAC"})");
  TEST_ASSERT_FALSE(s.finish());
  TEST_ASSERT_TRUE(s.head().size() <= REPLY_HEAD_MAX + 1);
}

void test_reset_reuses_sink() {
  int16_t buf[4];
  ReplySink s(buf, 4);
  feed(s, SUGGEST);
  TEST_ASSERT_TRUE(s.finish());
  s.reset();
  feed(s, R"({"stage":"done","audio":"AwAEAA=="})");
  TEST_ASSERT_TRUE(s.finish());
  TEST_ASSERT_EQUAL_STRING(R"({"stage":"done"})", s.head().c_str());
  TEST_ASSERT_EQUAL(2, s.samples());
  TEST_ASSERT_EQUAL(3, buf[0]);
  TEST_ASSERT_EQUAL(4, buf[1]);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_splits_head_and_audio);
  RUN_TEST(test_handles_one_byte_chunks);
  RUN_TEST(test_menu_braces_and_audio_word_in_text);
  RUN_TEST(test_single_padding);
  RUN_TEST(test_truncates_audio_beyond_buffer);
  RUN_TEST(test_missing_audio_fails);
  RUN_TEST(test_unterminated_audio_fails);
  RUN_TEST(test_oversized_head_fails);
  RUN_TEST(test_reset_reuses_sink);
  return UNITY_END();
}
