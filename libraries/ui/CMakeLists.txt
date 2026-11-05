# Drawing: the theme (from Nereus), the session the panels share, the canvas and the panels. Needs an ImGui context;
# no window of its own, so a Nereus panel can host it later.
add_library(notgroot_ui
  src/theme.cpp
  src/style.cpp
  src/session.cpp
  src/canvas.cpp
  src/inspector.cpp
  src/panels.cpp
  src/popups.cpp
  src/tour.cpp)
target_include_directories(notgroot_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(notgroot_ui PUBLIC notgroot_core notgroot_imgui PRIVATE yaml-cpp)
notgroot_project_options(notgroot_ui)
# theme.cpp is Nereus's as-is (kept identical so the two can merge); its brace-initialized fallback theme leaves the
# font fields to their defaults.
set_source_files_properties(src/theme.cpp PROPERTIES COMPILE_OPTIONS -Wno-missing-field-initializers)
