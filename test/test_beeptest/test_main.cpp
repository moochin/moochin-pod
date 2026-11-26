// Beep test: the schedule, and the engine running it against MockPodDriver.
#include <unity.h>

#include "engine/BeepTest.h"
#include "engine/DrillEngine.h"
#include "engine/Drills.h"
#include "pods/MockPodDriver.h"

void setUp() {}
void tearDown() {}

// ---------------------------------------------------------------- schedule

static void test_speeds_and_shuttle_times() {
  TEST_ASSERT_EQUAL(80, beeptest::speedTenths(1));    // 8.0 km/h
  TEST_ASSERT_EQUAL(90, beeptest::speedTenths(2));    // 9.0 km/h
  TEST_ASSERT_EQUAL(95, beeptest::speedTenths(3));    // then +0.5 per level
  TEST_ASSERT_EQUAL(180, beeptest::speedTenths(20));  // 18.0 km/h
  TEST_ASSERT_EQUAL(185, beeptest::speedTenths(21));  // 18.5 km/h
  TEST_ASSERT_EQUAL_UINT32(9000, beeptest::shuttleMs(1));  // 20 m at 8 km/h = 9.0 s
  TEST_ASSERT_EQUAL_UINT32(8000, beeptest::shuttleMs(2));
  TEST_ASSERT_EQUAL_UINT32(7579, beeptest::shuttleMs(3));  // 7.5789 s
  TEST_ASSERT_EQUAL_UINT32(6000, beeptest::shuttleMs(8));  // 12 km/h
  TEST_ASSERT_EQUAL_UINT32(4000, beeptest::shuttleMs(20));
  TEST_ASSERT_EQUAL_UINT32(3892, beeptest::shuttleMs(21));  // 3.8919 s
}

static void test_levels_last_about_a_minute() {
  for (uint8_t l = 1; l <= beeptest::kLevels; l++) {
    const uint32_t ms = beeptest::shuttleMs(l) * beeptest::shuttlesInLevel(l);
    TEST_ASSERT_TRUE_MESSAGE(ms >= 56000 && ms <= 68000, "level length out of range");
  }
}

static void test_score_from_shuttle_count() {
  beeptest::Score s = beeptest::scoreFor(0);
  TEST_ASSERT_EQUAL(1, s.level);
  TEST_ASSERT_EQUAL(0, s.shuttle);
  s = beeptest::scoreFor(7);  // all of level 1
  TEST_ASSERT_EQUAL(1, s.level);
  TEST_ASSERT_EQUAL(7, s.shuttle);
  s = beeptest::scoreFor(8);  // first of level 2
  TEST_ASSERT_EQUAL(2, s.level);
  TEST_ASSERT_EQUAL(1, s.shuttle);
  // Level 8.4: 7+8+8+9+9+10+10 = 61 before level 8, plus 4.
  s = beeptest::scoreFor(65);
  TEST_ASSERT_EQUAL(8, s.level);
  TEST_ASSERT_EQUAL(4, s.shuttle);
  TEST_ASSERT_EQUAL(61, beeptest::shuttlesBefore(8));
  TEST_ASSERT_EQUAL(247, beeptest::totalShuttles());
}

// ---------------------------------------------------------------- engine

static PodTap tapOn(uint8_t slot, uint32_t podMs) {
  PodTap t;
  t.slot = slot;
  t.kind = TapKind::LitStayedOn;
  t.podMs = podMs;
  t.hubMillis = 0;
  return t;
}

static uint32_t startBeep(DrillEngine& e, bool taps, uint32_t t0 = 1000) {
  TEST_ASSERT_TRUE(e.start(drills::beepTest(taps), t0));
  for (uint32_t t = t0; t <= t0 + 3000; t += 10) e.tick(t);
  TEST_ASSERT_EQUAL(DrillPhase::Running, e.phase());
  return t0 + 3000;
}

static void test_needs_two_pods() {
  MockPodDriver pods;
  pods.setAutoTap(false);
  pods.begin(4);
  pods.setConnected(2, true);
  DrillEngine e(pods);
  TEST_ASSERT_FALSE(e.start(drills::beepTest(true), 0));
  pods.setConnected(3, true);
  TEST_ASSERT_TRUE(e.start(drills::beepTest(true), 0));
}

static void test_pods_alternate_on_the_beep_schedule() {
  MockPodDriver pods;
  pods.setAutoTap(false);
  pods.begin(4);
  pods.connectAllNow();
  DrillEngine e(pods);
  const uint32_t go = startBeep(e, false);
  // Runs from pod 1 to pod 2 first; only the destination is lit.
  TEST_ASSERT_EQUAL(1, e.litPod());
  TEST_ASSERT_TRUE(pods.lit(1));
  TEST_ASSERT_FALSE(pods.lit(0));
  TEST_ASSERT_EQUAL(1, e.beepLevel());
  TEST_ASSERT_EQUAL(1, e.beepShuttle());
  e.tick(go + 8999);
  TEST_ASSERT_EQUAL(1, e.litPod());
  e.tick(go + 9000);  // beep: switch ends
  TEST_ASSERT_EQUAL(0, e.litPod());
  TEST_ASSERT_TRUE(pods.lit(0));
  TEST_ASSERT_FALSE(pods.lit(1));
  TEST_ASSERT_EQUAL(2, e.beepShuttle());
  TEST_ASSERT_EQUAL(1, e.result().hits);
}

static void test_level_up_after_seven_shuttles_without_drift() {
  MockPodDriver pods;
  pods.setAutoTap(false);
  pods.begin(2);
  pods.connectAllNow();
  DrillEngine e(pods);
  const uint32_t go = startBeep(e, false);
  DrillEvent ev;
  while (e.pollEvent(ev)) {
  }
  // Tick coarsely (every 37 ms): beeps must still land on the schedule.
  uint32_t t = go;
  while (e.beepLevel() == 1) e.tick(t += 37);
  TEST_ASSERT_EQUAL(2, e.beepLevel());
  TEST_ASSERT_EQUAL(1, e.beepShuttle());
  TEST_ASSERT_EQUAL(7, e.result().hits);
  // Level 2's first shuttle started at exactly 7 x 9 s after GO.
  TEST_ASSERT_EQUAL_UINT32(go + 63000 + 8000 - t, e.shuttleRemainingMs(t));
  bool levelUp = false;
  while (e.pollEvent(ev)) levelUp |= ev.type == DrillEventType::LevelUp && ev.n == 2;
  TEST_ASSERT_TRUE(levelUp);
}

static void test_tap_mode_two_late_in_a_row_ends_test() {
  MockPodDriver pods;
  pods.setAutoTap(false);
  pods.begin(2);
  pods.connectAllNow();
  DrillEngine e(pods);
  uint32_t t = startBeep(e, true);
  // Shuttle 1: tapped in time -> pod turns green, counts.
  e.onTap(tapOn(1, 7200), t + 7200);
  TEST_ASSERT_TRUE(e.shuttleReached());
  TEST_ASSERT_EQUAL(0, pods.colour(1).r);
  TEST_ASSERT_EQUAL(255, pods.colour(1).g);
  e.tick(t += 9000);
  TEST_ASSERT_EQUAL(1, e.result().hits);
  // Shuttle 2: late (no tap) -> warning, test goes on.
  e.tick(t += 9000);
  TEST_ASSERT_EQUAL(1, e.result().misses);
  TEST_ASSERT_EQUAL(DrillPhase::Running, e.phase());
  // Shuttle 3: made it -> warnings reset.
  e.onTap(tapOn(1, 8000), t + 8000);
  e.tick(t += 9000);
  TEST_ASSERT_EQUAL(2, e.result().hits);
  // Shuttles 4 and 5: late twice in a row -> out.
  e.tick(t += 9000);
  TEST_ASSERT_EQUAL(DrillPhase::Running, e.phase());
  e.tick(t += 9000);
  TEST_ASSERT_EQUAL(DrillPhase::Finished, e.phase());
  TEST_ASSERT_FALSE(e.result().aborted);
  TEST_ASSERT_EQUAL(2, e.result().hits);  // score: 2 shuttles
  TEST_ASSERT_FALSE(pods.lit(0));
  TEST_ASSERT_FALSE(pods.lit(1));
}

static void test_tap_on_wrong_end_does_not_count() {
  MockPodDriver pods;
  pods.setAutoTap(false);
  pods.begin(2);
  pods.connectAllNow();
  DrillEngine e(pods);
  uint32_t t = startBeep(e, true);
  e.onTap(tapOn(0, 100), t + 100);  // the pod he just left
  TEST_ASSERT_FALSE(e.shuttleReached());
}

static void test_group_mode_stop_is_a_finished_test() {
  MockPodDriver pods;
  pods.setAutoTap(false);
  pods.begin(2);
  pods.connectAllNow();
  DrillEngine e(pods);
  uint32_t t = startBeep(e, false);
  for (int i = 0; i < 10; i++) e.tick(t += 9000);  // no taps at all
  TEST_ASSERT_EQUAL(DrillPhase::Running, e.phase());
  e.stop(t + 100);
  TEST_ASSERT_EQUAL(DrillPhase::Finished, e.phase());
  TEST_ASSERT_FALSE(e.result().aborted);  // counts for PBs/history
  TEST_ASSERT_TRUE(e.result().hits >= 9);
  TEST_ASSERT_EQUAL(0, e.result().misses);
}

static void test_stop_in_countdown_is_aborted() {
  MockPodDriver pods;
  pods.setAutoTap(false);
  pods.begin(2);
  pods.connectAllNow();
  DrillEngine e(pods);
  TEST_ASSERT_TRUE(e.start(drills::beepTest(false), 0));
  e.stop(500);
  TEST_ASSERT_TRUE(e.result().aborted);
}

static void test_completes_all_21_levels() {
  MockPodDriver pods;
  pods.setAutoTap(false);
  pods.begin(2);
  pods.connectAllNow();
  DrillEngine e(pods);
  uint32_t t = startBeep(e, false);
  while (e.phase() == DrillPhase::Running && t < 30u * 60 * 1000) e.tick(t += 50);
  TEST_ASSERT_EQUAL(DrillPhase::Finished, e.phase());
  TEST_ASSERT_EQUAL(beeptest::totalShuttles(), e.result().hits);
}

static void test_dropped_destination_pod_is_credited_in_tap_mode() {
  MockPodDriver pods;
  pods.setAutoTap(false);
  pods.begin(2);
  pods.connectAllNow();
  DrillEngine e(pods);
  uint32_t t = startBeep(e, true);
  e.onTap(tapOn(1, 5000), t + 5000);
  pods.setConnected(0, false);  // the next destination vanishes
  e.tick(t += 9000);
  e.tick(t += 9000);  // that shuttle can't be tapped: no warning
  TEST_ASSERT_EQUAL(0, e.result().misses);
  TEST_ASSERT_EQUAL(2, e.result().hits);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_speeds_and_shuttle_times);
  RUN_TEST(test_levels_last_about_a_minute);
  RUN_TEST(test_score_from_shuttle_count);
  RUN_TEST(test_needs_two_pods);
  RUN_TEST(test_pods_alternate_on_the_beep_schedule);
  RUN_TEST(test_level_up_after_seven_shuttles_without_drift);
  RUN_TEST(test_tap_mode_two_late_in_a_row_ends_test);
  RUN_TEST(test_tap_on_wrong_end_does_not_count);
  RUN_TEST(test_group_mode_stop_is_a_finished_test);
  RUN_TEST(test_stop_in_countdown_is_aborted);
  RUN_TEST(test_completes_all_21_levels);
  RUN_TEST(test_dropped_destination_pod_is_credited_in_tap_mode);
  return UNITY_END();
}
