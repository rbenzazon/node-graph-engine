#pragma once

#include "lq_types.hpp"
#include "node_engine/factory.hpp"
#include "node_engine/node.hpp"

namespace node_engine::nodes::line_quality {

class LqDentRules : public NodeBase<LqDentRules> {
 public:
  static constexpr Meta meta{.type_id = "lq_dent_rules", .role = Role::Transformer};

  static void ports(Ports& p) {
    p.in<ChannelChunkF32>("in");
    p.in<float>("height_lo").literal(-0.5f);
    p.in<float>("height_hi").literal(0.5f);
    p.in<float>("dent_thr").literal(0.4f);
    p.out<bool>("height_ok").literal(true);
    p.out<bool>("dent").literal(false);
    p.out<float>("last_mean").literal(0.0f);
  }

  void compute(Slice) override {
    ChannelChunkF32 in = get<ChannelChunkF32>("in");
    float hlo = get<float>("height_lo");
    float hhi = get<float>("height_hi");
    float thr = get<float>("dent_thr");
    std::size_t K = num_frames(in);
    if (K == 0) {
      set_out("height_ok", false);
      set_out("dent", false);
      set_out("last_mean", 0.0f);
      suppress();
      return;
    }

    bool height_ok = true;
    bool dent = false;
    float last_mean = 0.0f;
    for (std::size_t f = 0; f < K; ++f) {
      float const* src = frame_ptr(in, f);
      float sum = 0.0f;
      for (std::size_t c = 0; c < in.channels; ++c) {
        sum += src[c];
      }
      float mean = sum / static_cast<float>(in.channels);
      last_mean = mean;
      if (mean < hlo || mean > hhi) {
        height_ok = false;
      }
      for (std::size_t c = 10; c <= 12 && c < in.channels; ++c) {
        if (mean - src[c] >= thr) {
          dent = true;
        }
      }
    }
    set_out("height_ok", height_ok);
    set_out("dent", dent);
    set_out("last_mean", last_mean);
    suppress();
  }
};

REGISTER_NODE(LqDentRules);

}  // namespace node_engine::nodes::line_quality
