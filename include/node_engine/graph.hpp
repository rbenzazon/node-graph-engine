#pragma once

#include "node_engine/node.hpp"

#include <memory>
#include <string>
#include <vector>

namespace node_engine {

struct Wire {
  std::string from_node;
  std::string from_pin;
  std::string to_node;
  std::string to_pin;
};

struct NamedNode {
  std::string id;
  std::unique_ptr<Node> node;
};

class Graph {
 public:
  std::vector<NamedNode> nodes;
  std::vector<Wire> wires;

  Node* find_node(std::string const& id);
  Node const* find_node(std::string const& id) const;
};

}  // namespace node_engine
