cmake_minimum_required(VERSION 3.20)

project(node_engine
  VERSION 0.1.0
  DESCRIPTION "C++20 hybrid node-graph engine with Taskflow backend"
  LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(NODE_ENGINE_BUILD_DEMOS "Build demo executables" ON)
option(NODE_ENGINE_BUILD_TESTS "Build tests" ON)

include(cmake/Dependencies.cmake)

add_library(node_engine
  src/engine.cpp
  src/factory.cpp
  src/flatten.cpp
  src/graph.cpp
  src/graph_node.cpp
  src/node.cpp
  src/register_builtin_nodes.cpp
  src/scheduler.cpp
  src/trigger_queue.cpp
)

add_library(node_engine::node_engine ALIAS node_engine)

target_include_directories(node_engine
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/nodes
)

target_link_libraries(node_engine
  PUBLIC
    Taskflow::Taskflow
)

target_compile_features(node_engine PUBLIC cxx_std_20)

if(MSVC)
  target_compile_options(node_engine PRIVATE /W4 /permissive- /Zc:__cplusplus)
else()
  target_compile_options(node_engine PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(NODE_ENGINE_BUILD_DEMOS)
  add_subdirectory(demos)
endif()

if(NODE_ENGINE_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
