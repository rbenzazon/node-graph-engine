#include "node_engine/scheduler.hpp"

#include <taskflow/taskflow.hpp>

#include <thread>

namespace node_engine {

Scheduler::Scheduler(std::size_t worker_threads)
    : worker_threads_(worker_threads == 0
                          ? std::max<std::size_t>(1, std::thread::hardware_concurrency())
                          : worker_threads) {}

void Scheduler::run_pass(FlatGraph& graph) {
  // Bootstrap: build a transient Taskflow that no-ops per node.
  // Real dirty-reachability + compute(Slice) arrives with the taskflow milestone.
  tf::Executor executor(worker_threads_);
  tf::Taskflow taskflow;

  for (auto& named : graph.nodes) {
    if (named.node == nullptr) {
      continue;
    }
    auto* node = named.node.get();
    taskflow.emplace([node]() {
      if (!node->dirty) {
        return;
      }
      node->compute(Slice{});
    });
  }

  if (!taskflow.empty()) {
    executor.run(taskflow).wait();
  }
}

}  // namespace node_engine
