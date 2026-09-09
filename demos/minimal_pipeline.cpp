#include "node_engine/factory.hpp"
#include "node_engine/flatten.hpp"
#include "node_engine/graph.hpp"
#include "node_engine/scheduler.hpp"

#include <iostream>
#include <taskflow/taskflow.hpp>

int main() {
  std::cout << "node_engine bootstrap ok\n";
  std::cout << "C++20 + Taskflow hello demo\n";

  // Prove Taskflow is linked and runnable.
  tf::Executor executor{2};
  tf::Taskflow taskflow;
  taskflow.emplace([]() { /* no-op */ });
  executor.run(taskflow).wait();

  node_engine::Graph graph;
  auto flat = node_engine::compile_flat(graph);
  node_engine::Scheduler scheduler{2};
  scheduler.run_pass(flat);

  auto types = node_engine::NodeFactory::instance().registered_types();
  std::cout << "registered node types: " << types.size() << '\n';
  std::cout << "flat edges: " << flat.edges.size() << '\n';
  return 0;
}
