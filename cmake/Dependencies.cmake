include(FetchContent)

set(TF_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(TF_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(TF_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
  taskflow
  GIT_REPOSITORY https://github.com/taskflow/taskflow.git
  GIT_TAG        v3.10.0
  GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(taskflow)

if(NOT TARGET Taskflow::Taskflow)
  add_library(Taskflow::Taskflow ALIAS Taskflow)
endif()
