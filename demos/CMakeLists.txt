add_executable(minimal_pipeline minimal_pipeline.cpp)

target_link_libraries(minimal_pipeline
  PRIVATE
    node_engine::node_engine
)

if(MSVC)
  target_compile_options(minimal_pipeline PRIVATE /W4 /permissive-)
else()
  target_compile_options(minimal_pipeline PRIVATE -Wall -Wextra -Wpedantic)
endif()
