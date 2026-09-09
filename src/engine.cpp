#include "node_engine/engine.hpp"

#include <queue>
#include <stdexcept>
#include <unordered_map>

namespace node_engine {

Engine::Engine(std::size_t worker_threads) : scheduler_(worker_threads) {}

Engine::Engine(std::shared_ptr<tf::Executor> executor) : scheduler_(std::move(executor)) {}

FlatGraph Engine::compile(Graph const& authoring) const {
  auto errors = validate_graph(authoring);
  if (!errors.empty()) {
    throw std::runtime_error("validate_graph failed: " + errors.front().message);
  }
  Graph copy = authoring.clone();
  propagate_types(copy);
  return compile_flat(copy);
}

void Engine::tick(FlatGraph& flat) {
  scheduler_.run_pass(flat);
}

FlatGraph Engine::run_once(Graph const& authoring) {
  FlatGraph flat = compile(authoring);
  // First tick: everything starts dirty.
  for (auto& n : flat.nodes) {
    if (n.node) {
      n.node->dirty = true;
    }
  }
  tick(flat);
  return flat;
}

void Engine::mark_dirty_reachable(FlatGraph& flat, std::string const& seed_id) const {
  std::unordered_map<std::string, std::vector<std::string>> successors;
  for (auto const& n : flat.nodes) {
    successors[n.id];
  }
  for (auto const& e : flat.edges) {
    successors[e.from_node].push_back(e.to_node);
  }

  std::queue<std::string> q;
  if (Node* seed = flat.find_node(seed_id)) {
    seed->dirty = true;
    q.push(seed_id);
  } else {
    return;
  }

  std::unordered_map<std::string, bool> seen;
  seen[seed_id] = true;
  while (!q.empty()) {
    auto id = q.front();
    q.pop();
    for (auto const& s : successors[id]) {
      if (seen[s]) {
        continue;
      }
      seen[s] = true;
      if (Node* n = flat.find_node(s)) {
        n->dirty = true;
      }
      q.push(s);
    }
  }
}

bool Engine::poll_trigger_and_run(FlatGraph& flat) {
  auto id = triggers_.try_pop();
  if (!id) {
    return false;
  }
  mark_dirty_reachable(flat, *id);
  tick(flat);
  return true;
}

}  // namespace node_engine
