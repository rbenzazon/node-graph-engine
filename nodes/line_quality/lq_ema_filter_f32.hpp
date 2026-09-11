#pragma once

#include "lq_types.hpp"
#include "node_engine/factory.hpp"
#include "node_engine/node.hpp"

#include <algorithm>
#include <vector>

namespace node_engine::nodes::line_quality {

class LqEmaFilterF32 : public NodeBase<LqEmaFilterF32> {
 public:
  static constexpr Meta meta{.type_id = "lq_ema_filter_f32", .role = Role::Transformer};

  static void ports(Ports& p) {
    p.in<ChannelChunkF32>("in");
    p.in<float>("alpha").literal(1.0f);  // 1.0 = pass-through (spike demo clarity)
    p.out<ChannelChunkF32>("out");
  }

  void reset_state() {
    state_.assign(kChannels, 0.0f);
    primed_ = false;
  }

  void compute(Slice) override {
    ChannelChunkF32 in = get<ChannelChunkF32>("in");
    float alpha = std::clamp(get<float>("alpha"), 0.0f, 1.0f);
    std::size_t ch = in.channels ? in.channels : kChannels;
    if (state_.size() != ch) {
      state_.assign(ch, 0.0f);
      primed_ = false;
    }

    ChannelChunkF32 out;
    out.signal_id = in.signal_id;
    out.channels = static_cast<std::uint32_t>(ch);
    out.t_s = in.t_s;
    out.samples.resize(in.samples.size());

    std::size_t K = num_frames(in);
    for (std::size_t f = 0; f < K; ++f) {
      float const* src = frame_ptr(in, f);
      float* dst = frame_ptr(out, f);
      for (std::size_t c = 0; c < ch; ++c) {
        if (!primed_) {
          state_[c] = src[c];
        } else {
          state_[c] = alpha * src[c] + (1.0f - alpha) * state_[c];
        }
        dst[c] = state_[c];
      }
      primed_ = true;
    }
    set_out("out", std::move(out));
    suppress();
  }

 private:
  std::vector<float> state_;
  bool primed_ = false;
};

REGISTER_NODE(LqEmaFilterF32);

}  // namespace node_engine::nodes::line_quality
