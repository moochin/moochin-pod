// Native unit tests for the BlazePod protocol layer.
//
// Expected auth payloads were produced by compiling and running the
// reference C code (sasodoma/blazepod-hacking main.c) with gcc, not by this
// port. Test cases mirror Snerls/blazepod tests/test_protocol.py.
#include <string.h>
#include <unity.h>

#include "pods/blazepod_protocol.h"

using namespace blazepod;

void setUp() {}
void tearDown() {}

static void assertAuth(const uint8_t* mfr, size_t len, const uint8_t expected[7]) {
  uint8_t out[7] = {0};
  TEST_ASSERT_TRUE(buildAuthPayload(mfr, len, out));
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, out, 7);
}

// Readme example: ManufacturerSpecificData D7-09-5A-01-DE-AD-BE-EF.
// Reference main.c prints: 73 65 61 3 48 60 B6
static void test_auth_readme_example_with_company_id() {
  const uint8_t mfr[] = {0xD7, 0x09, 0x5A, 0x01, 0xDE, 0xAD, 0xBE, 0xEF};
  const uint8_t expected[] = {0x73, 0x65, 0x61, 0x03, 0x48, 0x60, 0xB6};
  assertAuth(mfr, sizeof(mfr), expected);
}

// Same pod as seen through Bleak (company ID stripped) must give the same result.
static void test_auth_readme_example_without_company_id() {
  const uint8_t mfr[] = {0x5A, 0x01, 0xDE, 0xAD, 0xBE, 0xEF};
  const uint8_t expected[] = {0x73, 0x65, 0x61, 0x03, 0x48, 0x60, 0xB6};
  assertAuth(mfr, sizeof(mfr), expected);
}

static void test_auth_exactly_five_bytes() {
  const uint8_t mfr[] = {0x01, 0xDE, 0xAD, 0xBE, 0xEF};
  const uint8_t expected[] = {0x73, 0x65, 0x61, 0x03, 0x48, 0x60, 0xB6};
  assertAuth(mfr, sizeof(mfr), expected);
}

static void test_auth_leading_bytes_ignored() {
  const uint8_t a[] = {0x5A, 0x01, 0xDE, 0xAD, 0xBE, 0xEF};
  const uint8_t b[] = {0x00, 0xAA, 0xBB, 0x5A, 0x01, 0xDE, 0xAD, 0xBE, 0xEF};
  uint8_t outA[7], outB[7];
  TEST_ASSERT_TRUE(buildAuthPayload(a, sizeof(a), outA));
  TEST_ASSERT_TRUE(buildAuthPayload(b, sizeof(b), outB));
  TEST_ASSERT_EQUAL_HEX8_ARRAY(outA, outB, 7);
}

static void test_auth_offsets_match_reference() {
  struct Vec {
    uint8_t offset;
    uint8_t expected[7];
  };
  // body = 12 34 56 78; offsets as in Snerls test_auth_matches_c_port_across_offsets
  const Vec vecs[] = {
      {0, {0x73, 0x65, 0x61, 0x0A, 0x46, 0xC0, 0x75}},
      {1, {0x73, 0x65, 0x61, 0xF2, 0xBF, 0x24, 0x2E}},
      {25, {0x73, 0x65, 0x61, 0x70, 0x60, 0x47, 0xC0}},
      {49, {0x73, 0x65, 0x61, 0x43, 0x5F, 0x53, 0xA1}},
      {50, {0x73, 0x65, 0x61, 0x0A, 0x46, 0xC0, 0x75}},
      {99, {0x73, 0x65, 0x61, 0x43, 0x5F, 0x53, 0xA1}},
      {200, {0x73, 0x65, 0x61, 0x0A, 0x46, 0xC0, 0x75}},
      {255, {0x73, 0x65, 0x61, 0x74, 0xF3, 0x60, 0x0A}},
  };
  for (const Vec& v : vecs) {
    const uint8_t mfr[] = {v.offset, 0x12, 0x34, 0x56, 0x78};
    assertAuth(mfr, sizeof(mfr), v.expected);
  }
}

static void test_auth_edge_bodies() {
  const uint8_t zeros[] = {0x07, 0x00, 0x00, 0x00, 0x00};
  const uint8_t expZeros[] = {0x73, 0x65, 0x61, 0x08, 0x0D, 0xF0, 0x9A};
  assertAuth(zeros, sizeof(zeros), expZeros);

  const uint8_t ones[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  const uint8_t expOnes[] = {0x73, 0x65, 0x61, 0x08, 0x00, 0xE4, 0xFF};
  assertAuth(ones, sizeof(ones), expOnes);
}

static void test_auth_offset_changes_result() {
  const uint8_t a[] = {0x01, 0xDE, 0xAD, 0xBE, 0xEF};
  const uint8_t b[] = {0x02, 0xDE, 0xAD, 0xBE, 0xEF};
  uint8_t outA[7], outB[7];
  buildAuthPayload(a, 5, outA);
  buildAuthPayload(b, 5, outB);
  TEST_ASSERT_NOT_EQUAL(0, memcmp(outA, outB, 7));
}

static void test_auth_short_data_rejected() {
  const uint8_t mfr[] = {0x00, 0x01, 0x02, 0x03};
  uint8_t out[7];
  TEST_ASSERT_FALSE(buildAuthPayload(mfr, sizeof(mfr), out));
  TEST_ASSERT_FALSE(buildAuthPayload(nullptr, 8, out));
}

static void test_colour_is_g_b_r() {
  uint8_t out[4];
  TEST_ASSERT_EQUAL(3, encodeColour(255, 0, 0, false, out));
  const uint8_t red[] = {0x00, 0x00, 0xFF};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(red, out, 3);

  encodeColour(0, 255, 0, false, out);
  const uint8_t green[] = {0xFF, 0x00, 0x00};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(green, out, 3);

  encodeColour(0, 0, 255, false, out);
  const uint8_t blue[] = {0x00, 0xFF, 0x00};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(blue, out, 3);
}

static void test_colour_off_on_tap_flag() {
  uint8_t out[4];
  TEST_ASSERT_EQUAL(4, encodeColour(10, 20, 30, true, out));
  const uint8_t expected[] = {20, 30, 10, 0x01};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, out, 4);
}

static void test_colour_off() {
  uint8_t out[4];
  TEST_ASSERT_EQUAL(3, encodeColour(0, 0, 0, false, out));
  const uint8_t off[] = {0, 0, 0};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(off, out, 3);
}

// Readme: lit for 25 s (0x61A8 ms) then turned off: 25-A8-61-00-00-XX-XX-XX
static void test_tap_lit_turned_off() {
  const uint8_t p[] = {0x25, 0xA8, 0x61, 0x00, 0x00, 0x12, 0x34, 0x56};
  Tap t;
  TEST_ASSERT_EQUAL(TapParse::Ok, parseTap(p, sizeof(p), t));
  TEST_ASSERT_EQUAL(TapState::LitTurnedOff, t.state);
  TEST_ASSERT_EQUAL_UINT32(0x61A8, t.elapsedMs);
  TEST_ASSERT_TRUE(t.hitLitPod());
}

static void test_tap_lit_stayed_on() {
  const uint8_t p[] = {0x21, 0xE8, 0x03, 0x00, 0x00, 0, 0, 0};
  Tap t;
  TEST_ASSERT_EQUAL(TapParse::Ok, parseTap(p, sizeof(p), t));
  TEST_ASSERT_EQUAL(TapState::LitStayedOn, t.state);
  TEST_ASSERT_EQUAL_UINT32(1000, t.elapsedMs);
  TEST_ASSERT_TRUE(t.hitLitPod());
}

static void test_tap_while_off() {
  const uint8_t p[] = {0, 0, 0, 0, 0, 0, 0, 0};
  Tap t;
  TEST_ASSERT_EQUAL(TapParse::Ok, parseTap(p, sizeof(p), t));
  TEST_ASSERT_EQUAL(TapState::WhileOff, t.state);
  TEST_ASSERT_FALSE(t.hitLitPod());
}

static void test_tap_full_u32_little_endian() {
  const uint8_t p[] = {0x25, 0x78, 0x56, 0x34, 0x12, 0, 0, 0};
  Tap t;
  TEST_ASSERT_EQUAL(TapParse::Ok, parseTap(p, sizeof(p), t));
  TEST_ASSERT_EQUAL_HEX32(0x12345678, t.elapsedMs);
}

static void test_tap_unknown_state() {
  const uint8_t p[] = {0xFF, 0, 0, 0, 0, 0, 0, 0};
  Tap t;
  TEST_ASSERT_EQUAL(TapParse::UnknownState, parseTap(p, sizeof(p), t));
}

static void test_tap_too_short() {
  const uint8_t p[] = {0x00, 0x00};
  Tap t;
  TEST_ASSERT_EQUAL(TapParse::TooShort, parseTap(p, sizeof(p), t));
}

static void test_looks_like_pod() {
  TEST_ASSERT_TRUE(looksLikePod("BlazePod", 8));
  TEST_ASSERT_TRUE(looksLikePod("BlazePod-1234", 6));
  TEST_ASSERT_FALSE(looksLikePod("BlazePod", 4));   // mfr data too short to unlock
  TEST_ASSERT_FALSE(looksLikePod("Fitbit", 8));
  TEST_ASSERT_FALSE(looksLikePod("", 8));
  TEST_ASSERT_FALSE(looksLikePod(nullptr, 8));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_auth_readme_example_with_company_id);
  RUN_TEST(test_auth_readme_example_without_company_id);
  RUN_TEST(test_auth_exactly_five_bytes);
  RUN_TEST(test_auth_leading_bytes_ignored);
  RUN_TEST(test_auth_offsets_match_reference);
  RUN_TEST(test_auth_edge_bodies);
  RUN_TEST(test_auth_offset_changes_result);
  RUN_TEST(test_auth_short_data_rejected);
  RUN_TEST(test_colour_is_g_b_r);
  RUN_TEST(test_colour_off_on_tap_flag);
  RUN_TEST(test_colour_off);
  RUN_TEST(test_tap_lit_turned_off);
  RUN_TEST(test_tap_lit_stayed_on);
  RUN_TEST(test_tap_while_off);
  RUN_TEST(test_tap_full_u32_little_endian);
  RUN_TEST(test_tap_unknown_state);
  RUN_TEST(test_tap_too_short);
  RUN_TEST(test_looks_like_pod);
  return UNITY_END();
}
