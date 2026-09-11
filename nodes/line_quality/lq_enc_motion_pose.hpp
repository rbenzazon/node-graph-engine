#pragma once

#include "lq_types.hpp"
#include "node_engine/factory.hpp"
#include "node_engine/node.hpp"

namespace node_engine::nodes::line_quality {

class LqEncMotionPose : public NodeBase<LqEncMotionPose> {
 public:
  static constexpr Meta meta{.type_id = "lq_enc_motion_pose", .role = Role::Transformer};

  static void ports(Ports& p) {
    p.in<EncoderPoseF32>("in");
    p.in<float>("v_min").literal(0.5f);
    p.out<bool>("line_running").literal(false);
    p.out<float>("pos_mm").literal(0.0f);
    p.out<float>("vel").literal(0.0f);
  }

  void compute(Slice) override {
    EncoderPoseF32 pose = get<EncoderPoseF32>("in");
    float v_min = get<float>("v_min");
    bool running = (pose.vel_x > v_min) && (pose.quality != 0);
    set_out("line_running", running);
    set_out("pos_mm", pose.x_mm);
    set_out("vel", pose.vel_x);
    suppress();
  }
};

REGISTER_NODE(LqEncMotionPose);

}  // namespace node_engine::nodes::line_quality
