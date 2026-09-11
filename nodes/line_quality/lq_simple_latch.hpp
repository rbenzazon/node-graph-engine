#pragma once

#include "lq_types.hpp"
#include "node_engine/factory.hpp"
#include "node_engine/node.hpp"

namespace node_engine::nodes::line_quality {

// LKG holders for branch summaries (members for demo reads).
class ThickPathLatch : public NodeBase<ThickPathLatch> {
 public:
  static constexpr Meta meta{.type_id = "lq_thick_path_latch", .role = Role::Transformer};

  static void ports(Ports& p) {
    p.in<double>("t_stamp").literal(0.0);
    p.in<bool>("ok").literal(false);
    p.in<float>("mean").literal(0.0f);
    p.out<double>("t_stamp").literal(0.0);
    p.out<bool>("ok").literal(false);
    p.out<float>("mean").literal(0.0f);
  }

  void compute(Slice) override {
    t_stamp_ = get<double>("t_stamp");
    ok_ = get<bool>("ok");
    mean_ = get<float>("mean");
    set_out("t_stamp", t_stamp_);
    set_out("ok", ok_);
    set_out("mean", mean_);
    suppress();
  }

  double t_stamp() const { return t_stamp_; }
  bool ok() const { return ok_; }
  float mean() const { return mean_; }

 private:
  double t_stamp_ = 0.0;
  bool ok_ = false;
  float mean_ = 0.0f;
};

class DistPathLatch : public NodeBase<DistPathLatch> {
 public:
  static constexpr Meta meta{.type_id = "lq_dist_path_latch", .role = Role::Transformer};

  static void ports(Ports& p) {
    p.in<double>("t_stamp").literal(0.0);
    p.in<bool>("height_ok").literal(false);
    p.in<bool>("dent").literal(false);
    p.out<double>("t_stamp").literal(0.0);
    p.out<bool>("height_ok").literal(false);
    p.out<bool>("dent").literal(false);
    p.out<bool>("ok").literal(false);
  }

  void compute(Slice) override {
    t_stamp_ = get<double>("t_stamp");
    height_ok_ = get<bool>("height_ok");
    dent_ = get<bool>("dent");
    ok_ = height_ok_ && !dent_;
    set_out("t_stamp", t_stamp_);
    set_out("height_ok", height_ok_);
    set_out("dent", dent_);
    set_out("ok", ok_);
    suppress();
  }

  double t_stamp() const { return t_stamp_; }
  bool height_ok() const { return height_ok_; }
  bool dent() const { return dent_; }
  bool ok() const { return ok_; }

 private:
  double t_stamp_ = 0.0;
  bool height_ok_ = false;
  bool dent_ = false;
  bool ok_ = false;
};

class MotionPathLatch : public NodeBase<MotionPathLatch> {
 public:
  static constexpr Meta meta{.type_id = "lq_motion_path_latch", .role = Role::Transformer};

  static void ports(Ports& p) {
    p.in<double>("t_stamp").literal(0.0);
    p.in<bool>("line_running").literal(false);
    p.in<float>("pos_mm").literal(0.0f);
    p.out<double>("t_stamp").literal(0.0);
    p.out<bool>("line_running").literal(false);
    p.out<float>("pos_mm").literal(0.0f);
  }

  void compute(Slice) override {
    t_stamp_ = get<double>("t_stamp");
    line_running_ = get<bool>("line_running");
    pos_mm_ = get<float>("pos_mm");
    set_out("t_stamp", t_stamp_);
    set_out("line_running", line_running_);
    set_out("pos_mm", pos_mm_);
    suppress();
  }

  double t_stamp() const { return t_stamp_; }
  bool line_running() const { return line_running_; }
  float pos_mm() const { return pos_mm_; }

 private:
  double t_stamp_ = 0.0;
  bool line_running_ = false;
  float pos_mm_ = 0.0f;
};

// Fault-path latch: records last fault features routed via RouteFeatures.b
class FaultFeatureLatch : public NodeBase<FaultFeatureLatch> {
 public:
  static constexpr Meta meta{.type_id = "lq_fault_feature_latch", .role = Role::Transformer};

  static void ports(Ports& p) {
    p.in<FrameFeaturesF32>("in");
    p.out<FrameFeaturesF32>("out");
    p.out<bool>("has_fault").literal(false);
    p.out<float>("mean").literal(0.0f);
  }

  void compute(Slice) override {
    FrameFeaturesF32 f = get<FrameFeaturesF32>("in");
    // Empty route pin has signal_id==0 and mean==0; treat non-zero signal as active fault payload.
    has_fault_ = (f.signal_id != 0);
    if (has_fault_) {
      last_ = f;
    }
    set_out("out", last_);
    set_out("has_fault", has_fault_);
    set_out("mean", last_.mean);
    suppress();
  }

  bool has_fault() const { return has_fault_; }
  FrameFeaturesF32 const& last() const { return last_; }

  void reset() {
    has_fault_ = false;
    last_ = {};
  }

 private:
  bool has_fault_ = false;
  FrameFeaturesF32 last_{};
};

REGISTER_NODE(ThickPathLatch);
REGISTER_NODE(DistPathLatch);
REGISTER_NODE(MotionPathLatch);
REGISTER_NODE(FaultFeatureLatch);

}  // namespace node_engine::nodes::line_quality
