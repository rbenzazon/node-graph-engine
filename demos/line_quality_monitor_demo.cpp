// line_quality_monitor_demo — typed multi-root line quality (realistic payloads)
//
// Topology (no sensor mux):
//   edge_thick (I16) -[conv]-> cal -> ema -> stats -mean-> cmp_lo & cmp_hi -> and -> thick_ok
//                                              features -----------------> route.data
//                                              thick_ok -----------------> route.sel
//                                              route.b -> latch_fault
//   edge_dist  (I16) -[conv]-> cal -> dent_rules -> latch_b
//   edge_enc   (raw) -[conv]-> motion -> latch_c
//   latches + thick_ok -> fusion -> plc
//
// CompareScalar: op and operand are SEPARATE pins; const via unwired literals.

#include "node_engine/engine.hpp"
#include "node_engine/graph.hpp"

#include "line_quality/and_bool.hpp"
#include "line_quality/compare_scalar.hpp"
#include "line_quality/edge_sinks.hpp"
#include "line_quality/lq_calibrate_f32.hpp"
#include "line_quality/lq_dent_rules.hpp"
#include "line_quality/lq_ema_filter_f32.hpp"
#include "line_quality/lq_enc_motion_pose.hpp"
#include "line_quality/lq_fusion_simple.hpp"
#include "line_quality/lq_simple_latch.hpp"
#include "line_quality/lq_stats_f32.hpp"
#include "line_quality/lq_types.hpp"
#include "line_quality/route_features.hpp"
#include "line_quality/typed_edge_source.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace node_engine;
using namespace node_engine::nodes::line_quality;

int fail(char const* msg) {
  std::cerr << "FAIL: " << msg << '\n';
  return 1;
}

struct ThickRing {
  std::vector<std::int16_t> data;  // N * 24 milli-units
  std::size_t frames = 0;
  std::int16_t const* frame(std::size_t i) const {
    return data.data() + (i % frames) * kChannels;
  }
};

struct DistRing {
  std::vector<std::int16_t> data;
  std::size_t frames = 0;
  std::int16_t const* frame(std::size_t i) const {
    return data.data() + (i % frames) * kChannels;
  }
};

struct EncRing {
  std::vector<EncoderRawI16> frames;
};

ThickRing make_thickness_ring(std::size_t n, std::size_t spike_at) {
  ThickRing r;
  r.frames = n;
  r.data.resize(n * kChannels);
  for (std::size_t i = 0; i < n; ++i) {
    double t = static_cast<double>(i) * 0.001;
    double base = 2.0 + 0.02 * std::sin(t * 6.283185307179586);
    for (std::size_t c = 0; c < kChannels; ++c) {
      double taper = 0.01 * (static_cast<double>(c) - 11.5) / 11.5;
      double v = base + taper;
      if (i == spike_at) {
        v += 0.5;
      }
      r.data[i * kChannels + c] = static_cast<std::int16_t>(std::lround(v * 1000.0));
    }
  }
  return r;
}

DistRing make_distance_ring(std::size_t n, std::size_t dent_at) {
  DistRing r;
  r.frames = n;
  r.data.resize(n * kChannels);
  for (std::size_t i = 0; i < n; ++i) {
    for (std::size_t c = 0; c < kChannels; ++c) {
      double x = (static_cast<double>(c) - 11.5) / 11.5;
      double bow = 0.05 * x * x;
      double v = bow;
      if (i == dent_at && c >= 10 && c <= 12) {
        v -= 0.55;
      }
      r.data[i * kChannels + c] = static_cast<std::int16_t>(std::lround(v * 1000.0));
    }
  }
  return r;
}

// vel_counts are mm/s * 100. Running 10 mm/s => 1000 counts.
EncRing make_encoder_ring(std::size_t n, std::size_t stop_begin, std::size_t stop_len,
                          float v_run_mm_s) {
  EncRing r;
  r.frames.resize(n);
  double pos_mm = 0.0;
  double dt = 0.0005;
  std::int16_t v_run = static_cast<std::int16_t>(std::lround(v_run_mm_s * 100.0f));
  for (std::size_t i = 0; i < n; ++i) {
    bool stopped = i >= stop_begin && i < stop_begin + stop_len;
    std::int16_t vel = stopped ? static_cast<std::int16_t>(0) : v_run;
    double vel_mm = static_cast<double>(vel) / 100.0;
    pos_mm += vel_mm * dt;
    EncoderRawI16 e;
    e.signal_id = kSigEncoder;
    e.x_counts = static_cast<std::int16_t>(std::lround(pos_mm * 100.0));
    e.y_counts = 0;
    e.vel_counts = vel;
    e.status = 1;
    r.frames[i] = e;
  }
  return r;
}

struct EmitCursor {
  std::size_t index = 0;
  double t_frame = 0.0;
  double period = 50e-6;
  std::size_t K = 1;
};

ChannelChunkI16 pull_thick(ThickRing const& ring, EmitCursor& cur) {
  ChannelChunkI16 c;
  c.signal_id = kSigThickness;
  c.channels = static_cast<std::uint32_t>(kChannels);
  c.t_s = cur.t_frame;
  c.samples.resize(cur.K * kChannels);
  for (std::size_t f = 0; f < cur.K; ++f) {
    auto const* src = ring.frame(cur.index + f);
    std::copy(src, src + kChannels, c.samples.begin() + static_cast<std::ptrdiff_t>(f * kChannels));
  }
  cur.index += cur.K;
  return c;
}

ChannelChunkI16 pull_dist(DistRing const& ring, EmitCursor& cur) {
  ChannelChunkI16 c;
  c.signal_id = kSigDistance;
  c.channels = static_cast<std::uint32_t>(kChannels);
  c.t_s = cur.t_frame;
  c.samples.resize(cur.K * kChannels);
  for (std::size_t f = 0; f < cur.K; ++f) {
    auto const* src = ring.frame(cur.index + f);
    std::copy(src, src + kChannels, c.samples.begin() + static_cast<std::ptrdiff_t>(f * kChannels));
  }
  cur.index += cur.K;
  return c;
}

EncoderRawI16 pull_enc(EncRing const& ring, EmitCursor& cur) {
  EncoderRawI16 e = ring.frames[cur.index % ring.frames.size()];
  e.t_s = cur.t_frame;
  cur.index += 1;
  return e;
}

struct Handles {
  ThickEdgeSourceI16* edge_thick = nullptr;
  DistEdgeSourceI16* edge_dist = nullptr;
  EncEdgeSourceRaw* edge_enc = nullptr;
  LqEmaFilterF32* ema_a = nullptr;
  CompareScalar* cmp_lo = nullptr;
  CompareScalar* cmp_hi = nullptr;
  AndBool* thick_and = nullptr;
  RouteFeatures* route = nullptr;
  ThickPathLatch* latch_ok = nullptr;
  FaultFeatureLatch* latch_fault = nullptr;
  DistPathLatch* latch_b = nullptr;
  MotionPathLatch* latch_c = nullptr;
  LqFusionSimple* fusion = nullptr;
  EdgePlcGoNoGo* plc = nullptr;
  Node* fusion_node = nullptr;
};

Graph build_graph() {
  Graph g;
  g.add_node("edge_thick", std::make_unique<ThickEdgeSourceI16>());
  g.add_node("edge_dist", std::make_unique<DistEdgeSourceI16>());
  g.add_node("edge_enc", std::make_unique<EncEdgeSourceRaw>());

  g.add_node("cal_a", std::make_unique<LqCalibrateF32>());
  g.add_node("ema_a", std::make_unique<LqEmaFilterF32>());
  g.add_node("stats_a", std::make_unique<LqStatsF32>());
  g.add_node("cmp_lo", std::make_unique<CompareScalar>());
  g.add_node("cmp_hi", std::make_unique<CompareScalar>());
  g.add_node("thick_and", std::make_unique<AndBool>());
  g.add_node("route", std::make_unique<RouteFeatures>());
  g.add_node("latch_ok", std::make_unique<ThickPathLatch>());
  g.add_node("latch_fault", std::make_unique<FaultFeatureLatch>());

  g.add_node("cal_b", std::make_unique<LqCalibrateF32>());
  g.add_node("dent_b", std::make_unique<LqDentRules>());
  g.add_node("latch_b", std::make_unique<DistPathLatch>());

  g.add_node("motion_c", std::make_unique<LqEncMotionPose>());
  g.add_node("latch_c", std::make_unique<MotionPathLatch>());

  g.add_node("fusion", std::make_unique<LqFusionSimple>());
  g.add_node("edge_plc", std::make_unique<EdgePlcGoNoGo>());

  // Branch A — I16 edge -> (converter on wire) F32 cal/ema/stats
  g.connect("edge_thick", "out", "cal_a", "in");
  g.connect("cal_a", "out", "ema_a", "in");
  g.connect("ema_a", "out", "stats_a", "in");
  g.connect("stats_a", "mean", "cmp_lo", "value");
  g.connect("stats_a", "mean", "cmp_hi", "value");
  g.connect("cmp_lo", "pass", "thick_and", "a");
  g.connect("cmp_hi", "pass", "thick_and", "b");
  g.connect("stats_a", "features", "route", "data");
  g.connect("thick_and", "pass", "route", "sel");
  g.connect("edge_thick", "t_stamp", "latch_ok", "t_stamp");
  g.connect("thick_and", "pass", "latch_ok", "ok");
  g.connect("stats_a", "mean", "latch_ok", "mean");
  g.connect("route", "b", "latch_fault", "in");

  // Branch B
  g.connect("edge_dist", "out", "cal_b", "in");
  g.connect("cal_b", "out", "dent_b", "in");
  g.connect("edge_dist", "t_stamp", "latch_b", "t_stamp");
  g.connect("dent_b", "height_ok", "latch_b", "height_ok");
  g.connect("dent_b", "dent", "latch_b", "dent");

  // Branch C — raw encoder -> pose converter on wire -> motion
  g.connect("edge_enc", "out", "motion_c", "in");
  g.connect("edge_enc", "t_stamp", "latch_c", "t_stamp");
  g.connect("motion_c", "line_running", "latch_c", "line_running");
  g.connect("motion_c", "pos_mm", "latch_c", "pos_mm");

  // Fusion late join
  g.connect("latch_ok", "t_stamp", "fusion", "t_a");
  g.connect("latch_ok", "ok", "fusion", "thick_ok");
  g.connect("latch_b", "t_stamp", "fusion", "t_b");
  g.connect("latch_b", "ok", "fusion", "dist_ok");
  g.connect("latch_c", "t_stamp", "fusion", "t_c");
  g.connect("latch_c", "line_running", "fusion", "line_running");
  g.connect("fusion", "go", "edge_plc", "go");
  g.connect("fusion", "nogo_latch", "edge_plc", "nogo_latch");

  return g;
}

bool bind_handles(FlatGraph& flat, Handles& h) {
  h.edge_thick = dynamic_cast<ThickEdgeSourceI16*>(flat.find_node("edge_thick"));
  h.edge_dist = dynamic_cast<DistEdgeSourceI16*>(flat.find_node("edge_dist"));
  h.edge_enc = dynamic_cast<EncEdgeSourceRaw*>(flat.find_node("edge_enc"));
  h.ema_a = dynamic_cast<LqEmaFilterF32*>(flat.find_node("ema_a"));
  h.cmp_lo = dynamic_cast<CompareScalar*>(flat.find_node("cmp_lo"));
  h.cmp_hi = dynamic_cast<CompareScalar*>(flat.find_node("cmp_hi"));
  h.thick_and = dynamic_cast<AndBool*>(flat.find_node("thick_and"));
  h.route = dynamic_cast<RouteFeatures*>(flat.find_node("route"));
  h.latch_ok = dynamic_cast<ThickPathLatch*>(flat.find_node("latch_ok"));
  h.latch_fault = dynamic_cast<FaultFeatureLatch*>(flat.find_node("latch_fault"));
  h.latch_b = dynamic_cast<DistPathLatch*>(flat.find_node("latch_b"));
  h.latch_c = dynamic_cast<MotionPathLatch*>(flat.find_node("latch_c"));
  h.fusion = dynamic_cast<LqFusionSimple*>(flat.find_node("fusion"));
  h.fusion_node = flat.find_node("fusion");
  h.plc = dynamic_cast<EdgePlcGoNoGo*>(flat.find_node("edge_plc"));
  return h.edge_thick && h.edge_dist && h.edge_enc && h.ema_a && h.cmp_lo && h.cmp_hi &&
         h.thick_and && h.route && h.latch_ok && h.latch_fault && h.latch_b && h.latch_c &&
         h.fusion && h.plc && h.fusion_node;
}

void set_policy_literals(FlatGraph& flat) {
  // Const params = unwired pin literals (each param is its own pin).
  auto* lo = flat.find_node("cmp_lo");
  lo->find_input("op")->literal = static_cast<std::int32_t>(kOpGE);
  lo->find_input("operand")->literal = 1.90f;

  auto* hi = flat.find_node("cmp_hi");
  hi->find_input("op")->literal = static_cast<std::int32_t>(kOpLE);
  hi->find_input("operand")->literal = 2.10f;

  flat.find_node("cal_a")->find_input("scale")->literal = 1.0f;
  flat.find_node("cal_a")->find_input("offset")->literal = 0.0f;
  flat.find_node("ema_a")->find_input("alpha")->literal = 1.0f;
  flat.find_node("cal_b")->find_input("scale")->literal = 1.0f;
  flat.find_node("dent_b")->find_input("dent_thr")->literal = 0.4f;
  flat.find_node("motion_c")->find_input("v_min")->literal = 0.5f;

  auto* fusion = flat.find_node("fusion");
  fusion->find_input("stale_a")->literal = 0.001;
  fusion->find_input("stale_b")->literal = 0.003;
  fusion->find_input("stale_c")->literal = 0.015;
}

void clear_dirty(FlatGraph& flat) {
  for (auto& n : flat.nodes) {
    if (n.node) {
      n.node->dirty = false;
    }
  }
}

void arm_thick(Engine& engine, FlatGraph& flat, Handles& h, ChannelChunkI16 chunk, double t_now) {
  h.edge_thick->set_emit(std::move(chunk));
  h.fusion_node->find_input("t_now")->literal = t_now;
  clear_dirty(flat);
  engine.triggers().push("edge_thick");
  if (!engine.poll_trigger_and_run(flat)) {
    throw std::runtime_error("poll failed edge_thick");
  }
}

void arm_dist(Engine& engine, FlatGraph& flat, Handles& h, ChannelChunkI16 chunk, double t_now) {
  h.edge_dist->set_emit(std::move(chunk));
  h.fusion_node->find_input("t_now")->literal = t_now;
  clear_dirty(flat);
  engine.triggers().push("edge_dist");
  if (!engine.poll_trigger_and_run(flat)) {
    throw std::runtime_error("poll failed edge_dist");
  }
}

void arm_enc(Engine& engine, FlatGraph& flat, Handles& h, EncoderRawI16 raw, double t_now) {
  h.edge_enc->set_emit(raw);
  h.fusion_node->find_input("t_now")->literal = t_now;
  clear_dirty(flat);
  engine.triggers().push("edge_enc");
  if (!engine.poll_trigger_and_run(flat)) {
    throw std::runtime_error("poll failed edge_enc");
  }
}

bool out_bool(Node& n, char const* pin) {
  auto* p = n.find_output(pin);
  if (!p || !std::holds_alternative<bool>(p->buffer)) {
    return false;
  }
  return std::get<bool>(p->buffer);
}

bool assert_multi_root(FlatGraph const& flat) {
  bool has_thick = false, has_dist = false, has_enc = false;
  for (auto const& n : flat.nodes) {
    if (n.id == "edge_thick") {
      has_thick = true;
    }
    if (n.id == "edge_dist") {
      has_dist = true;
    }
    if (n.id == "edge_enc") {
      has_enc = true;
    }
  }
  if (!has_thick || !has_dist || !has_enc) {
    return false;
  }
  for (auto const& e : flat.edges) {
    if (e.to_node == "edge_thick" || e.to_node == "edge_dist" || e.to_node == "edge_enc") {
      return false;
    }
  }
  return true;
}

struct RunResult {
  bool saw_spike = false;
  bool saw_dent = false;
  bool saw_go_true = false;
  bool saw_go_false_on_stop = false;
  bool saw_go_false_on_stale = false;
  bool saw_route_fault = false;
  std::size_t emits_a = 0;
};

RunResult run_scenario(Engine& engine, FlatGraph& flat, Handles& h) {
  constexpr std::size_t kN = 8000;
  constexpr std::size_t kSpike = 100;
  constexpr std::size_t kDent = 56;
  constexpr std::size_t kStop0 = 80;
  constexpr std::size_t kStopN = 40;

  ThickRing ring_a = make_thickness_ring(kN, kSpike);
  DistRing ring_b = make_distance_ring(kN, kDent);
  EncRing ring_c = make_encoder_ring(kN, kStop0, kStopN, 10.0f);

  EmitCursor ca{.index = 0, .t_frame = 0.0, .period = 50e-6, .K = 1};
  EmitCursor cb{.index = 0, .t_frame = 0.0, .period = 1.0 / 7000.0, .K = 1};
  EmitCursor cc{.index = 0, .t_frame = 0.0, .period = 1.0 / 2000.0, .K = 1};

  h.ema_a->reset_state();
  h.latch_fault->reset();
  h.fusion->reset_state();
  h.plc->reset_counts();

  RunResult rr;
  double t_end = 0.10;
  double t_now = 0.0;

  // Prime each root once.
  {
    auto ba = pull_thick(ring_a, ca);
    arm_thick(engine, flat, h, std::move(ba), 0.0);
    ca.t_frame = ca.period;
    auto bb = pull_dist(ring_b, cb);
    arm_dist(engine, flat, h, std::move(bb), 0.0);
    cb.t_frame = cb.period;
    auto bc = pull_enc(ring_c, cc);
    arm_enc(engine, flat, h, bc, 0.0);
    cc.t_frame = cc.period;
    ++rr.emits_a;
    if (h.plc->last_go()) {
      rr.saw_go_true = true;
    }
  }

  double next_a = ca.t_frame;
  double next_b = cb.t_frame;
  double next_c = cc.t_frame;

  auto step_a = [&]() {
    if (ca.index + ca.K > ring_a.frames) {
      next_a = 1e100;
      return;
    }
    double t0 = ca.t_frame;
    auto buf = pull_thick(ring_a, ca);
    ca.t_frame += ca.period;
    next_a = ca.t_frame;
    t_now = std::max(t_now, t0);
    arm_thick(engine, flat, h, std::move(buf), t_now);
    ++rr.emits_a;
    if (!out_bool(*flat.find_node("thick_and"), "pass") && ca.index > kSpike) {
      rr.saw_spike = true;
    }
    if (h.latch_fault->has_fault()) {
      rr.saw_route_fault = true;
    }
    if (h.plc->last_go()) {
      rr.saw_go_true = true;
    }
  };

  auto step_b = [&]() {
    if (cb.index + cb.K > ring_b.frames) {
      next_b = 1e100;
      return;
    }
    double t0 = cb.t_frame;
    auto buf = pull_dist(ring_b, cb);
    cb.t_frame += cb.period;
    next_b = cb.t_frame;
    t_now = std::max(t_now, t0);
    arm_dist(engine, flat, h, std::move(buf), t_now);
    if (h.latch_b->dent()) {
      rr.saw_dent = true;
    }
    if (h.plc->last_go()) {
      rr.saw_go_true = true;
    }
  };

  auto step_c = [&]() {
    if (cc.index >= ring_c.frames.size()) {
      next_c = 1e100;
      return;
    }
    double t0 = cc.t_frame;
    auto raw = pull_enc(ring_c, cc);
    cc.t_frame += cc.period;
    next_c = cc.t_frame;
    t_now = std::max(t_now, t0);
    arm_enc(engine, flat, h, raw, t_now);
    if (!h.latch_c->line_running() && !h.plc->last_go()) {
      rr.saw_go_false_on_stop = true;
    }
    if (h.plc->last_go()) {
      rr.saw_go_true = true;
    }
  };

  while (true) {
    double n = std::min({next_a, next_b, next_c});
    if (n > 1e50 || n > t_end) {
      break;
    }
    t_now = std::max(t_now, n);
    if (n == next_a) {
      step_a();
    } else if (n == next_b) {
      step_b();
    } else {
      step_c();
    }
  }

  // Stale-C: freeze encoder, keep A/B updating past stale_c.
  double t_c_last = h.latch_c->t_stamp();
  double stale_c = 0.015;
  for (int i = 0; i < 5; ++i) {
    if (ca.index + ca.K > ring_a.frames) {
      break;
    }
    auto buf = pull_thick(ring_a, ca);
    ca.t_frame += ca.period;
    t_now = t_c_last + stale_c + 0.005 + static_cast<double>(i) * 0.001;
    arm_thick(engine, flat, h, std::move(buf), t_now);
    ++rr.emits_a;
    if (cb.index + cb.K <= ring_b.frames) {
      auto bb = pull_dist(ring_b, cb);
      cb.t_frame += cb.period;
      arm_dist(engine, flat, h, std::move(bb), t_now);
    }
    if (!h.plc->last_go()) {
      rr.saw_go_false_on_stale = true;
    }
  }

  return rr;
}

}  // namespace

int main() {
  register_lq_demo_types();
  register_lq_demo_converters();

  Engine engine{4};
  std::cout << "line_quality_monitor_demo (typed)\n";
  std::cout << "payloads: ChannelChunkI16/F32, EncoderRawI16/PoseF32, FrameFeaturesF32\n";
  std::cout << "branching: CompareScalar (op|operand pins) + RouteFeatures\n";
  std::cout << "const demo: cmp_lo/hi op+operand as unwired literals\n\n";

  Graph g = build_graph();
  if (!g.find_node("edge_thick") || !g.find_node("edge_dist") || !g.find_node("edge_enc")) {
    return fail("missing edge roots");
  }
  if (g.find_node("sensor_mux") || g.find_node("edge_daq_all")) {
    return fail("unexpected choke-point node");
  }

  auto errs = validate_graph(g);
  if (!errs.empty()) {
    std::cerr << errs.front().message << '\n';
    return fail("validate_graph");
  }

  FlatGraph flat = engine.compile(g);
  Handles h;
  if (!bind_handles(flat, h)) {
    return fail("bind handles");
  }
  set_policy_literals(flat);

  if (!assert_multi_root(flat)) {
    return fail("multi-root broken");
  }
  std::cout << "multi-root ok; validate ok (I16->F32 and raw->pose converters on wires)\n";

  std::cout << "\n--- virtual-time scenario ---\n";
  RunResult r = run_scenario(engine, flat, h);
  std::cout << "emits_a=" << r.emits_a << " spike=" << r.saw_spike << " dent=" << r.saw_dent
            << " route_fault=" << r.saw_route_fault << " go_true=" << r.saw_go_true
            << " go_false_stop=" << r.saw_go_false_on_stop
            << " go_false_stale=" << r.saw_go_false_on_stale << '\n';

  if (!r.saw_spike && !r.saw_route_fault) {
    return fail("expected thickness out-of-band / route fault");
  }
  if (!r.saw_dent) {
    return fail("expected distance dent");
  }
  if (!r.saw_go_true) {
    return fail("expected go true while healthy");
  }
  if (!r.saw_go_false_on_stop) {
    return fail("expected go false on encoder stop");
  }
  if (!r.saw_go_false_on_stale) {
    return fail("expected go false on stale encoder");
  }

  std::cout << "demo ok\n";
  return 0;
}
