#pragma once

#include "node_engine/factory.hpp"
#include "node_engine/node.hpp"

#include <algorithm>
#include <cstddef>
#include <vector>

namespace node_engine::nodes {

// Parallel OnCollection filter: out[i] = in[i] * gain
class ScaleFilter : public NodeBase<ScaleFilter> {
 public:
  static constexpr Meta meta{.type_id = "scale_filter", .role = Role::Transformer};
  static constexpr ParallelHint parallel{.mode = ParallelMode::OnCollection, .grain_size = 256};

  static void ports(Ports& p) {
    p.in_buffer("in");
    p.in<double>("gain").literal(1.0);
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
    double gain = get<double>("gain");
    auto& out = buffer_out("out");
    // Scheduler may call with [0,0) as a single-threaded prepare before parallel splits.
    if (out.size() != in.size()) {
      out.assign(in.size(), 0.0);
    }
    if (slice.begin >= slice.end) {
      return;
    }
    std::size_t begin = slice.begin;
    std::size_t end = std::min(slice.end, in.size());
    for (std::size_t i = begin; i < end; ++i) {
      out[i] = in[i] * gain;
    }
    suppress();
  }
};

REGISTER_NODE(ScaleFilter);

}  // namespace node_engine::nodes
