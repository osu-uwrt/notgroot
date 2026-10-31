# The editor's model, without UI: XML documents, node models, the workspace, undo, edits, layout, variables, search
# and validation.
add_library(notgroot_core
  src/xml.cpp
  src/models.cpp
  src/workspace.cpp
  src/history.cpp
  src/edit.cpp
  src/variables.cpp
  src/validate.cpp
  src/layout.cpp
  src/search.cpp)
target_include_directories(notgroot_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(notgroot_core PRIVATE PkgConfig::tinyxml2)
notgroot_project_options(notgroot_core)
