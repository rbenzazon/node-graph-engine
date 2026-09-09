add_executable(smoke_test smoke_test.cpp)

target_link_libraries(smoke_test
  PRIVATE
    node_engine::node_engine
)

add_test(NAME smoke_test COMMAND smoke_test)
