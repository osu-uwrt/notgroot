# notgroot: a keyboard-first editor for BehaviorTree.CPP 4 XML. The core library (documents, models, layout,
# variables, search, validation) has no UI; the ui library draws it with Dear ImGui; app/ is the window around them.
cmake_minimum_required(VERSION 3.22)
project(notgroot VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CTest)

option(NOTGROOT_ENABLE_SANITIZERS "Enable address and undefined-behavior sanitizers" OFF)

# Common settings for every notgroot target: C++17 without extensions, warnings as errors on GCC/Clang, and
# ASan/UBSan when NOTGROOT_ENABLE_SANITIZERS is on (PUBLIC so dependents link the runtime too).
function(notgroot_project_options target)
  target_compile_features(${target} PUBLIC cxx_std_17)
  set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    if(NOTGROOT_ENABLE_SANITIZERS)
      target_compile_options(${target} PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
      target_link_options(${target} PUBLIC -fsanitize=address,undefined)
    endif()
  endif()
endfunction()

find_package(yaml-cpp REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(tinyxml2 REQUIRED IMPORTED_TARGET tinyxml2) # Ubuntu's package has no CMake config
find_package(glfw3 3.3 REQUIRED)
find_package(OpenGL REQUIRED)
find_package(GLEW REQUIRED)
find_package(PNG REQUIRED)
find_package(Fontconfig REQUIRED)
find_package(Freetype REQUIRED)

# Content (themes, fonts, icons) is read from the source tree in a build and from share/notgroot once installed.
set(NOTGROOT_CONTENT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/content)

# Vendored Dear ImGui (docking branch; GLFW + OpenGL 3 backends) as a static library; SYSTEM includes keep -Werror
# off its headers. Fonts are rasterized with FreeType (hinted, sharper than stb_truetype at small sizes).
set(imgui ${PROJECT_SOURCE_DIR}/third_party/imgui)
add_library(notgroot_imgui STATIC ${imgui}/imgui.cpp ${imgui}/imgui_draw.cpp
  ${imgui}/imgui_tables.cpp ${imgui}/imgui_widgets.cpp
  ${imgui}/backends/imgui_impl_glfw.cpp ${imgui}/backends/imgui_impl_opengl3.cpp
  ${imgui}/misc/freetype/imgui_freetype.cpp)
target_include_directories(notgroot_imgui SYSTEM PUBLIC ${imgui})
target_compile_definitions(notgroot_imgui PUBLIC IMGUI_ENABLE_FREETYPE)
target_link_libraries(notgroot_imgui PRIVATE glfw Freetype::Freetype ${CMAKE_DL_LIBS})

add_subdirectory(libraries/core)
add_subdirectory(libraries/ui)
add_subdirectory(app)
# The BehaviorTree.CPP check (fetches BT.CPP 4.10.0 once at configure).
set(NOTGROOT_BTCPP_SHA256 c758fdedb3666f7ca4c9998a4c0b243251e0a5fb5d03c99e2ee63d615af7a71d)
add_subdirectory(tools/check)

if(BUILD_TESTING)
  find_package(GTest REQUIRED)
  add_subdirectory(tests)
endif()

install(DIRECTORY content/ DESTINATION ${CMAKE_INSTALL_DATADIR}/notgroot)
install(FILES ${imgui}/LICENSE.txt DESTINATION ${CMAKE_INSTALL_DATADIR}/notgroot/legal RENAME imgui-MIT.txt)
