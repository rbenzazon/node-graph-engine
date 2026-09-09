#include "node_engine/graph.hpp"

namespace node_engine {

Node* Graph::find_node(std::string const& id) {
  for (auto& named : nodes) {
    if (named.id == id) {
      return named.node.get();
    }
  }
  return nullptr;
}

Node const* Graph::find_node(std::string const& id) const {
  for (auto const& named : nodes) {
    if (named.id == id) {
      return named.node.get();
    }
  }
  return nullptr;
}

}  // namespace node_engine
