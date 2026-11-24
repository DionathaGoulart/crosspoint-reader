add_executable(ProfilePathTest
  ProfilePathTest.cpp
  ${REPO_ROOT}/lib/Serialization/ProfilePath.cpp
)

target_include_directories(ProfilePathTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/lib/Serialization
)

target_link_libraries(ProfilePathTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ProfilePathTest)
