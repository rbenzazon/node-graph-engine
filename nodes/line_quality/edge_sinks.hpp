#pragma once

#include "node_engine/factory.hpp"
#include "node_engine/node.hpp"

#include <cstddef>
#include <cstdint>

namespace node_engine::nodes::line_quality {

// Fake PLC coil observer.
class EdgePlcGoNoGo : public NodeBase<EdgePlcGoNoGo> {
 public:
  static constexpr Meta meta{.type_id = "lq_edge_plc", .role = Role::Consumer};

  static void ports(Ports& p) {
    p.in<bool>("go").literal(false);
    p.in<bool>("nogo_latch").literal(false);
  }

  void compute(Slice) override {
    go_ = get<bool>("go");
    nogo_ = get<bool>("nogo_latch");
    ++emits_;
    if (go_) {
      ++go_true_count_;
    }
    if (nogo_) {
      ++nogo_true_count_;
    }
    suppress();
  }

  std::uint64_t emits() const { return emits_; }
  std::uint64_t go_true_count() const { return go_true_count_; }
  std::uint64_t nogo_true_count() const { return nogo_true_count_; }
  bool last_go() const { return go_; }
  bool last_nogo() const { return nogo_; }

  void reset_counts() {
    emits_ = 0;
    go_true_count_ = 0;
    nogo_true_count_ = 0;
  }

 private:
  bool go_ = false;
  bool nogo_ = false;
  std::uint64_t emits_ = 0;
  std::uint64_t go_true_count_ = 0;
  std::uint64_t nogo_true_count_ = 0;
};

REGISTER_NODE(EdgePlcGoNoGo);

// Fake MES event counter on rising nogo / fault.
class EdgeMesEvent : public NodeBase<EdgeMesEvent> {
 public:
  static constexpr Meta meta{.type_id = "lq_edge_mes", .role = Role::Consumer};

  static void ports(Ports& p) {
    p.in<bool>("nogo_latch").literal(false);
    p.in<double>("fault_code").literal(0.0);
    p.in<double>("grade").literal(0.0);
  }

  void compute(Slice) override {
    bool nogo = get<bool>("nogo_latch");
    double fault = get<double>("fault_code");
    ++emits_;
    if (nogo && !prev_nogo_) {
      ++events_;
      last_fault_ = fault;
    }
    prev_nogo_ = nogo;
    suppress();
  }

  std::uint64_t events() const { return events_; }
  std::uint64_t emits() const { return emits_; }
  double last_fault() const { return last_fault_; }

  void reset_counts() {
    events_ = 0;
    emits_ = 0;
    prev_nogo_ = false;
  }

 private:
  bool prev_nogo_ = false;
  std::uint64_t events_ = 0;
  std::uint64_t emits_ = 0;
  double last_fault_ = 0.0;
};

REGISTER_NODE(EdgeMesEvent);

// Fake HMI bitfield sink.
class EdgeHmiFlags : public NodeBase<EdgeHmiFlags> {
 public:
  static constexpr Meta meta{.type_id = "lq_edge_hmi", .role = Role::Consumer};

  static void ports(Ports& p) {
    p.in<bool>("go").literal(false);
    p.in<bool>("spike").literal(false);
    p.in<bool>("dent").literal(false);
    p.in<bool>("line_ok").literal(false);
  }

  void compute(Slice) override {
    std::uint32_t bits = 0;
    if (get<bool>("go")) {
      bits |= 1u;
    }
    if (get<bool>("spike")) {
      bits |= 2u;
    }
    if (get<bool>("dent")) {
      bits |= 4u;
    }
    if (get<bool>("line_ok")) {
      bits |= 8u;
    }
    last_bits_ = bits;
    ++emits_;
    suppress();
  }

  std::uint32_t last_bits() const { return last_bits_; }
  std::uint64_t emits() const { return emits_; }

 private:
  std::uint32_t last_bits_ = 0;
  std::uint64_t emits_ = 0;
};

REGISTER_NODE(EdgeHmiFlags);

// Optional timer root: carries t_now only (seed for PLC poll path).
class TimerEdge : public NodeBase<TimerEdge> {
 public:
  static constexpr Meta meta{.type_id = "lq_timer_edge", .role = Role::Producer};

  static void ports(Ports& p) {
    p.in<double>("t_now").literal(0.0);
    p.out<double>("t_now");
  }

  void compute(Slice) override {
    set_out("t_now", get<double>("t_now"));
    suppress();
  }
};

REGISTER_NODE(TimerEdge);

}  // namespace node_engine::nodes::line_quality
