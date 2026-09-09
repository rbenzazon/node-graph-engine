add_executable(smoke_test smoke_test.cpp)

target_link_libraries(smoke_test
  PRIVATE
    node_engine::node_engine
)

target_include_directories(smoke_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/nodes
)

add_test(NAME smoke_test COMMAND smoke_test)
