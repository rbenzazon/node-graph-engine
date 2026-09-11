#include "node_engine/graph_node.hpp"

namespace node_engine {

GraphNode::GraphNode(std::shared_ptr<Graph> blueprint, std::string display_name)
    : blueprint_(std::move(blueprint)), display_name_(std::move(display_name)) {
  rebuild_ports_from_blueprint();
}

std::unique_ptr<Node> GraphNode::clone() const {
  // Share blueprint template; instance state lives in flattened clones.
  return std::make_unique<GraphNode>(blueprint_, display_name_);
}

void GraphNode::rebuild_ports_from_blueprint() {
  inputs.clear();
  outputs.clear();
  if (!blueprint_) {
    return;
  }
  for (auto const& named : blueprint_->nodes) {
    if (!named.node) {
      continue;
    }
    if (named.node->role() == Role::BoundaryIn) {
      // Outer input port name = boundary node id. Type from first boundary output.
      TypeId t = TypeId::Dynamic;
      std::string tk;
      if (!named.node->outputs.empty()) {
        t = named.node->outputs.front().type;
        tk = named.node->outputs.front().type_key;
      }
      Pin p;
      p.id = named.id;
      p.type = t;
      p.type_key = std::move(tk);
      inputs.push_back(std::move(p));
    } else if (named.node->role() == Role::BoundaryOut) {
      TypeId t = TypeId::Dynamic;
      std::string tk;
      if (!named.node->inputs.empty()) {
        t = named.node->inputs.front().type;
        tk = named.node->inputs.front().type_key;
      }
      Pin p;
      p.id = named.id;
      p.type = t;
      p.type_key = std::move(tk);
      outputs.push_back(std::move(p));
    }
  }
}

}  // namespace node_engine
