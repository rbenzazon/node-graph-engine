#pragma once

#include "frame_util.hpp"
#include "node_engine/factory.hpp"
#include "node_engine/node.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace node_engine::nodes::line_quality {

// Decode encoder pack (ch0=pos mm, ch1=vel) and motion gates. Last frame wins for latch outs.
class EncMotion : public NodeBase<EncMotion> {
 public:
  static constexpr Meta meta{.type_id = "lq_enc_motion", .role = Role::Transformer};

  static void ports(Ports& p) {
    p.in_buffer("in");
    p.in<double>("v_min").literal(0.5);
    p.in<double>("vel_std_max").literal(2.0);
    p.out<bool>("line_running").literal(false);
    p.out<bool>("speed_stable").literal(true);
    p.out<double>("pos_mm").literal(0.0);
    p.out<double>("vel").literal(0.0);
    p.out<double>("valid").literal(0.0);
  }

  void compute(Slice) override {
    auto const& in = buffer_in("in");
    std::size_t K = num_frames(in);
    if (K == 0) {
      set_out("line_running", false);
      set_out("speed_stable", false);
      suppress();
      return;
    }
    double v_min = get<double>("v_min");
    double vel_std_max = get<double>("vel_std_max");

    double last_pos = 0.0;
    double last_vel = 0.0;
    double last_valid = 0.0;
    double sum_v = 0.0;
    double sum_v2 = 0.0;

    for (std::size_t f = 0; f < K; ++f) {
      double const* src = frame_ptr(in, f);
      last_pos = src[0];
      last_vel = src[1];
      last_valid = src[4];
      sum_v += last_vel;
      sum_v2 += last_vel * last_vel;
      vel_hist_.push_back(last_vel);
      if (vel_hist_.size() > 16) {
        vel_hist_.erase(vel_hist_.begin());
      }
    }

    double mean_v = sum_v / static_cast<double>(K);
    double var = sum_v2 / static_cast<double>(K) - mean_v * mean_v;
    if (var < 0.0) {
      var = 0.0;
    }
    double std_emit = std::sqrt(var);

    // Prefer longer history std when available.
    double std_use = std_emit;
    if (vel_hist_.size() >= 4) {
      double s = 0.0;
      double s2 = 0.0;
      for (double v : vel_hist_) {
        s += v;
        s2 += v * v;
      }
      double m = s / static_cast<double>(vel_hist_.size());
      double vv = s2 / static_cast<double>(vel_hist_.size()) - m * m;
      if (vv < 0.0) {
        vv = 0.0;
      }
      std_use = std::sqrt(vv);
    }

    bool running = last_vel > v_min && last_valid > 0.5;
    bool stable = std_use <= vel_std_max;

    set_out("line_running", running);
    set_out("speed_stable", stable);
    set_out("pos_mm", last_pos);
    set_out("vel", last_vel);
    set_out("valid", last_valid);
    suppress();
  }

  void reset_state() { vel_hist_.clear(); }

 private:
  std::vector<double> vel_hist_;
};

REGISTER_NODE(EncMotion);

}  // namespace node_engine::nodes::line_quality
