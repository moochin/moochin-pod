// Session log format: what's saved must parse back the same.
#include <string.h>
#include <unity.h>

#include "storage/ShotFormat.h"

void setUp() {}
void tearDown() {}

static void add(DrillResult& r, uint8_t pod, Shot::Kind k, uint16_t ms) {
  r.shots[r.shotCount++] = Shot{pod, k, ms};
}

static void test_format_hits_misses_mistakes() {
  DrillResult r;
  add(r, 0, Shot::Hit, 321);
  add(r, 2, Shot::Miss, 0);
  add(r, 1, Shot::Mistake, 0);
  add(r, 3, Shot::Hit, 287);
  char buf[64];
  shotfmt::format(r, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("1h321 3m 2d 4h287", buf);
}

static void test_round_trip() {
  DrillResult r;
  for (uint16_t i = 0; i < 100; i++) add(r, i % 6, i % 7 == 0 ? Shot::Miss : (i % 11 == 0 ? Shot::Mistake : Shot::Hit), 200 + i * 7);
  static char buf[shotfmt::kBufferSize];
  shotfmt::format(r, buf, sizeof(buf));
  uint16_t i = 0;
  shotfmt::parse(buf, [&](unsigned pod, char kind, unsigned ms) {
    TEST_ASSERT_TRUE(i < r.shotCount);
    TEST_ASSERT_EQUAL(r.shots[i].pod + 1, pod);
    TEST_ASSERT_EQUAL((char)r.shots[i].kind, kind);
    TEST_ASSERT_EQUAL(r.shots[i].kind == Shot::Hit ? r.shots[i].ms : 0, ms);
    i++;
  });
  TEST_ASSERT_EQUAL(r.shotCount, i);
}

static void test_full_session_fits_buffer() {
  DrillResult r;
  for (uint16_t i = 0; i < DrillResult::kMaxShots; i++) add(r, 5, Shot::Hit, 65535);
  static char buf[shotfmt::kBufferSize];
  const size_t n = shotfmt::format(r, buf, sizeof(buf));
  TEST_ASSERT_TRUE(n < sizeof(buf));
  uint16_t count = 0;
  shotfmt::parse(buf, [&](unsigned, char, unsigned ms) { count++; TEST_ASSERT_EQUAL(65535, ms); });
  TEST_ASSERT_EQUAL(DrillResult::kMaxShots, count);
}

static void test_parse_stops_on_garbage() {
  unsigned count = 0;
  shotfmt::parse("1h300 2x 3h400", [&](unsigned, char, unsigned) { count++; });
  TEST_ASSERT_EQUAL(1, count);
  shotfmt::parse("", [&](unsigned, char, unsigned) { count++; });
  shotfmt::parse(nullptr, [&](unsigned, char, unsigned) { count++; });
  TEST_ASSERT_EQUAL(1, count);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_format_hits_misses_mistakes);
  RUN_TEST(test_round_trip);
  RUN_TEST(test_full_session_fits_buffer);
  RUN_TEST(test_parse_stops_on_garbage);
  return UNITY_END();
}
