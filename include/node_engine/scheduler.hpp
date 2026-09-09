#pragma once

#include "node_engine/flatten.hpp"

#include <cstddef>

namespace node_engine {

class Scheduler {
 public:
  explicit Scheduler(std::size_t worker_threads = 0);

  // Build a transient Taskflow from dirty reachable nodes and run it.
  void run_pass(FlatGraph& graph);

 private:
  std::size_t worker_threads_;
};

}  // namespace node_engine
