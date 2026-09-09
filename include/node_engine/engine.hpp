#pragma once

#include "node_engine/flatten.hpp"
#include "node_engine/graph.hpp"
#include "node_engine/scheduler.hpp"
#include "node_engine/trigger_queue.hpp"

#include <memory>
#include <string>
#include <unordered_set>

namespace node_engine {

// Thin façade: validate → propagate → flatten → schedule.
//
// Intended session usage (API consumer owns lifetime):
//   Engine engine{N};                 // or Engine{shared_executor}
//   FlatGraph flat = engine.compile(g);
//   for (;;) { /* feed pins */ engine.tick(flat); }
//
// compile() once per authoring topology; tick() repeatedly with live data.
// The session tf::Executor lives as long as Engine (unless you injected one).
// Trigger queue wakes top-level passes; mid-graph events are node-local.
class Engine {
 public:
  explicit Engine(std::size_t worker_threads = 0);
  explicit Engine(std::shared_ptr<tf::Executor> executor);

  FlatGraph compile(Graph const& authoring) const;

  // Full tick on a flat graph (already compiled).
  void tick(FlatGraph& flat);

  // Compile authoring and run one tick (fresh flatten each call).
  // Convenience for tests; prefer compile() + tick() in long sessions.
  FlatGraph run_once(Graph const& authoring);

  // Mark a node dirty and all direct-or-transitive downstream consumers.
  void mark_dirty_reachable(FlatGraph& flat, std::string const& seed_id) const;

  TriggerQueue& triggers() { return triggers_; }
  TriggerQueue const& triggers() const { return triggers_; }
  Scheduler& scheduler() { return scheduler_; }
  Scheduler const& scheduler() const { return scheduler_; }

  // Pop one trigger (if any), mark dirty cone, run pass. Returns false if queue empty.
  bool poll_trigger_and_run(FlatGraph& flat);

 private:
  Scheduler scheduler_;
  TriggerQueue triggers_;
};

}  // namespace node_engine
