#pragma once

#include "frame_util.hpp"
#include "node_engine/factory.hpp"
#include "node_engine/node.hpp"

#include <cstddef>
#include <utility>
#include <vector>

namespace node_engine::nodes::line_quality {

// Input edge / producer root: harness arms one emit (K frames), then triggers this node id.
// Sample (K=1) and chunk (K>1) share the same out buffer contract: size == 24*K.
class FrameEdgeSource : public NodeBase<FrameEdgeSource> {
 public:
  static constexpr Meta meta{.type_id = "lq_frame_edge_source", .role = Role::Producer};

  static void ports(Ports& p) {
    p.out_buffer("out");
    p.out<double>("frames").literal(0.0);
    p.out<double>("t_stamp").literal(0.0);
  }

  void set_emit(std::vector<double> data, double t_first_frame, std::size_t frames) {
    if (data.size() != frames * kChannels) {
      throw std::runtime_error("FrameEdgeSource::set_emit size mismatch");
    }
    data_ = std::move(data);
    t0_ = t_first_frame;
    frames_ = frames;
  }

  std::size_t last_frames() const { return frames_; }
  double last_t0() const { return t0_; }

  void compute(Slice) override {
    set_out("out", data_);
    set_out("frames", static_cast<double>(frames_));
    set_out("t_stamp", t0_);
    suppress();
  }

 private:
  std::vector<double> data_;
  double t0_ = 0.0;
  std::size_t frames_ = 0;
};

REGISTER_NODE(FrameEdgeSource);

}  // namespace node_engine::nodes::line_quality
