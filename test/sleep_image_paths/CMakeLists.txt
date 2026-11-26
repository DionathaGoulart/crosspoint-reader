add_executable(SleepImagePathsTest
  SleepImagePathsTest.cpp
  ${REPO_ROOT}/src/util/SleepImagePaths.cpp
  ${REPO_ROOT}/lib/Serialization/ProfilePath.cpp
)

target_include_directories(SleepImagePathsTest PRIVATE
  ${REPO_ROOT}/test/profile_path/stubs
  ${REPO_ROOT}/lib/Serialization
  ${REPO_ROOT}/src
)

target_link_libraries(SleepImagePathsTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(SleepImagePathsTest)
