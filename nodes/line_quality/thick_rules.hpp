#pragma once

#include "frame_util.hpp"
#include "node_engine/factory.hpp"
#include "node_engine/node.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace node_engine::nodes::line_quality {

// Fast thickness rules on per-frame mean/p2p streams (length K). Stateful spike detect.
class ThickRules : public NodeBase<ThickRules> {
 public:
  static constexpr Meta meta{.type_id = "lq_thick_rules", .role = Role::Transformer};

  static void ports(Ports& p) {
    p.in_buffer("mean");
    p.in_buffer("p2p");
    p.in<double>("lo").literal(1.90);
    p.in<double>("hi").literal(2.10);
    p.in<double>("uniform_max").literal(0.15);
    p.in<double>("spike_delta").literal(0.25);
    p.out<bool>("in_spec").literal(true);
    p.out<bool>("uniform").literal(true);
    p.out<bool>("spike").literal(false);
    p.out<double>("last_mean").literal(0.0);
    p.out<double>("last_p2p").literal(0.0);
  }

  void compute(Slice) override {
    auto const& mean = buffer_in("mean");
    auto const& p2p = buffer_in("p2p");
    if (mean.size() != p2p.size() || mean.empty()) {
      set_out("in_spec", false);
      set_out("uniform", false);
      set_out("spike", false);
      suppress();
      return;
    }
    double lo = get<double>("lo");
    double hi = get<double>("hi");
    double uni_max = get<double>("uniform_max");
    double spike_d = get<double>("spike_delta");

    bool all_spec = true;
    bool all_uni = true;
    bool any_spike = false;
    double last_m = mean.back();
    double last_p = p2p.back();

    for (std::size_t i = 0; i < mean.size(); ++i) {
      double m = mean[i];
      double p = p2p[i];
      if (m < lo || m > hi) {
        all_spec = false;
      }
      if (p > uni_max) {
        all_uni = false;
      }
      if (have_prev_) {
        if (std::abs(m - prev_mean_) >= spike_d) {
          any_spike = true;
        }
      }
      prev_mean_ = m;
      have_prev_ = true;
    }

    set_out("in_spec", all_spec);
    set_out("uniform", all_uni);
    set_out("spike", any_spike);
    set_out("last_mean", last_m);
    set_out("last_p2p", last_p);
    suppress();
  }

  void reset_state() {
    have_prev_ = false;
    prev_mean_ = 0.0;
  }

 private:
  bool have_prev_ = false;
  double prev_mean_ = 0.0;
};

REGISTER_NODE(ThickRules);

}  // namespace node_engine::nodes::line_quality
