#pragma once

#include "node_engine/factory.hpp"
#include "node_engine/node.hpp"

#include <cstddef>
#include <vector>

namespace node_engine::nodes {

// Emits a FloatBuffer of sequential samples [0..count).
class SequenceProducer : public NodeBase<SequenceProducer> {
 public:
  static constexpr Meta meta{.type_id = "sequence_producer", .role = Role::Producer};

  static void ports(Ports& p) {
    p.in<double>("count").literal(8.0);
    p.out_buffer("out");
  }

  void compute(Slice) override {
    std::size_t n = static_cast<std::size_t>(get<double>("count"));
    std::vector<double> out;
    out.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
      out.push_back(static_cast<double>(i));
    }
    set_out("out", std::move(out));
    suppress();
  }
};

REGISTER_NODE(SequenceProducer);

// Emits one scalar sample each time it is dirty (for coalescer demos).
class ScalarProducer : public NodeBase<ScalarProducer> {
 public:
  static constexpr Meta meta{.type_id = "scalar_producer", .role = Role::Producer};

  static void ports(Ports& p) {
    p.in<double>("value").literal(0.0);
    p.out<double>("out");
  }

  void compute(Slice) override {
    set_out("out", get<double>("value"));
    suppress();
  }
};

REGISTER_NODE(ScalarProducer);

// Stores last received buffer and exposes it for tests/demos.
class BufferConsumer : public NodeBase<BufferConsumer> {
 public:
  static constexpr Meta meta{.type_id = "buffer_consumer", .role = Role::Consumer};

  static void ports(Ports& p) { p.in_buffer("in"); }

  void compute(Slice) override {
    last_ = buffer_in("in");
    suppress();
  }

  std::vector<double> const& last() const { return last_; }

 private:
  std::vector<double> last_;
};

REGISTER_NODE(BufferConsumer);

// Stores last scalar.
class ScalarConsumer : public NodeBase<ScalarConsumer> {
 public:
  static constexpr Meta meta{.type_id = "scalar_consumer", .role = Role::Consumer};

  static void ports(Ports& p) { p.in<double>("in"); }

  void compute(Slice) override {
    last_ = get<double>("in");
    has_ = true;
    suppress();
  }

  double last() const { return last_; }
  bool has_value() const { return has_; }

 private:
  double last_ = 0.0;
  bool has_ = false;
};

REGISTER_NODE(ScalarConsumer);

}  // namespace node_engine::nodes
