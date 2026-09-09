#pragma once

#include "node_engine/factory.hpp"
#include "node_engine/node.hpp"

#include <string>

namespace node_engine::nodes::line_quality {

// Generic multi-bool + scalar latch with external timestamp input (from edge t_stamp + duration).
// Branch A and B use different wire sets into separate instances.
class FeatureLatch : public NodeBase<FeatureLatch> {
 public:
  static constexpr Meta meta{.type_id = "lq_feature_latch", .role = Role::Transformer};

  static void ports(Ports& p) {
    p.in<double>("t_stamp").literal(0.0);
    p.in<bool>("b0").literal(true);
    p.in<bool>("b1").literal(true);
    p.in<bool>("b2").literal(false);
    p.in<double>("s0").literal(0.0);
    p.in<double>("s1").literal(0.0);
    p.out<double>("t_stamp");
    p.out<bool>("b0");
    p.out<bool>("b1");
    p.out<bool>("b2");
    p.out<double>("s0");
    p.out<double>("s1");
  }

  void compute(Slice) override {
    t_ = get<double>("t_stamp");
    b0_ = get<bool>("b0");
    b1_ = get<bool>("b1");
    b2_ = get<bool>("b2");
    s0_ = get<double>("s0");
    s1_ = get<double>("s1");
    set_out("t_stamp", t_);
    set_out("b0", b0_);
    set_out("b1", b1_);
    set_out("b2", b2_);
    set_out("s0", s0_);
    set_out("s1", s1_);
    suppress();
  }

  double t_stamp() const { return t_; }
  bool b0() const { return b0_; }
  bool b1() const { return b1_; }
  bool b2() const { return b2_; }
  double s0() const { return s0_; }
  double s1() const { return s1_; }

 private:
  double t_ = 0.0;
  bool b0_ = true;
  bool b1_ = true;
  bool b2_ = false;
  double s0_ = 0.0;
  double s1_ = 0.0;
};

REGISTER_NODE(FeatureLatch);

// Motion latch: line_ok bits + mm cursor.
class MotionLatch : public NodeBase<MotionLatch> {
 public:
  static constexpr Meta meta{.type_id = "lq_motion_latch", .role = Role::Transformer};

  static void ports(Ports& p) {
    p.in<double>("t_stamp").literal(0.0);
    p.in<bool>("line_running").literal(false);
    p.in<bool>("speed_stable").literal(true);
    p.in<double>("pos_mm").literal(0.0);
    p.in<double>("vel").literal(0.0);
    p.out<double>("t_stamp");
    p.out<bool>("line_running");
    p.out<bool>("speed_stable");
    p.out<bool>("line_ok");
    p.out<double>("pos_mm");
    p.out<double>("vel");
  }

  void compute(Slice) override {
    t_ = get<double>("t_stamp");
    run_ = get<bool>("line_running");
    stable_ = get<bool>("speed_stable");
    pos_ = get<double>("pos_mm");
    vel_ = get<double>("vel");
    bool ok = run_ && stable_;
    set_out("t_stamp", t_);
    set_out("line_running", run_);
    set_out("speed_stable", stable_);
    set_out("line_ok", ok);
    set_out("pos_mm", pos_);
    set_out("vel", vel_);
    suppress();
  }

  double t_stamp() const { return t_; }
  bool line_ok() const { return run_ && stable_; }
  double pos_mm() const { return pos_; }

 private:
  double t_ = 0.0;
  bool run_ = false;
  bool stable_ = true;
  double pos_ = 0.0;
  double vel_ = 0.0;
};

REGISTER_NODE(MotionLatch);

}  // namespace node_engine::nodes::line_quality
