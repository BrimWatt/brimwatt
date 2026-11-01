// Host-side tests for the goal tracker: when a departure goal's deadline counts as "just passed", once.
#include "../support/TestSupport.h"

#include "planner/GoalTracker.h"

using solar::GoalTracker;

namespace {

constexpr int kOffset = -7 * 3600;
constexpr int kSevenAm = 7 * 60;
constexpr int64_t kMidnight = 20000LL * 86400 + 7 * 3600;   // local midnight for kOffset on day 20000

int64_t at(int hour, int minute) { return kMidnight + hour * 3600LL + minute * 60LL; }

void test_the_first_look_only_sets_the_baseline_and_judges_nothing() {
    GoalTracker t;
    TEST_ASSERT_TRUE(t.due(at(23, 0), kOffset, kSevenAm, 20) == -1);
}

void test_a_deadline_is_judged_once_right_after_it_passes() {
    GoalTracker t;
    t.due(at(23, 0), kOffset, kSevenAm, 20);
    const int64_t deadline = at(24 + 7, 0);
    TEST_ASSERT_TRUE(t.due(deadline - 60, kOffset, kSevenAm, 20) == -1);
    TEST_ASSERT_TRUE(t.due(deadline + 30, kOffset, kSevenAm, 20) == deadline);
    TEST_ASSERT_TRUE(t.due(deadline + 90, kOffset, kSevenAm, 20) == -1);   // not again
}

void test_a_deadline_noticed_much_later_is_not_judged() {
    GoalTracker t;
    t.due(at(23, 0), kOffset, kSevenAm, 20);
    TEST_ASSERT_TRUE(t.due(at(24 + 12, 0), kOffset, kSevenAm, 20) == -1);   // five hours late: the board was off
    TEST_ASSERT_TRUE(t.due(at(48 + 7, 1), kOffset, kSevenAm, 20) == at(48 + 7, 0));
}

void test_changing_the_goal_or_its_time_starts_over_without_judging() {
    GoalTracker t;
    t.due(at(23, 0), kOffset, kSevenAm, 20);
    TEST_ASSERT_TRUE(t.due(at(24 + 7, 0) + 30, kOffset, 6 * 60, 20) == -1);
    TEST_ASSERT_TRUE(t.due(at(24 + 7, 0) + 60, kOffset, 6 * 60, 25) == -1);
}

void test_turning_the_goal_off_forgets_everything() {
    GoalTracker t;
    t.due(at(23, 0), kOffset, kSevenAm, 20);
    TEST_ASSERT_TRUE(t.due(at(24 + 7, 0) + 30, kOffset, kSevenAm, 0) == -1);
    TEST_ASSERT_TRUE(t.due(at(24 + 7, 0) + 60, kOffset, kSevenAm, 20) == -1);
}

}  // namespace

void setUp() {}
void tearDown() {}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_the_first_look_only_sets_the_baseline_and_judges_nothing);
    RUN_TEST(test_a_deadline_is_judged_once_right_after_it_passes);
    RUN_TEST(test_a_deadline_noticed_much_later_is_not_judged);
    RUN_TEST(test_changing_the_goal_or_its_time_starts_over_without_judging);
    RUN_TEST(test_turning_the_goal_off_forgets_everything);
    return UNITY_END();
}
