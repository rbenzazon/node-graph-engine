#pragma once

#include "frame_util.hpp"
#include "node_engine/factory.hpp"
#include "node_engine/node.hpp"

#include <cstddef>
#include <vector>

namespace node_engine::nodes::line_quality {

// Accumulates W frames from variable-K emits into a window buffer (24*W).
// Counts frames, not emits — one K=W emit can complete a window immediately.
class FrameCoalescer : public NodeBase<FrameCoalescer> {
 public:
  static constexpr Meta meta{.type_id = "lq_frame_coalescer", .role = Role::Transformer};

  static void ports(Ports& p) {
    p.in_buffer("in");
    p.in<double>("window_frames").literal(20.0);
    p.out_buffer("window");
    p.out<bool>("ready").literal(false);
  }

  void compute(Slice) override {
    auto const& in = buffer_in("in");
    std::size_t K = num_frames(in);
    std::size_t need = static_cast<std::size_t>(get<double>("window_frames"));
    if (need == 0) {
      need = 1;
    }

    for (std::size_t f = 0; f < K; ++f) {
      double const* src = frame_ptr(in, f);
      pending_.insert(pending_.end(), src, src + kChannels);
    }

    std::size_t have = pending_.size() / kChannels;
    if (have < need) {
      set_out("window", std::vector<double>{});
      set_out("ready", false);
      suppress();
      return;
    }

    std::size_t take = need * kChannels;
    std::vector<double> win(pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(take));
    pending_.erase(pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(take));
    set_out("window", std::move(win));
    set_out("ready", true);
    suppress();
  }

  void reset() { pending_.clear(); }

  std::size_t pending_frames() const { return pending_.size() / kChannels; }

 private:
  std::vector<double> pending_;
};

REGISTER_NODE(FrameCoalescer);

}  // namespace node_engine::nodes::line_quality
