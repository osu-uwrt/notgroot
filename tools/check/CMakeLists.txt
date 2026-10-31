# notgroot-check: BehaviorTree.CPP 4.10.0 (the robot's, apt ros-humble-behaviortree-cpp) built from source without ROS,
# so the editor checks trees with the parser doTask uses. Its own tests, tools and loggers stay out of the build.
include(FetchContent)
FetchContent_Declare(behaviortree_cpp
  URL https://github.com/BehaviorTree/BehaviorTree.CPP/archive/refs/tags/4.10.0.tar.gz
  URL_HASH SHA256=${NOTGROOT_BTCPP_SHA256})
FetchContent_GetProperties(behaviortree_cpp)
if(NOT behaviortree_cpp_POPULATED)
  FetchContent_Populate(behaviortree_cpp)
  set(saved_testing ${BUILD_TESTING})
  set(BUILD_TESTING OFF)
  set(BTCPP_SHARED_LIBS OFF CACHE BOOL "" FORCE)
  set(BTCPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
  set(BTCPP_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(BTCPP_GROOT_INTERFACE OFF CACHE BOOL "" FORCE)
  set(BTCPP_SQLITE_LOGGING OFF CACHE BOOL "" FORCE)
  # Plain CMake even in a shell with ROS sourced (ament would swap in ROS's tinyxml2 and rclcpp bits).
  set(CMAKE_DISABLE_FIND_PACKAGE_ament_cmake TRUE)
  add_subdirectory(${behaviortree_cpp_SOURCE_DIR} ${behaviortree_cpp_BINARY_DIR} EXCLUDE_FROM_ALL)
  set(BUILD_TESTING ${saved_testing})
  unset(CMAKE_DISABLE_FIND_PACKAGE_ament_cmake)
endif()

add_executable(notgroot-check notgroot_check.cpp)
target_link_libraries(notgroot-check PRIVATE behaviortree_cpp tinyxml2::tinyxml2)
notgroot_project_options(notgroot-check)
install(TARGETS notgroot-check RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
