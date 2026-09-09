#pragma once

#include "node_engine/graph.hpp"

#include <string>
#include <vector>

namespace node_engine {

struct FlatEdge {
  std::string from_node;
  std::string from_pin;
  std::string to_node;
  std::string to_pin;
};

struct FlatGraph {
  std::vector<NamedNode> nodes;
  std::vector<FlatEdge> edges;

  Node* find_node(std::string const& id);
  Node const* find_node(std::string const& id) const;
  NamedNode* find_named(std::string const& id);
};

// Clone GraphNode macros, splice parent wires to inner neighbors,
// drop wrappers + boundary nodes. Scheduler never sees nesting.
// Authoring graph is not mutated (nodes are cloned).
FlatGraph compile_flat(Graph const& authoring);

}  // namespace node_engine
