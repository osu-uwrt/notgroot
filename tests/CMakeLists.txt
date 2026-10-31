# GTest suites for the core library. NOTGROOT_TREES_DIR (or ~/osu-uwrt/bt4_ws/src/riptide_autonomy/trees) adds the
# round-trip gate over a real trees directory; without one that test is skipped.
add_executable(notgroot_core_tests
  core_xml.cpp
  core_workspace.cpp)
target_link_libraries(notgroot_core_tests PRIVATE notgroot_core GTest::gtest_main)
target_compile_definitions(notgroot_core_tests PRIVATE NOTGROOT_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data"
  NOTGROOT_BUILTIN_MODELS="${NOTGROOT_CONTENT_DIR}/builtin_models.xml")
notgroot_project_options(notgroot_core_tests)
include(GoogleTest)
gtest_discover_tests(notgroot_core_tests)
