add_executable(SkinMetricsTest SkinMetricsTest.cpp)

target_include_directories(SkinMetricsTest PRIVATE ../../src)

target_link_libraries(SkinMetricsTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(SkinMetricsTest)
