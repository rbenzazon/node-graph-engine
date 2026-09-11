#pragma once

#include "node_engine/factory.hpp"
#include "node_engine/node.hpp"

namespace node_engine::nodes::line_quality {

// Late multi-input join: freshness + branch OK flags. No sensor mux.
class LqFusionSimple : public NodeBase<LqFusionSimple> {
 public:
  static constexpr Meta meta{.type_id = "lq_fusion_simple", .role = Role::Transformer};

  static void ports(Ports& p) {
    p.in<double>("t_now").literal(0.0);
    p.in<double>("t_a").literal(0.0);
    p.in<double>("t_b").literal(0.0);
    p.in<double>("t_c").literal(0.0);
    p.in<bool>("thick_ok").literal(false);
    p.in<bool>("dist_ok").literal(false);
    p.in<bool>("line_running").literal(false);
    p.in<double>("stale_a").literal(0.001);
    p.in<double>("stale_b").literal(0.003);
    p.in<double>("stale_c").literal(0.015);
    p.out<bool>("go").literal(false);
    p.out<bool>("nogo_latch").literal(false);
    p.out<bool>("fresh_ok").literal(false);
  }

  void reset_state() { nogo_latched_ = false; }

  bool nogo_latched() const { return nogo_latched_; }
  bool last_go() const { return last_go_; }

  void compute(Slice) override {
    double t_now = get<double>("t_now");
    double t_a = get<double>("t_a");
    double t_b = get<double>("t_b");
    double t_c = get<double>("t_c");
    bool thick_ok = get<bool>("thick_ok");
    bool dist_ok = get<bool>("dist_ok");
    bool line_running = get<bool>("line_running");
    double stale_a = get<double>("stale_a");
    double stale_b = get<double>("stale_b");
    double stale_c = get<double>("stale_c");

    bool fresh_a = (t_now - t_a) <= stale_a;
    bool fresh_b = (t_now - t_b) <= stale_b;
    bool fresh_c = (t_now - t_c) <= stale_c;
    bool fresh_ok = fresh_a && fresh_b && fresh_c;

    bool go = thick_ok && dist_ok && line_running && fresh_ok;
    if (!go) {
      nogo_latched_ = true;
    }
    last_go_ = go;
    set_out("go", go);
    set_out("nogo_latch", nogo_latched_);
    set_out("fresh_ok", fresh_ok);
    suppress();
  }

 private:
  bool nogo_latched_ = false;
  bool last_go_ = false;
};

REGISTER_NODE(LqFusionSimple);

}  // namespace node_engine::nodes::line_quality
