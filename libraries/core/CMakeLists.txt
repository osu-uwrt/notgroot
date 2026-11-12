# The editor's model, without UI: XML documents, node models, the workspace, undo, edits, layout, variables, search,
# validation, runs (.btlog) and the live link to doTask's run monitor (ZeroMQ).
add_library(notgroot_core
  src/xml.cpp
  src/models.cpp
  src/workspace.cpp
  src/history.cpp
  src/edit.cpp
  src/globals.cpp
  src/variables.cpp
  src/validate.cpp
  src/layout.cpp
  src/search.cpp
  src/bt_check.cpp
  src/run.cpp
  src/run_client.cpp)
target_include_directories(notgroot_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
find_package(Threads REQUIRED)
target_link_libraries(notgroot_core PRIVATE PkgConfig::tinyxml2 PkgConfig::libzmq ZLIB::ZLIB Threads::Threads)
notgroot_project_options(notgroot_core)
