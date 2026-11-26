// Drill engine tests, run natively against MockPodDriver.
#include <unity.h>

#include "engine/DrillEngine.h"
#include "engine/Drills.h"
#include "pods/MockPodDriver.h"

void setUp() {}
void tearDown() {}

static PodTap tapOn(uint8_t slot, uint32_t podMs, TapKind kind = TapKind::LitTurnedOff) {
  PodTap t;
  t.slot = slot;
  t.kind = kind;
  t.podMs = podMs;
  t.hubMillis = 0;
  return t;
}

// Runs the countdown and returns the time at which the drill is running.
static uint32_t startRunning(DrillEngine& e, const DrillConfig& cfg, uint32_t t0 = 1000) {
  TEST_ASSERT_TRUE(e.start(cfg, t0));
  for (uint32_t t = t0; t <= t0 + 3000; t += 10) e.tick(t);
  TEST_ASSERT_EQUAL(DrillPhase::Running, e.phase());
  return t0 + 3000;
}

static uint8_t countEvents(DrillEngine& e, DrillEventType type) {
  uint8_t n = 0;
  DrillEvent ev;
  while (e.pollEvent(ev)) n += ev.type == type;
  return n;
}

static void test_start_needs_a_connected_pod() {
  MockPodDriver pods;
  pods.begin(4);  // nothing connected yet
  DrillEngine e(pods);
  TEST_ASSERT_FALSE(e.start(drills::reaction(30000), 0));
  TEST_ASSERT_EQUAL(DrillPhase::Idle, e.phase());
}

static void test_countdown_beats_then_go_and_first_light() {
  MockPodDriver pods;
  pods.setAutoTap(false);
  pods.begin(4);
  pods.connectAllNow();
  DrillEngine e(pods);
  TEST_ASSERT_TRUE(e.start(drills::reaction(30000), 0));
  e.tick(999);
  TEST_ASSERT_EQUAL(DrillPhase::Countdown, e.phase());
  TEST_ASSERT_EQUAL(3, e.countdown());
  e.tick(1000);
  TEST_ASSERT_EQUAL(2, e.countdown());
  e.tick(2000);
  TEST_ASSERT_EQUAL(1, e.countdown());
  e.tick(3000);
  TEST_ASSERT_EQUAL(DrillPhase::Running, e.phase());
  TEST_ASSERT_TRUE(e.litPod() >= 0);
  TEST_ASSERT_TRUE(pods.lit(e.litPod()));

  DrillEvent ev;
  uint8_t beats = 0, go = 0, lit = 0;
  while (e.pollEvent(ev)) {
    beats += ev.type == DrillEventType::CountdownBeat;
    go += ev.type == DrillEventType::Go;
    lit += ev.type == DrillEventType::Lit;
  }
  TEST_ASSERT_EQUAL(3, beats);
  TEST_ASSERT_EQUAL(1, go);
  TEST_ASSERT_EQUAL(1, lit);
}

static void test_hit_uses_pod_measured_time() {
  MockPodDriver pods;
  pods.setAutoTap(false);
  pods.begin(4);
  pods.connectAllNow();
  DrillEngine e(pods);
  uint32_t t = startRunning(e, drills::reaction(30000));
  const int8_t p = e.litPod();
  // The hub sees the tap 900 ms later (slow BLE), but the pod says 412 ms.
  e.onTap(tapOn(p, 412), t + 900);
  TEST_ASSERT_EQUAL(1, e.result().hits);
  TEST_ASSERT_EQUAL_UINT32(412, e.result().averageMs());
  TEST_ASSERT_EQUAL_UINT32(412, e.result().bestMs);
  TEST_ASSERT_EQUAL(1, e.result().podHits[p]);
  TEST_ASSERT_EQUAL(-1, e.litPod());
}

static void test_timeout_is_a_miss_and_lights_off() {
  MockPodDriver pods;
  pods.setAutoTap(false);
  pods.begin(4);
  pods.connectAllNow();
  DrillEngine e(pods);
  uint32_t t = startRunning(e, drills::reaction(30000));
  const int8_t p = e.litPod();
  e.tick(t + 2999);
  TEST_ASSERT_EQUAL(0, e.result().misses);
  e.tick(t + 3000);
  TEST_ASSERT_EQUAL(1, e.result().misses);
  TEST_ASSERT_EQUAL(1, e.result().podMisses[p]);
  TEST_ASSERT_FALSE(pods.lit(p));
}

static void test_never_same_pod_twice_in_a_row() {
  MockPodDriver pods;
  pods.setAutoTap(false);
  pods.begin(4);
  pods.connectAllNow();
  DrillEngine e(pods, 99);
  uint32_t t = startRunning(e, drills::sprint(600000));
  int8_t last = -1;
  for (int i = 0; i < 300; i++) {
    const int8_t p = e.litPod();
    TEST_ASSERT_TRUE(p >= 0);
    TEST_ASSERT_NOT_EQUAL(last, p);
    last = p;
    e.onTap(tapOn(p, 300), t);
    t += 10;
    e.tick(t);  // sprint: next pod lights straight away
  }
  TEST_ASSERT_EQUAL(300, e.result().hits);
}

static void test_reaction_delay_between_lights_in_range() {
  MockPodDriver pods;
  pods.setAutoTap(false);
  pods.begin(4);
  pods.connectAllNow();
  DrillEngine e(pods, 7);
  uint32_t t = startRunning(e, drills::reaction(600000));
  for (int i = 0; i < 50; i++) {
    e.onTap(tapOn(e.litPod(), 300), t);
    uint32_t hitAt = t;
    while (e.litPod() < 0) {
      t += 10;
      e.tick(t);
    }
    const uint32_t gap = t - hitAt;
    TEST_ASSERT_TRUE_MESSAGE(gap >= 500 && gap <= 2010, "delay outside 0.5-2 s");
  }
}

static void test_sequence_lights_in_order_and_skips_missing_pods() {
  MockPodDriver pods;
  pods.setAutoTap(false);
  pods.begin(4);
  pods.connectAllNow();
  pods.setConnected(2, false);  // pod 3 is off
  DrillEngine e(pods);
  uint32_t t = startRunning(e, drills::sequence(600000));
  const int8_t expected[] = {0, 1, 3, 0, 1, 3};
  for (int8_t want : expected) {
    TEST_ASSERT_EQUAL(want, e.litPod());
    e.onTap(tapOn(want, 250), t);
    t += 10;
    e.tick(t);
  }
}

static void test_disconnect_while_lit_is_not_a_miss() {
  MockPodDriver pods;
  pods.setAutoTap(false);
  pods.begin(4);
  pods.connectAllNow();
  DrillEngine e(pods);
  uint32_t t = startRunning(e, drills::reaction(30000));
  const int8_t p = e.litPod();
  pods.setConnected(p, false);
  e.tick(t + 100);
  TEST_ASSERT_EQUAL(-1, e.litPod());
  e.tick(t + 5000);
  TEST_ASSERT_EQUAL(0, e.result().misses);
  TEST_ASSERT_TRUE(e.litPod() >= 0);
  TEST_ASSERT_NOT_EQUAL(p, e.litPod());
}

static void test_ends_on_time_with_lights_off() {
  MockPodDriver pods;
  pods.setAutoTap(false);
  pods.begin(4);
  pods.connectAllNow();
  DrillEngine e(pods);
  uint32_t t = startRunning(e, drills::reaction(30000));
  TEST_ASSERT_EQUAL_UINT32(30000, e.remainingMs(t));
  e.tick(t + 29990);
  TEST_ASSERT_EQUAL(DrillPhase::Running, e.phase());
  e.tick(t + 30000);
  TEST_ASSERT_EQUAL(DrillPhase::Finished, e.phase());
  TEST_ASSERT_FALSE(e.result().aborted);
  TEST_ASSERT_EQUAL_UINT32(30000, e.result().durationMs);
  for (uint8_t i = 0; i < 4; i++) TEST_ASSERT_FALSE(pods.lit(i));
  TEST_ASSERT_EQUAL(1, countEvents(e, DrillEventType::Finished));
}

static void test_stop_aborts() {
  MockPodDriver pods;
  pods.setAutoTap(false);
  pods.begin(4);
  pods.connectAllNow();
  DrillEngine e(pods);
  uint32_t t = startRunning(e, drills::reaction(30000));
  e.stop(t + 5000);
  TEST_ASSERT_EQUAL(DrillPhase::Finished, e.phase());
  TEST_ASSERT_TRUE(e.result().aborted);
  for (uint8_t i = 0; i < 4; i++) TEST_ASSERT_FALSE(pods.lit(i));
}

static void test_wrong_tap_counted_not_scored() {
  MockPodDriver pods;
  pods.setAutoTap(false);
  pods.begin(4);
  pods.connectAllNow();
  DrillEngine e(pods);
  uint32_t t = startRunning(e, drills::reaction(30000));
  const uint8_t other = (uint8_t)((e.litPod() + 1) % 4);
  e.onTap(tapOn(other, 0, TapKind::WhileOff), t + 100);
  TEST_ASSERT_EQUAL(1, e.result().wrongTaps);
  TEST_ASSERT_EQUAL(0, e.result().hits);
  TEST_ASSERT_TRUE(e.litPod() >= 0);  // still waiting for the right pod
}

static void test_full_drill_with_simulated_player() {
  MockPodDriver pods(4242);
  pods.begin(4);
  DrillEngine e(pods, 4242);
  uint32_t t = 0;
  for (; t < 5000; t += 10) pods.tick(t);  // pods trickle in
  TEST_ASSERT_EQUAL(4, pods.connectedCount());
  TEST_ASSERT_TRUE(e.start(drills::reaction(30000), t));
  for (; e.phase() != DrillPhase::Finished && t < 60000; t += 5) {
    pods.tick(t);
    PodTap tap;
    while (pods.pollTap(tap)) e.onTap(tap, t);
    e.tick(t);
  }
  const DrillResult& r = e.result();
  TEST_ASSERT_EQUAL(DrillPhase::Finished, e.phase());
  TEST_ASSERT_TRUE(r.hits >= 10);
  TEST_ASSERT_TRUE(r.averageMs() >= 250 && r.averageMs() <= 900);
  TEST_ASSERT_TRUE(r.bestMs >= 250 && r.worstMs <= 900);
  uint16_t podTotal = 0;
  for (uint8_t i = 0; i < 4; i++) podTotal += r.podHits[i];
  TEST_ASSERT_EQUAL(r.hits, podTotal);
}

static bool sameRgb(Rgb a, Rgb b) { return a.r == b.r && a.g == b.g && a.b == b.b; }

static uint8_t litCount(MockPodDriver& pods) {
  uint8_t n = 0;
  for (uint8_t i = 0; i < pods.maxPods(); i++) n += pods.lit(i);
  return n;
}

static void test_colour_call_lights_green_target_and_red_decoy() {
  MockPodDriver pods;
  pods.setAutoTap(false);
  pods.begin(4);
  pods.connectAllNow();
  DrillEngine e(pods, 3);
  uint32_t t = startRunning(e, drills::colourCall(600000));
  for (int round = 0; round < 50; round++) {
    const int8_t target = e.litPod();
    TEST_ASSERT_TRUE(target >= 0);
    TEST_ASSERT_EQUAL(2, litCount(pods));
    TEST_ASSERT_TRUE(sameRgb(pods.colour(target), Rgb{0, 255, 0}));
    uint8_t decoys = 0;
    for (uint8_t i = 0; i < 4; i++)
      if (pods.lit(i) && i != target) {
        TEST_ASSERT_TRUE(sameRgb(pods.colour(i), Rgb{255, 0, 0}));
        decoys++;
      }
    TEST_ASSERT_EQUAL(1, decoys);
    e.onTap(tapOn(target, 400), t);
    TEST_ASSERT_EQUAL(0, litCount(pods));  // hit: the decoy goes off too
    while (e.litPod() < 0) e.tick(t += 10);
  }
  TEST_ASSERT_EQUAL(50, e.result().hits);
  TEST_ASSERT_EQUAL(0, e.result().mistakes);
}

static void test_colour_call_decoy_hit_is_a_mistake_and_ends_round() {
  MockPodDriver pods;
  pods.setAutoTap(false);
  pods.begin(4);
  pods.connectAllNow();
  DrillEngine e(pods, 5);
  uint32_t t = startRunning(e, drills::colourCall(30000));
  int8_t decoy = -1;
  for (uint8_t i = 0; i < 4; i++)
    if (e.litMask() & (1u << i) && i != e.litPod()) decoy = i;
  TEST_ASSERT_TRUE(decoy >= 0);
  e.onTap(tapOn(decoy, 350), t + 350);
  TEST_ASSERT_EQUAL(1, e.result().mistakes);
  TEST_ASSERT_EQUAL(1, e.result().podMistakes[decoy]);
  TEST_ASSERT_EQUAL(0, e.result().hits);
  TEST_ASSERT_EQUAL(-1, e.litPod());
  TEST_ASSERT_EQUAL(0, litCount(pods));
  TEST_ASSERT_EQUAL(1, countEvents(e, DrillEventType::Mistake));
}

static void test_colour_call_single_pod_has_no_decoy() {
  MockPodDriver pods;
  pods.setAutoTap(false);
  pods.begin(1);
  pods.connectAllNow();
  DrillEngine e(pods);
  startRunning(e, drills::colourCall(30000));
  TEST_ASSERT_EQUAL(0, e.litPod());
  TEST_ASSERT_EQUAL(1, litCount(pods));
}

static void test_left_right_cue_colour_matches_target_side() {
  MockPodDriver pods;
  pods.setAutoTap(false);
  pods.begin(4);
  pods.connectAllNow();
  const uint8_t left = 0b0101;  // pods 1 and 3 on the left
  DrillEngine e(pods, 11);
  uint32_t t = startRunning(e, drills::leftRight(600000, left));
  uint16_t lefts = 0, rights = 0;
  for (int round = 0; round < 200; round++) {
    const int8_t target = e.litPod();
    TEST_ASSERT_TRUE(target >= 0);
    TEST_ASSERT_EQUAL(2, litCount(pods));
    const bool targetLeft = left & (1u << target);
    // Both lit pods show the same cue; blue means the target is on the left.
    for (uint8_t i = 0; i < 4; i++)
      if (pods.lit(i)) TEST_ASSERT_TRUE(sameRgb(pods.colour(i), targetLeft ? kCueLeft : kCueRight));
    // One lit pod on each side.
    uint8_t litLeft = 0, litRight = 0;
    for (uint8_t i = 0; i < 4; i++)
      if (pods.lit(i)) (left & (1u << i)) ? litLeft++ : litRight++;
    TEST_ASSERT_EQUAL(1, litLeft);
    TEST_ASSERT_EQUAL(1, litRight);
    targetLeft ? lefts++ : rights++;
    e.onTap(tapOn(target, 500), t);
    while (e.litPod() < 0) e.tick(t += 10);
  }
  TEST_ASSERT_TRUE(lefts > 60 && rights > 60);  // both sides get called
}

static void test_left_right_one_side_missing_uses_other_side() {
  MockPodDriver pods;
  pods.setAutoTap(false);
  pods.begin(4);
  pods.connectAllNow();
  pods.setConnected(0, false);
  pods.setConnected(1, false);  // both left pods gone
  DrillEngine e(pods);
  startRunning(e, drills::leftRight(30000, 0b0011));
  const int8_t target = e.litPod();
  TEST_ASSERT_TRUE(target == 2 || target == 3);
  TEST_ASSERT_TRUE(sameRgb(pods.colour(target), kCueRight));
  TEST_ASSERT_EQUAL(1, litCount(pods));
}

static void test_shot_log_records_hits_misses_mistakes_in_order() {
  MockPodDriver pods;
  pods.setAutoTap(false);
  pods.begin(4);
  pods.connectAllNow();
  DrillEngine e(pods, 9);
  uint32_t t = startRunning(e, drills::colourCall(600000));
  // hit
  const int8_t first = e.litPod();
  e.onTap(tapOn(first, 321), t);
  while (e.litPod() < 0) e.tick(t += 10);
  // miss (timeout)
  const int8_t second = e.litPod();
  e.tick(t += 3000);
  while (e.litPod() < 0) e.tick(t += 10);
  // mistake
  int8_t decoy = -1;
  for (uint8_t i = 0; i < 4; i++)
    if (e.litMask() & (1u << i) && i != e.litPod()) decoy = i;
  e.onTap(tapOn(decoy, 200), t);

  const DrillResult& r = e.result();
  TEST_ASSERT_EQUAL(3, r.shotCount);
  TEST_ASSERT_EQUAL(Shot::Hit, r.shots[0].kind);
  TEST_ASSERT_EQUAL(first, r.shots[0].pod);
  TEST_ASSERT_EQUAL(321, r.shots[0].ms);
  TEST_ASSERT_EQUAL(Shot::Miss, r.shots[1].kind);
  TEST_ASSERT_EQUAL(second, r.shots[1].pod);
  TEST_ASSERT_EQUAL(Shot::Mistake, r.shots[2].kind);
  TEST_ASSERT_EQUAL(decoy, r.shots[2].pod);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_start_needs_a_connected_pod);
  RUN_TEST(test_countdown_beats_then_go_and_first_light);
  RUN_TEST(test_hit_uses_pod_measured_time);
  RUN_TEST(test_timeout_is_a_miss_and_lights_off);
  RUN_TEST(test_never_same_pod_twice_in_a_row);
  RUN_TEST(test_reaction_delay_between_lights_in_range);
  RUN_TEST(test_sequence_lights_in_order_and_skips_missing_pods);
  RUN_TEST(test_disconnect_while_lit_is_not_a_miss);
  RUN_TEST(test_ends_on_time_with_lights_off);
  RUN_TEST(test_stop_aborts);
  RUN_TEST(test_wrong_tap_counted_not_scored);
  RUN_TEST(test_full_drill_with_simulated_player);
  RUN_TEST(test_colour_call_lights_green_target_and_red_decoy);
  RUN_TEST(test_colour_call_decoy_hit_is_a_mistake_and_ends_round);
  RUN_TEST(test_colour_call_single_pod_has_no_decoy);
  RUN_TEST(test_left_right_cue_colour_matches_target_side);
  RUN_TEST(test_left_right_one_side_missing_uses_other_side);
  RUN_TEST(test_shot_log_records_hits_misses_mistakes_in_order);
  return UNITY_END();
}
