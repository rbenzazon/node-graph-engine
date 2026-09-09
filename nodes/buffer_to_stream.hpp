#pragma once

#include "node_engine/factory.hpp"
#include "node_engine/node.hpp"

#include <cstddef>
#include <vector>

namespace node_engine::nodes {

// Emits one scalar per sub-tick from an input FloatBuffer; stays dirty until drained.
class BufferToStream : public NodeBase<BufferToStream> {
 public:
  static constexpr Meta meta{.type_id = "buffer_to_stream", .role = Role::Transformer};

  static void ports(Ports& p) {
    p.in_buffer("chunk");
    p.out<double>("sample");
    p.out<bool>("active");
  }

  void compute(Slice) override {
    // Reload queue when a new non-empty buffer arrives and local queue is empty.
    if (queue_.empty()) {
      auto const& buf = buffer_in("chunk");
      if (!buf.empty()) {
        queue_ = buf;
        index_ = 0;
      }
    }
    if (index_ >= queue_.size()) {
      // Idle: no sample this compute. Clear active so gated sinks ignore stale values.
      set_out("active", false);
      queue_.clear();
      index_ = 0;
      suppress();
      return;
    }
    // Emit one sample. active=true for every produced sample (including the last)
    // so gated consumers can record this value; a later idle compute clears active.
    set_out("sample", queue_[index_]);
    set_out("active", true);
    ++index_;
    if (index_ < queue_.size()) {
      emit();  // request another sub-tick
    } else {
      queue_.clear();
      index_ = 0;
      suppress();
    }
  }

 private:
  std::vector<double> queue_;
  std::size_t index_ = 0;
};

REGISTER_NODE(BufferToStream);

}  // namespace node_engine::nodes
