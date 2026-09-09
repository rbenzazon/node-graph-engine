#include "node_engine/flatten.hpp"

namespace node_engine {

// Bootstrap stub: copy authoring graph as-is (no nesting yet).
// Real splice/clone of GraphNode macros lands in the flatten milestone.
FlatGraph compile_flat(Graph const& authoring) {
  FlatGraph flat;

  for (auto const& named : authoring.nodes) {
    // Nodes are not cloneable yet; flatten ownership comes later.
    // For bootstrap we only preserve topology ids/edges.
    (void)named;
  }

  for (auto const& wire : authoring.wires) {
    flat.edges.push_back(FlatEdge{
        wire.from_node,
        wire.from_pin,
        wire.to_node,
        wire.to_pin,
    });
  }

  return flat;
}

}  // namespace node_engine
