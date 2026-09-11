#pragma once

#include "lq_types.hpp"
#include "node_engine/factory.hpp"
#include "node_engine/node.hpp"

namespace node_engine::nodes::line_quality {

// Routing only — no compare logic. Decision comes from CompareScalar.pass → sel.
class RouteFeatures : public NodeBase<RouteFeatures> {
 public:
  static constexpr Meta meta{.type_id = "lq_route_features", .role = Role::Transformer};

  static void ports(Ports& p) {
    p.in<FrameFeaturesF32>("data");
    p.in<bool>("sel").literal(true);
    p.out<FrameFeaturesF32>("a");
    p.out<FrameFeaturesF32>("b");
  }

  void compute(Slice) override {
    FrameFeaturesF32 data = get<FrameFeaturesF32>("data");
    bool sel = get<bool>("sel");
    if (sel) {
      set_out("a", data);
      set_out("b", FrameFeaturesF32{});
    } else {
      set_out("a", FrameFeaturesF32{});
      set_out("b", data);
    }
    suppress();
  }
};

REGISTER_NODE(RouteFeatures);

}  // namespace node_engine::nodes::line_quality
