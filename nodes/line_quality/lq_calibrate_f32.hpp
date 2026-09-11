#pragma once

#include "lq_types.hpp"
#include "node_engine/factory.hpp"
#include "node_engine/node.hpp"

namespace node_engine::nodes::line_quality {

class LqCalibrateF32 : public NodeBase<LqCalibrateF32> {
 public:
  static constexpr Meta meta{.type_id = "lq_calibrate_f32", .role = Role::Transformer};

  static void ports(Ports& p) {
    p.in<ChannelChunkF32>("in");
    p.in<float>("scale").literal(1.0f);
    p.in<float>("offset").literal(0.0f);
    p.out<ChannelChunkF32>("out");
  }

  void compute(Slice) override {
    ChannelChunkF32 in = get<ChannelChunkF32>("in");
    float scale = get<float>("scale");
    float offset = get<float>("offset");
    ChannelChunkF32 out;
    out.signal_id = in.signal_id;
    out.channels = in.channels;
    out.t_s = in.t_s;
    out.samples.resize(in.samples.size());
    for (std::size_t i = 0; i < in.samples.size(); ++i) {
      out.samples[i] = in.samples[i] * scale + offset;
    }
    set_out("out", std::move(out));
    suppress();
  }
};

REGISTER_NODE(LqCalibrateF32);

}  // namespace node_engine::nodes::line_quality
