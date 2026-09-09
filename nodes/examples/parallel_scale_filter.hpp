#pragma once

#include "node_engine/factory.hpp"
#include "node_engine/node.hpp"

#include <algorithm>
#include <cstddef>
#include <vector>

namespace node_engine::nodes {

// Bench-oriented scale filter: runtime ParallelMode, grain, and optional
// arithmetic intensity (burn_iters) so timings are not pure memory-bound.
class ParallelScaleFilter : public NodeBase<ParallelScaleFilter> {
 public:
  static constexpr Meta meta{.type_id = "parallel_scale_filter", .role = Role::Transformer};

  // Default static hint; runtime overrides via parallel_hint().
  static constexpr ParallelHint parallel{.mode = ParallelMode::OnCollection, .grain_size = 1024};

  static void ports(Ports& p) {
    p.in_buffer("in");
    p.in<double>("gain").literal(1.0);
    p.out_buffer("out");
  }

  ParallelMode mode = ParallelMode::OnCollection;
  std::size_t grain_size = 1024;   // 0 → scheduler default_grain_
  std::size_t burn_iters = 1;      // extra mul-adds per element

  ParallelHint parallel_hint() const override {
    return ParallelHint{mode, grain_size};
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
    if (out.size() != in.size()) {
      out.assign(in.size(), 0.0);
    }
    if (slice.begin >= slice.end) {
      return;
    }

    std::size_t begin = slice.begin;
    std::size_t end = std::min(slice.end, in.size());
    std::size_t burns = std::max<std::size_t>(1, burn_iters);

    for (std::size_t i = begin; i < end; ++i) {
      double x = in[i];
      // Intentionally simple dependent chain so the compiler cannot drop work.
      for (std::size_t b = 0; b < burns; ++b) {
        x = x * gain + 0.0000001;
      }
      out[i] = x;
    }
    suppress();
  }
};

REGISTER_NODE(ParallelScaleFilter);

}  // namespace node_engine::nodes
