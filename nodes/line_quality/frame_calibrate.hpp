#pragma once

#include "frame_util.hpp"
#include "node_engine/factory.hpp"
#include "node_engine/node.hpp"

#include <algorithm>
#include <cstddef>
#include <vector>

namespace node_engine::nodes::line_quality {

// Per-channel affine: out = in * scale + offset. Same body for K=1 and K>1.
class FrameCalibrate : public NodeBase<FrameCalibrate> {
 public:
  static constexpr Meta meta{.type_id = "lq_frame_calibrate", .role = Role::Transformer};
  static constexpr ParallelHint parallel{.mode = ParallelMode::OnCollection, .grain_size = 256};

  static void ports(Ports& p) {
    p.in_buffer("in");
    p.in<double>("scale").literal(1.0);
    p.in<double>("offset").literal(0.0);
    p.out_buffer("out");
  }

  std::size_t parallel_length() const override {
    Pin const* pin = find_input("in");
    if (!pin) {
      return 0;
    }
    Value const& v = read_pin(*pin);
    if (!std::holds_alternative<std::vector<double>>(v)) {
      return 0;
    }
    return std::get<std::vector<double>>(v).size();
  }

  void compute(Slice slice) override {
    auto const& in = buffer_in("in");
    double scale = get<double>("scale");
    double offset = get<double>("offset");
    auto& out = buffer_out("out");
    if (out.size() != in.size()) {
      out.assign(in.size(), 0.0);
    }
    if (slice.begin >= slice.end) {
      return;
    }
    std::size_t end = std::min(slice.end, in.size());
    for (std::size_t i = slice.begin; i < end; ++i) {
      out[i] = in[i] * scale + offset;
    }
    suppress();
  }
};

REGISTER_NODE(FrameCalibrate);

}  // namespace node_engine::nodes::line_quality
