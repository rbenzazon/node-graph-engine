#pragma once

#include "frame_util.hpp"
#include "node_engine/factory.hpp"
#include "node_engine/node.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace node_engine::nodes::line_quality {

// Per-frame mean / min / max / peak-to-peak over 24 channels. Feature outs length K.
class FrameStats : public NodeBase<FrameStats> {
 public:
  static constexpr Meta meta{.type_id = "lq_frame_stats", .role = Role::Transformer};

  static void ports(Ports& p) {
    p.in_buffer("in");
    p.out_buffer("mean");
    p.out_buffer("min");
    p.out_buffer("max");
    p.out_buffer("p2p");
    p.out_buffer("out");  // pass-through geometry for downstream coalesce
  }

  void compute(Slice) override {
    auto const& in = buffer_in("in");
    std::size_t K = num_frames(in);
    auto& mean = buffer_out("mean");
    auto& mn = buffer_out("min");
    auto& mx = buffer_out("max");
    auto& p2p = buffer_out("p2p");
    auto& pass = buffer_out("out");
    mean.resize(K);
    mn.resize(K);
    mx.resize(K);
    p2p.resize(K);
    pass = in;

    for (std::size_t f = 0; f < K; ++f) {
      double const* src = frame_ptr(in, f);
      double lo = src[0];
      double hi = src[0];
      double sum = 0.0;
      for (std::size_t c = 0; c < kChannels; ++c) {
        double v = src[c];
        sum += v;
        lo = std::min(lo, v);
        hi = std::max(hi, v);
      }
      mean[f] = sum / static_cast<double>(kChannels);
      mn[f] = lo;
      mx[f] = hi;
      p2p[f] = hi - lo;
    }
    suppress();
  }
};

REGISTER_NODE(FrameStats);

}  // namespace node_engine::nodes::line_quality
