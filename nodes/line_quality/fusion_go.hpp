#pragma once

#include "node_engine/factory.hpp"
#include "node_engine/node.hpp"

#include <algorithm>
#include <cmath>

namespace node_engine::nodes::line_quality {

// Late join: LKG latch inputs + freshness vs t_now + sustain nogo in mm or ms.
class FusionGo : public NodeBase<FusionGo> {
 public:
  static constexpr Meta meta{.type_id = "lq_fusion_go", .role = Role::Transformer};

  static void ports(Ports& p) {
    p.in<double>("t_now").literal(0.0);
    // Latch A: thick
    p.in<double>("t_a").literal(-1e9);
    p.in<bool>("thick_in_spec").literal(true);
    p.in<bool>("thick_uniform").literal(true);
    p.in<bool>("thick_spike").literal(false);
    // Latch B: dist
    p.in<double>("t_b").literal(-1e9);
    p.in<bool>("height_ok").literal(true);
    p.in<bool>("flat").literal(true);
    p.in<bool>("dent").literal(false);
    // Latch C: motion
    p.in<double>("t_c").literal(-1e9);
    p.in<bool>("line_ok").literal(false);
    p.in<double>("pos_mm").literal(0.0);
    // Policy
    p.in<double>("stale_a").literal(0.001);
    p.in<double>("stale_b").literal(0.003);
    p.in<double>("stale_c").literal(0.015);
    p.in<double>("sustain_ms").literal(0.010);
    p.in<double>("sustain_mm").literal(10.0);
    p.out<bool>("go").literal(false);
    p.out<bool>("nogo_latch").literal(false);
    p.out<bool>("in_spec").literal(false);
    p.out<bool>("fresh_ok").literal(false);
    p.out<double>("grade").literal(0.0);
    p.out<double>("fault_code").literal(0.0);
  }

  void compute(Slice) override {
    double t_now = get<double>("t_now");
    double t_a = get<double>("t_a");
    double t_b = get<double>("t_b");
    double t_c = get<double>("t_c");

    bool fresh_a = (t_now - t_a) <= get<double>("stale_a");
    bool fresh_b = (t_now - t_b) <= get<double>("stale_b");
    bool fresh_c = (t_now - t_c) <= get<double>("stale_c");

    bool thick_ok = get<bool>("thick_in_spec") && get<bool>("thick_uniform") && !get<bool>("thick_spike");
    bool dist_ok = get<bool>("height_ok") && get<bool>("flat") && !get<bool>("dent");
    bool in_spec = thick_ok && dist_ok;
    bool line_ok = get<bool>("line_ok") && fresh_c;
    bool fresh_ok = fresh_a && fresh_b && fresh_c;
    bool go = in_spec && line_ok && fresh_a && fresh_b;

    double pos = get<double>("pos_mm");
    double sustain_ms = get<double>("sustain_ms");
    double sustain_mm = get<double>("sustain_mm");

    // Sustained out-of-family while line_ok (use fresh geometry intent: !in_spec && line_ok).
    bool bad_run = (!in_spec) && line_ok;
    if (bad_run) {
      if (!accum_active_) {
        accum_active_ = true;
        bad_t0_ = t_now;
        bad_pos0_ = pos;
      }
      double dt = t_now - bad_t0_;
      double dmm = std::abs(pos - bad_pos0_);
      if (dt >= sustain_ms || dmm >= sustain_mm) {
        nogo_latched_ = true;
      }
    } else {
      accum_active_ = false;
    }

    double fault = 0.0;
    if (get<bool>("thick_spike")) {
      fault = 1.0;
    } else if (get<bool>("dent")) {
      fault = 2.0;
    } else if (!get<bool>("thick_in_spec") || !get<bool>("thick_uniform")) {
      fault = 3.0;
    } else if (!get<bool>("flat") || !get<bool>("height_ok")) {
      fault = 4.0;
    } else if (!line_ok) {
      fault = 5.0;
    } else if (!fresh_ok) {
      fault = 6.0;
    }

    double grade = go ? 1.0 : (in_spec ? 0.5 : 0.0);

    set_out("go", go);
    set_out("nogo_latch", nogo_latched_);
    set_out("in_spec", in_spec);
    set_out("fresh_ok", fresh_ok);
    set_out("grade", grade);
    set_out("fault_code", fault);
    suppress();
  }

  void reset_state() {
    nogo_latched_ = false;
    accum_active_ = false;
    bad_t0_ = 0.0;
    bad_pos0_ = 0.0;
  }

  bool nogo_latched() const { return nogo_latched_; }

 private:
  bool nogo_latched_ = false;
  bool accum_active_ = false;
  double bad_t0_ = 0.0;
  double bad_pos0_ = 0.0;
};

REGISTER_NODE(FusionGo);

}  // namespace node_engine::nodes::line_quality
