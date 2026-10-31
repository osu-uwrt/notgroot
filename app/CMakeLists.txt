# The editor application: window, menus, docking layout, preferences, around the ui panels.
add_executable(notgroot
  src/main.cpp
  src/app.cpp
  src/window.cpp
  src/file_picker.cpp)
target_link_libraries(notgroot PRIVATE notgroot_ui notgroot_imgui glfw OpenGL::GL GLEW::GLEW PNG::PNG
  Fontconfig::Fontconfig yaml-cpp)
target_compile_definitions(notgroot PRIVATE NOTGROOT_CONTENT_DIR="${NOTGROOT_CONTENT_DIR}"
  NOTGROOT_INSTALLED_CONTENT_DIR="${CMAKE_INSTALL_FULL_DATADIR}/notgroot" NOTGROOT_VERSION="${PROJECT_VERSION}")
notgroot_project_options(notgroot)
install(TARGETS notgroot RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
