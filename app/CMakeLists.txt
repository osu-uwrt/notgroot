# The editor application: window, menus, docking layout, preferences, around the ui panels.
add_executable(notgroot
  src/main.cpp
  src/app.cpp
  src/window.cpp
  src/file_picker.cpp)
# Touchpad pinches: the trackpad's magnify events on macOS (pinch_cocoa.mm), else XInput 2.4 (libXi 1.8 headers);
# without those pinch.cpp builds as a stub.
if(APPLE)
  enable_language(OBJCXX)
  target_sources(notgroot PRIVATE src/pinch_cocoa.mm src/dock_icon_cocoa.mm) # and the Dock icon
  target_link_libraries(notgroot PRIVATE "-framework AppKit")
else()
  target_sources(notgroot PRIVATE src/pinch.cpp)
  find_package(X11 COMPONENTS Xi)
endif()
include(CheckSymbolExists)
if(X11_Xi_FOUND)
  set(CMAKE_REQUIRED_INCLUDES ${X11_INCLUDE_DIR})
  check_symbol_exists(XI_GesturePinchBegin "X11/extensions/XInput2.h" NOTGROOT_HAVE_XI_GESTURES)
  unset(CMAKE_REQUIRED_INCLUDES)
endif()
target_link_libraries(notgroot PRIVATE notgroot_ui notgroot_imgui glfw OpenGL::GL GLEW::GLEW PNG::PNG
  Fontconfig::Fontconfig yaml-cpp)
if(NOTGROOT_HAVE_XI_GESTURES)
  target_link_libraries(notgroot PRIVATE X11::X11 X11::Xi)
  target_compile_definitions(notgroot PRIVATE NOTGROOT_PINCH=1)
endif()
target_compile_definitions(notgroot PRIVATE NOTGROOT_CONTENT_DIR="${NOTGROOT_CONTENT_DIR}"
  NOTGROOT_INSTALLED_CONTENT_DIR="${CMAKE_INSTALL_FULL_DATADIR}/notgroot" NOTGROOT_VERSION="${PROJECT_VERSION}"
  NOTGROOT_CHECK="$<TARGET_FILE:notgroot-check>" NOTGROOT_INSTALLED_CHECK="${CMAKE_INSTALL_FULL_BINDIR}/notgroot-check")
add_dependencies(notgroot notgroot-check)
notgroot_project_options(notgroot)
install(TARGETS notgroot RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
