#pragma once

#include "lq_types.hpp"
#include "node_engine/factory.hpp"
#include "node_engine/node.hpp"

#include <algorithm>
#include <limits>

namespace node_engine::nodes::line_quality {

// Per-emit features from the last frame in the chunk; also exposes mean as float for CompareScalar.
class LqStatsF32 : public NodeBase<LqStatsF32> {
 public:
  static constexpr Meta meta{.type_id = "lq_stats_f32", .role = Role::Transformer};

  static void ports(Ports& p) {
    p.in<ChannelChunkF32>("in");
    p.out<FrameFeaturesF32>("features");
    p.out<float>("mean").literal(0.0f);
  }

  void compute(Slice) override {
    ChannelChunkF32 in = get<ChannelChunkF32>("in");
    std::size_t K = num_frames(in);
    FrameFeaturesF32 feat;
    feat.signal_id = in.signal_id;
    feat.t_s = in.t_s;
    if (K == 0 || in.channels == 0) {
      set_out("features", feat);
      set_out("mean", 0.0f);
      suppress();
      return;
    }

    float const* fr = frame_ptr(in, K - 1);
    std::size_t ch = in.channels;
    float sum = 0.0f;
    float lo = std::numeric_limits<float>::infinity();
    float hi = -std::numeric_limits<float>::infinity();
    for (std::size_t c = 0; c < ch; ++c) {
      float v = fr[c];
      sum += v;
      lo = std::min(lo, v);
      hi = std::max(hi, v);
    }
    feat.mean = sum / static_cast<float>(ch);
    feat.min_v = lo;
    feat.max_v = hi;
    feat.p2p = hi - lo;
    set_out("features", feat);
    set_out("mean", feat.mean);
    suppress();
  }
};

REGISTER_NODE(LqStatsF32);

}  // namespace node_engine::nodes::line_quality
