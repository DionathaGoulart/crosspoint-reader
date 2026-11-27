#include <gtest/gtest.h>

#include "activities/settings/OnboardingPlan.h"

using onboarding::Mode;
using onboarding::Step;

TEST(OnboardingPlan, FullWalksEveryPageInOrder) {
  Step steps[onboarding::MAX_STEPS];
  ASSERT_EQ(onboarding::stepsFor(Mode::Full, steps), 6u);
  EXPECT_EQ(steps[0], Step::Welcome);
  EXPECT_EQ(steps[1], Step::Theme);
  EXPECT_EQ(steps[2], Step::Reading);
  EXPECT_EQ(steps[3], Step::Controls);
  EXPECT_EQ(steps[4], Step::Sleep);
  EXPECT_EQ(steps[5], Step::Goals);
}

TEST(OnboardingPlan, NewProfileShowsOnlyWhatBelongsToThatProfile) {
  Step steps[onboarding::MAX_STEPS];
  ASSERT_EQ(onboarding::stepsFor(Mode::ProfileOnly, steps), 2u);
  // Controls, Sleep, Theme and Welcome (language) are device-wide settings.
  EXPECT_EQ(steps[0], Step::Reading);
  EXPECT_EQ(steps[1], Step::Goals);
}
