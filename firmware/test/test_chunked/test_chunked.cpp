#include <unity.h>
#include <cstring>
#include <string>
#include "hw/chunked.h"

// the body is {"stage":"done","audio":"AQACAA=="}: 35 bytes = 0x23
static const char* HEAD = R"({"stage":"done"})";

static void feed(ChunkedDecoder& d, const char* wire, size_t chunk = 0) {
  size_t n = strlen(wire);
  if (chunk == 0) chunk = n;
  for (size_t i = 0; i < n; i += chunk) {
    size_t len = (n - i < chunk) ? n - i : chunk;
    d.feed(reinterpret_cast<const uint8_t*>(wire + i), len);
  }
}

static void assertBody(const ReplySink& s) {
  TEST_ASSERT_TRUE(s.finish());
  TEST_ASSERT_EQUAL_STRING(HEAD, s.head().c_str());
  TEST_ASSERT_EQUAL(2, s.samples());
}

// the body split over three chunks: 10 + 20 + 5 bytes
static const char* WIRE =
    "a\r\n{\"stage\":\"\r\n"
    "14\r\ndone\",\"audio\":\"AQACA\r\n"
    "5\r\nA==\"}\r\n"
    "0\r\n\r\n";

void test_decodes_chunks() {
  int16_t buf[4];
  ReplySink s(buf, 4);
  ChunkedDecoder d(s);
  feed(d, WIRE);
  TEST_ASSERT_TRUE(d.done());
  TEST_ASSERT_FALSE(d.error());
  TEST_ASSERT_EQUAL(35, d.bodyBytes());
  assertBody(s);
}

void test_one_byte_at_a_time() {
  int16_t buf[4];
  ReplySink s(buf, 4);
  ChunkedDecoder d(s);
  size_t n = strlen(WIRE);
  for (size_t i = 0; i < n; i++) {
    TEST_ASSERT_FALSE(d.done());
    feed(d, std::string(1, WIRE[i]).c_str());
  }
  TEST_ASSERT_TRUE(d.done());
  TEST_ASSERT_FALSE(d.error());
  assertBody(s);
}

void test_not_done_before_last_chunk() {
  int16_t buf[4];
  ReplySink s(buf, 4);
  ChunkedDecoder d(s);
  feed(d, "23\r\n{\"stage\":\"done\",\"audio\":\"AQACAA==\"}\r\n0\r\n");
  TEST_ASSERT_FALSE(d.done());
  TEST_ASSERT_FALSE(d.error());
  TEST_ASSERT_TRUE(s.finish());
}

void test_hex_case_and_extension() {
  const char* body = "{\"stage\":\"done\",\"audio\":\"AQACAA==\"}";
  for (const char* size : {"23", "023", "0023;name=value"}) {
    int16_t buf[4];
    ReplySink s(buf, 4);
    ChunkedDecoder d(s);
    std::string wire = std::string(size) + "\r\n" + body + "\r\n0;last\r\n\r\n";
    feed(d, wire.c_str());
    TEST_ASSERT_TRUE_MESSAGE(d.done(), size);
    assertBody(s);
  }
  // 0x1A and 0x1a are the same size
  for (const char* size : {"1A", "1a"}) {
    int16_t buf[4];
    ReplySink s(buf, 4);
    ChunkedDecoder d(s);
    std::string data(26, ' ');
    std::string wire = std::string(size) + "\r\n" + data + "\r\n0\r\n\r\n";
    feed(d, wire.c_str());
    TEST_ASSERT_TRUE_MESSAGE(d.done(), size);
    TEST_ASSERT_EQUAL(26, d.bodyBytes());
  }
}

void test_trailer_after_last_chunk() {
  int16_t buf[4];
  ReplySink s(buf, 4);
  ChunkedDecoder d(s);
  feed(d, "23\r\n{\"stage\":\"done\",\"audio\":\"AQACAA==\"}\r\n0\r\nX-Trace: abc\r\nX-More: 1\r\n", 3);
  TEST_ASSERT_FALSE(d.done());
  feed(d, "\r\n");
  TEST_ASSERT_TRUE(d.done());
  TEST_ASSERT_FALSE(d.error());
  assertBody(s);
}

void test_malformed() {
  const char* bad[] = {
      "zz\r\n",                       // not hex
      "\r\n",                         // no size
      "24\n",                         // LF without CR
      "4\r\nabcdX\r\n",               // data longer than the size
      "4\r\nabcd\rX",                 // CR not followed by LF after the data
      "123456789\r\n",                // size too large
      "0\r\n\rX",                     // broken end of the trailer
  };
  for (const char* wire : bad) {
    int16_t buf[4];
    ReplySink s(buf, 4);
    ChunkedDecoder d(s);
    feed(d, wire);
    TEST_ASSERT_TRUE_MESSAGE(d.error(), wire);
    TEST_ASSERT_FALSE_MESSAGE(d.done(), wire);
  }
}

void test_ignores_bytes_after_done() {
  int16_t buf[4];
  ReplySink s(buf, 4);
  ChunkedDecoder d(s);
  feed(d, "23\r\n{\"stage\":\"done\",\"audio\":\"AQACAA==\"}\r\n0\r\n\r\ngarbage");
  TEST_ASSERT_TRUE(d.done());
  TEST_ASSERT_FALSE(d.error());
  TEST_ASSERT_EQUAL(35, d.bodyBytes());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_decodes_chunks);
  RUN_TEST(test_one_byte_at_a_time);
  RUN_TEST(test_not_done_before_last_chunk);
  RUN_TEST(test_hex_case_and_extension);
  RUN_TEST(test_trailer_after_last_chunk);
  RUN_TEST(test_malformed);
  RUN_TEST(test_ignores_bytes_after_done);
  return UNITY_END();
}
