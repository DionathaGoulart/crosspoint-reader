add_executable(OnboardingPlanTest OnboardingPlanTest.cpp)

target_include_directories(OnboardingPlanTest PRIVATE ../../src)

target_link_libraries(OnboardingPlanTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(OnboardingPlanTest)
