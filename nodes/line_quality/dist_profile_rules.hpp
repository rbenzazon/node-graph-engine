#pragma once

#include "frame_util.hpp"
#include "node_engine/factory.hpp"
#include "node_engine/node.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace node_engine::nodes::line_quality {

// Distance/height profile: plane residual proxy, dent on channels 10-12, height band.
class DistProfileRules : public NodeBase<DistProfileRules> {
 public:
  static constexpr Meta meta{.type_id = "lq_dist_profile_rules", .role = Role::Transformer};

  static void ports(Ports& p) {
    p.in_buffer("in");
    p.in<double>("height_lo").literal(-0.5);
    p.in<double>("height_hi").literal(0.5);
    p.in<double>("flat_max").literal(0.35);
    p.in<double>("dent_thr").literal(0.4);
    p.out<bool>("height_ok").literal(true);
    p.out<bool>("flat").literal(true);
    p.out<bool>("dent").literal(false);
    p.out<double>("last_mean").literal(0.0);
    p.out<double>("last_bow").literal(0.0);
  }

  void compute(Slice) override {
    auto const& in = buffer_in("in");
    std::size_t K = num_frames(in);
    if (K == 0) {
      set_out("height_ok", false);
      set_out("flat", false);
      set_out("dent", false);
      suppress();
      return;
    }
    double hlo = get<double>("height_lo");
    double hhi = get<double>("height_hi");
    double flat_max = get<double>("flat_max");
    double dent_thr = get<double>("dent_thr");

    bool height_ok = true;
    bool flat = true;
    bool dent = false;
    double last_mean = 0.0;
    double last_bow = 0.0;

    for (std::size_t f = 0; f < K; ++f) {
      double const* src = frame_ptr(in, f);
      double sum = 0.0;
      double lo = src[0];
      double hi = src[0];
      for (std::size_t c = 0; c < kChannels; ++c) {
        sum += src[c];
        lo = std::min(lo, src[c]);
        hi = std::max(hi, src[c]);
      }
      double mean = sum / static_cast<double>(kChannels);
      double bow = hi - lo;
      last_mean = mean;
      last_bow = bow;
      if (mean < hlo || mean > hhi) {
        height_ok = false;
      }
      if (bow > flat_max) {
        flat = false;
      }
      // Dent: channels 10..12 deeply below mean.
      for (std::size_t c = 10; c <= 12 && c < kChannels; ++c) {
        if (mean - src[c] >= dent_thr) {
          dent = true;
        }
      }
    }

    set_out("height_ok", height_ok);
    set_out("flat", flat);
    set_out("dent", dent);
    set_out("last_mean", last_mean);
    set_out("last_bow", last_bow);
    suppress();
  }
};

REGISTER_NODE(DistProfileRules);

}  // namespace node_engine::nodes::line_quality
