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
  NamedNode* find_named(std::string const& id);
  NamedNode const* find_named(std::string const& id) const;

  void add_node(std::string id, std::unique_ptr<Node> node);
  void connect(std::string from_node, std::string from_pin, std::string to_node,
               std::string to_pin);

  // Deep-clone nodes + wires (new node instances).
  Graph clone() const;
};

struct ValidationError {
  std::string message;
};

std::vector<ValidationError> validate_graph(Graph const& graph);
void propagate_types(Graph& graph);

}  // namespace node_engine
