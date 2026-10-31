# notgroot-globals: the batch form of Make global, for converting a trees directory.
add_executable(notgroot-globals notgroot_globals.cpp)
target_link_libraries(notgroot-globals PRIVATE notgroot_core)
target_compile_definitions(notgroot-globals PRIVATE NOTGROOT_BUILTIN_MODELS="${NOTGROOT_CONTENT_DIR}/builtin_models.xml")
notgroot_project_options(notgroot-globals)
install(TARGETS notgroot-globals RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
