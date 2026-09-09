#pragma once

#include "node_engine/factory.hpp"
#include "node_engine/node.hpp"

namespace node_engine::nodes {

// Temporal example: arm on rising edge of arm pin; fire on rising edge of go while armed.
class ArmThenGo : public NodeBase<ArmThenGo> {
 public:
  static constexpr Meta meta{.type_id = "arm_then_go", .role = Role::Transformer};

  static void ports(Ports& p) {
    p.in<bool>("arm").literal(false);
    p.in<bool>("go").literal(false);
    p.in<double>("payload").literal(0.0);
    p.out<bool>("fired");
    p.out<double>("value");
  }

  void compute(Slice) override {
    bool arm = get<bool>("arm");
    bool go = get<bool>("go");
    if (rising_edge(prev_arm_, arm)) {
      armed_ = true;
    }
    bool fire = armed_ && rising_edge(prev_go_, go);
    prev_arm_ = arm;
    prev_go_ = go;
    if (fire) {
      armed_ = false;
      set_out("fired", true);
      set_out("value", get<double>("payload"));
    } else {
      set_out("fired", false);
    }
    suppress();
  }

 private:
  bool armed_ = false;
  bool prev_arm_ = false;
  bool prev_go_ = false;
};

REGISTER_NODE(ArmThenGo);

}  // namespace node_engine::nodes
