#include "node_engine/scheduler.hpp"

#include <algorithm>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace node_engine {
namespace {

std::size_t resolve_workers(std::size_t worker_threads) {
  if (worker_threads == 0) {
    return std::max<std::size_t>(1, std::thread::hardware_concurrency());
  }
  return worker_threads;
}

void copy_inbound_edges(FlatGraph& graph, std::string const& node_id) {
  Node* to = graph.find_node(node_id);
  if (!to) {
    return;
  }
  for (auto const& edge : graph.edges) {
    if (edge.to_node != node_id) {
      continue;
    }
    Node* from = graph.find_node(edge.from_node);
    if (!from) {
      continue;
    }
    Pin const* out_pin = from->find_output(edge.from_pin);
    Pin* in_pin = to->find_input(edge.to_pin);
    if (!out_pin || !in_pin) {
      continue;
    }
    Value src = out_pin->buffer;
    if (in_pin->type != TypeId::Dynamic && type_of(src) != TypeId::Dynamic &&
        type_of(src) != in_pin->type && can_autoconvert(type_of(src), in_pin->type)) {
      src = autoconvert(src, in_pin->type);
    }
    in_pin->buffer = std::move(src);
    in_pin->connected = true;
  }
}

}  // namespace

Scheduler::Scheduler(std::size_t worker_threads)
    : worker_threads_(resolve_workers(worker_threads)),
      executor_(std::make_shared<tf::Executor>(worker_threads_)) {}

Scheduler::Scheduler(std::shared_ptr<tf::Executor> executor)
    : worker_threads_(executor ? executor->num_workers() : resolve_workers(0)),
      executor_(executor ? std::move(executor)
                         : std::make_shared<tf::Executor>(worker_threads_)) {
  if (worker_threads_ == 0) {
    worker_threads_ = resolve_workers(0);
  }
}

void Scheduler::copy_edges(FlatGraph& graph) const {
  for (auto const& edge : graph.edges) {
    copy_inbound_edges(graph, edge.to_node);
  }
}

void Scheduler::execute_node(Node& node, tf::Subflow& subflow) const {
  ParallelHint hint = node.parallel_hint();
  std::size_t len = node.parallel_length();

  bool do_parallel = false;
  if (hint.mode == ParallelMode::Always && len > 1) {
    do_parallel = true;
  } else if (hint.mode == ParallelMode::OnCollection && len >= min_parallel_items_) {
    do_parallel = true;
  }

  if (!do_parallel) {
    node.compute(Slice{0, len == 0 ? 1 : len});
    return;
  }

  std::size_t grain = hint.grain_size == 0 ? default_grain_ : hint.grain_size;
  grain = std::max<std::size_t>(1, grain);

  if (len <= grain) {
    node.compute(Slice{0, len});
    return;
  }

  // Single-threaded prepare so nodes can size shared outputs safely.
  node.compute(Slice{0, 0});

  // Intra-node parallel on the *same* session executor via subflow (no nested
  // executor::run, which can deadlock when called from a worker thread).
  for (std::size_t begin = 0; begin < len; begin += grain) {
    std::size_t end = std::min(begin + grain, len);
    subflow.emplace([&node, begin, end]() { node.compute(Slice{begin, end}); });
  }
  subflow.join();
}

void Scheduler::run_wave(FlatGraph& graph) {
  std::unordered_map<std::string, std::vector<std::string>> successors;
  std::unordered_map<std::string, std::vector<std::string>> predecessors;
  for (auto const& n : graph.nodes) {
    successors[n.id];
    predecessors[n.id];
  }
  for (auto const& e : graph.edges) {
    successors[e.from_node].push_back(e.to_node);
    predecessors[e.to_node].push_back(e.from_node);
  }

  std::unordered_set<std::string> active;
  std::vector<std::string> stack;
  for (auto const& n : graph.nodes) {
    if (n.node && n.node->dirty) {
      active.insert(n.id);
      stack.push_back(n.id);
    }
  }
  while (!stack.empty()) {
    auto id = stack.back();
    stack.pop_back();
    for (auto const& s : successors[id]) {
      if (active.insert(s).second) {
        stack.push_back(s);
      }
    }
  }

  if (active.empty()) {
    return;
  }

  for (auto const& id : active) {
    if (Node* n = graph.find_node(id)) {
      n->dirty = true;
    }
  }

  // Transient Taskflow topology; session executor runs it.
  tf::Taskflow taskflow;
  std::unordered_map<std::string, tf::Task> tasks;

  for (auto const& id : active) {
    Node* node = graph.find_node(id);
    if (!node) {
      continue;
    }
    tasks.emplace(id, taskflow.emplace([this, &graph, id, node](tf::Subflow& sf) {
      copy_inbound_edges(graph, id);
      execute_node(*node, sf);
    }));
  }

  for (auto const& id : active) {
    auto it = tasks.find(id);
    if (it == tasks.end()) {
      continue;
    }
    for (auto const& pred : predecessors[id]) {
      auto pit = tasks.find(pred);
      if (pit != tasks.end()) {
        pit->second.precede(it->second);
      }
    }
  }

  executor_->run(taskflow).wait();
}

void Scheduler::run_pass(FlatGraph& graph) {
  for (std::size_t i = 0; i < max_subticks_; ++i) {
    bool any_dirty = false;
    for (auto const& n : graph.nodes) {
      if (n.node && n.node->dirty) {
        any_dirty = true;
        break;
      }
    }
    if (!any_dirty) {
      break;
    }
    run_wave(graph);
  }
}

}  // namespace node_engine
