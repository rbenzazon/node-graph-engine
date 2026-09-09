#pragma once

#include "node_engine/graph.hpp"
#include "node_engine/node.hpp"

#include <memory>
#include <string>
#include <utility>

namespace node_engine {

// Nesting wrapper: holds a blueprint inner graph. Flatten clones the inner
// graph per instance and splices parent wires to boundary neighbors.
class GraphNode : public Node {
 public:
  static constexpr Meta meta{
      .type_id = "graph_node",
      .role = Role::GraphWrapper,
  };

  // blueprint is shared as a template; each flatten instance clones nodes.
  explicit GraphNode(std::shared_ptr<Graph> blueprint, std::string display_name = {});

  std::string_view type_id() const override { return meta.type_id; }
  Role role() const override { return Role::GraphWrapper; }

  void compute(Slice) override {
    // Never executed: removed during flatten.
  }

  std::unique_ptr<Node> clone() const override;

  std::shared_ptr<Graph> const& blueprint() const { return blueprint_; }
  std::string const& display_name() const { return display_name_; }

 private:
  void rebuild_ports_from_blueprint();

  std::shared_ptr<Graph> blueprint_;
  std::string display_name_;
};

}  // namespace node_engine
