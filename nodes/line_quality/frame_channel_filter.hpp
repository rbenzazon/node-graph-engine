#pragma once

#include "frame_util.hpp"
#include "node_engine/factory.hpp"
#include "node_engine/node.hpp"

#include <algorithm>
#include <cstddef>
#include <vector>

namespace node_engine::nodes::line_quality {

// Causal per-channel EMA across frames in the emit, carrying state across emits.
// Processes frames serially so K=1 and chunked K match (no parallel over time).
class FrameChannelFilter : public NodeBase<FrameChannelFilter> {
 public:
  static constexpr Meta meta{.type_id = "lq_frame_channel_filter", .role = Role::Transformer};

  static void ports(Ports& p) {
    p.in_buffer("in");
    p.in<double>("alpha").literal(0.4);
    p.out_buffer("out");
  }

  void compute(Slice) override {
    auto const& in = buffer_in("in");
    std::size_t K = num_frames(in);
    double alpha = get<double>("alpha");
    alpha = std::clamp(alpha, 0.0, 1.0);
    auto& out = buffer_out("out");
    out.resize(in.size());

    if (state_.size() != kChannels) {
      state_.assign(kChannels, 0.0);
      primed_ = false;
    }

    for (std::size_t f = 0; f < K; ++f) {
      double const* src = frame_ptr(in, f);
      double* dst = frame_ptr(out, f);
      for (std::size_t c = 0; c < kChannels; ++c) {
        if (!primed_) {
          state_[c] = src[c];
        } else {
          state_[c] = alpha * src[c] + (1.0 - alpha) * state_[c];
        }
        dst[c] = state_[c];
      }
      primed_ = true;
    }
    suppress();
  }

  void reset_state() {
    state_.clear();
    primed_ = false;
  }

 private:
  std::vector<double> state_;
  bool primed_ = false;
};

REGISTER_NODE(FrameChannelFilter);

}  // namespace node_engine::nodes::line_quality
