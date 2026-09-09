#pragma once

#include "node_engine/flatten.hpp"

#include <taskflow/taskflow.hpp>

#include <cstddef>
#include <memory>

namespace node_engine {

// Session-scoped scheduler: owns one tf::Executor for the lifetime of this
// object. API consumers should keep Scheduler/Engine alive for the run
// session (constant feed + repeated tick). Each tick still builds a
// *transient* Taskflow from the dirty set; the worker pool is not recreated.
//
// Advanced: pass a shared executor to coordinate with other Taskflow work in
// the host app. If null, Scheduler creates its own pool with worker_threads
// (0 = hardware_concurrency).
class Scheduler {
 public:
  explicit Scheduler(std::size_t worker_threads = 0);
  explicit Scheduler(std::shared_ptr<tf::Executor> executor);

  // Copy values along edges, build transient Taskflow from dirty-reachable
  // nodes, run with optional intra-node ParallelHint splitting (subflow on
  // the session executor). Repeats local sub-ticks while nodes stay dirty.
  void run_pass(FlatGraph& graph);

  std::size_t worker_threads() const { return worker_threads_; }
  tf::Executor& executor() { return *executor_; }
  tf::Executor const& executor() const { return *executor_; }
  std::shared_ptr<tf::Executor> const& shared_executor() const { return executor_; }

 private:
  void run_wave(FlatGraph& graph);
  void copy_edges(FlatGraph& graph) const;
  void execute_node(Node& node, tf::Subflow& subflow) const;

  std::size_t worker_threads_;
  std::shared_ptr<tf::Executor> executor_;
  std::size_t max_subticks_ = 1024;
  std::size_t default_grain_ = 1024;
  std::size_t min_parallel_items_ = 64;
};

}  // namespace node_engine
