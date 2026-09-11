#pragma once

#include "lq_types.hpp"
#include "node_engine/factory.hpp"
#include "node_engine/node.hpp"

namespace node_engine::nodes::line_quality {

// Decision only: each parameter is its own pin (engine rule).
// op and operand are independent — never bundled in one struct.
// Unwired literals demo const params; either pin can later be wired instead.
class CompareScalar : public NodeBase<CompareScalar> {
 public:
  static constexpr Meta meta{.type_id = "lq_compare_scalar", .role = Role::Transformer};

  static void ports(Ports& p) {
    p.in<float>("value").literal(0.0f);
    p.in<std::int32_t>("op").literal(kOpLE);
    p.in<float>("operand").literal(0.0f);
    p.out<bool>("pass").literal(false);
  }

  void compute(Slice) override {
    float value = get<float>("value");
    std::int32_t op = get<std::int32_t>("op");
    float operand = get<float>("operand");
    bool pass = false;
    switch (op) {
      case kOpLT:
        pass = value < operand;
        break;
      case kOpLE:
        pass = value <= operand;
        break;
      case kOpGT:
        pass = value > operand;
        break;
      case kOpGE:
        pass = value >= operand;
        break;
      case kOpEQ:
        pass = value == operand;
        break;
      case kOpNE:
        pass = value != operand;
        break;
      default:
        pass = false;
        break;
    }
    set_out("pass", pass);
    suppress();
  }
};

REGISTER_NODE(CompareScalar);

}  // namespace node_engine::nodes::line_quality
