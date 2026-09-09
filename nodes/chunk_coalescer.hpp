#pragma once

#include "node_engine/factory.hpp"
#include "node_engine/node.hpp"

#include <cstddef>
#include <vector>

namespace node_engine::nodes {

// Accumulates scalar samples into a fixed-size FloatBuffer chunk.
class FixedChunkCoalescer : public NodeBase<FixedChunkCoalescer> {
 public:
  static constexpr Meta meta{.type_id = "fixed_chunk_coalescer", .role = Role::Transformer};

  static void ports(Ports& p) {
    p.in<double>("sample");
    p.in<double>("chunk_size").literal(64.0);
    p.out_buffer("chunk");
    p.out<bool>("ready").literal(false);
  }

  void compute(Slice) override {
    std::size_t need = static_cast<std::size_t>(get<double>("chunk_size"));
    if (need == 0) {
      need = 1;
    }
    pending_.push_back(get<double>("sample"));
    if (pending_.size() < need) {
      // Stable empty buffer so downstream buffer_in never sees monostate mid-fill.
      set_out("chunk", std::vector<double>{});
      set_out("ready", false);
      // dirty is driven by the next upstream sample; stay quiet until then.
      suppress();
      return;
    }
    std::vector<double> out;
    out.reserve(need);
    for (std::size_t i = 0; i < need; ++i) {
      out.push_back(pending_[i]);
    }
    pending_.erase(pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(need));
    set_out("chunk", std::move(out));
    set_out("ready", true);
    suppress();
  }

 private:
  std::vector<double> pending_;
};

REGISTER_NODE(FixedChunkCoalescer);

}  // namespace node_engine::nodes
