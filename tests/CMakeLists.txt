# GTest suites for the core library. NOTGROOT_TREES_DIR (or ~/osu-uwrt/bt4_ws/src/riptide_autonomy/trees) adds the
# round-trip gate over a real trees directory; without one that test is skipped.
add_executable(notgroot_core_tests
  core_xml.cpp
  core_workspace.cpp
  core_edit.cpp
  core_check.cpp
  core_run.cpp
  core_run_client.cpp)
target_link_libraries(notgroot_core_tests PRIVATE notgroot_core GTest::gtest_main PkgConfig::libzmq ZLIB::ZLIB)
target_compile_definitions(notgroot_core_tests PRIVATE NOTGROOT_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data"
  NOTGROOT_BUILTIN_MODELS="${NOTGROOT_CONTENT_DIR}/builtin_models.xml" NOTGROOT_CHECK="$<TARGET_FILE:notgroot-check>")
add_dependencies(notgroot_core_tests notgroot-check)
notgroot_project_options(notgroot_core_tests)
include(GoogleTest)
gtest_discover_tests(notgroot_core_tests)

# The editor's panels, and a run's, driven by injected key presses in a headless ImGui context.
add_executable(notgroot_ui_tests ui_keyboard.cpp ui_run.cpp)
target_link_libraries(notgroot_ui_tests PRIVATE notgroot_ui GTest::gtest_main)
target_compile_definitions(notgroot_ui_tests PRIVATE NOTGROOT_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data"
  NOTGROOT_BUILTIN_MODELS="${NOTGROOT_CONTENT_DIR}/builtin_models.xml"
  NOTGROOT_KEYS_DOC="${PROJECT_SOURCE_DIR}/docs/keys.md" NOTGROOT_THEMES="${NOTGROOT_CONTENT_DIR}/themes")
notgroot_project_options(notgroot_ui_tests)
gtest_discover_tests(notgroot_ui_tests)
