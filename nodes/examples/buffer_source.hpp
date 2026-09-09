#pragma once

#include "node_engine/factory.hpp"
#include "node_engine/node.hpp"

#include <cstddef>
#include <utility>
#include <vector>

namespace node_engine::nodes {

// Producer that emits a prebuilt FloatBuffer (set once, tick many times).
// Used by bandwidth benches so generation cost is outside the timed path.
class BufferSource : public NodeBase<BufferSource> {
 public:
  static constexpr Meta meta{.type_id = "buffer_source", .role = Role::Producer};

  static void ports(Ports& p) { p.out_buffer("out"); }

  void set_data(std::vector<double> data) { data_ = std::move(data); }
  std::vector<double> const& data() const { return data_; }

  void compute(Slice) override {
    set_out("out", data_);
    suppress();
  }

 private:
  std::vector<double> data_;
};

REGISTER_NODE(BufferSource);

}  // namespace node_engine::nodes
