#pragma once

#include "node_engine/factory.hpp"
#include "node_engine/node.hpp"

namespace node_engine::nodes::line_quality {

class AndBool : public NodeBase<AndBool> {
 public:
  static constexpr Meta meta{.type_id = "lq_and_bool", .role = Role::Transformer};

  static void ports(Ports& p) {
    p.in<bool>("a").literal(false);
    p.in<bool>("b").literal(false);
    p.out<bool>("pass").literal(false);
  }

  void compute(Slice) override {
    set_out("pass", get<bool>("a") && get<bool>("b"));
    suppress();
  }
};

REGISTER_NODE(AndBool);

}  // namespace node_engine::nodes::line_quality
