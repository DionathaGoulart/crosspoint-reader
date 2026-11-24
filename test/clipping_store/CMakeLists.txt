foreach(target ClippingStoreTest ClippingStoreS3Test)
  # ProfilePath: ClippingStore resolves its directory through the active
  # profile, so the test links the real composition rather than stubbing it --
  # the paths are what the test is about.
  add_executable(${target} ClippingStoreTest.cpp ${REPO_ROOT}/src/ClippingStore.cpp
                 ${REPO_ROOT}/lib/Serialization/ProfilePath.cpp ${REPO_ROOT}/lib/Utf8/Utf8.cpp)
  target_link_libraries(${target} PRIVATE crosspoint_test_common GTest::gtest_main)
  target_include_directories(${target} PRIVATE stubs ../library_builder/stubs ${REPO_ROOT}/src ${REPO_ROOT}/lib/Serialization ${REPO_ROOT}/lib/Utf8)
  gtest_discover_tests(${target} TEST_PREFIX "${target}.")
endforeach()
target_compile_definitions(ClippingStoreS3Test PRIVATE CONFIG_IDF_TARGET_ESP32S3=1)
