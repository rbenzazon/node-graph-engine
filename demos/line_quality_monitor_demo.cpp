// line_quality_monitor_demo — multi-root desynced line quality monitor (Phase A + light B)
//
// Design: docs/line-quality-monitor.md
//
// Topology (no sensor mux choke point):
//   edge_thick  -> cal -> ema -> stats -> rules -> latch_a -+
//   edge_dist   -> cal -> ema -> rules -> latch_b ---------+-> fusion -> plc/mes/hmi
//   edge_enc    -> motion -> latch_c ----------------------+
//
// Input edges emit sample (K=1) or chunk (K>1) as FloatBuffer 24*K; same branch compute.
// Harness: virtual-time multi-edge feed (deterministic) + optional async producer threads.

#include "node_engine/engine.hpp"
#include "node_engine/graph.hpp"

#include "line_quality/dist_profile_rules.hpp"
#include "line_quality/edge_sinks.hpp"
#include "line_quality/enc_motion.hpp"
#include "line_quality/feature_latch.hpp"
#include "line_quality/frame_calibrate.hpp"
#include "line_quality/frame_channel_filter.hpp"
#include "line_quality/frame_coalescer.hpp"
#include "line_quality/frame_edge_source.hpp"
#include "line_quality/frame_stats.hpp"
#include "line_quality/frame_util.hpp"
#include "line_quality/fusion_go.hpp"
#include "line_quality/thick_rules.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace node_engine;
using namespace node_engine::nodes::line_quality;

using clock = std::chrono::steady_clock;

int fail(char const* msg) {
  std::cerr << "FAIL: " << msg << '\n';
  return 1;
}

struct Ring {
  std::vector<double> data;  // N * 24
  std::size_t frames = 0;

  double const* frame(std::size_t i) const {
    return data.data() + (i % frames) * kChannels;
  }
};

Ring make_thickness_ring(std::size_t n, std::size_t spike_at) {
  Ring r;
  r.frames = n;
  r.data.resize(n * kChannels);
  for (std::size_t i = 0; i < n; ++i) {
    double t = static_cast<double>(i) * 0.001;
    double base = 2.0 + 0.02 * std::sin(t * 6.283185307179586);
    for (std::size_t c = 0; c < kChannels; ++c) {
      double taper = 0.01 * (static_cast<double>(c) - 11.5) / 11.5;
      double v = base + taper;
      if (i == spike_at) {
        v += 0.5;  // planted spike
      }
      r.data[i * kChannels + c] = v;
    }
  }
  return r;
}

Ring make_distance_ring(std::size_t n, std::size_t dent_at) {
  Ring r;
  r.frames = n;
  r.data.resize(n * kChannels);
  for (std::size_t i = 0; i < n; ++i) {
    for (std::size_t c = 0; c < kChannels; ++c) {
      double x = (static_cast<double>(c) - 11.5) / 11.5;
      double bow = 0.05 * x * x;
      double v = bow;
      if (i == dent_at && c >= 10 && c <= 12) {
        v -= 0.55;  // planted dent
      }
      r.data[i * kChannels + c] = v;
    }
  }
  return r;
}

// Encoder: constant vel then stop gap then resume. pos ramps with vel.
Ring make_encoder_ring(std::size_t n, std::size_t stop_begin, std::size_t stop_len, double v_run) {
  Ring r;
  r.frames = n;
  r.data.resize(n * kChannels, 0.0);
  double pos = 0.0;
  double dt = 0.0005;  // 2 kHz frame period default
  for (std::size_t i = 0; i < n; ++i) {
    bool stopped = i >= stop_begin && i < stop_begin + stop_len;
    double vel = stopped ? 0.0 : v_run;
    pos += vel * dt;
    double* f = r.data.data() + i * kChannels;
    f[0] = pos;
    f[1] = vel;
    f[2] = 0.0;
    f[3] = std::fmod(pos / 100.0, 1.0);
    f[4] = 1.0;
  }
  return r;
}

struct EmitCursor {
  std::size_t index = 0;
  double t_frame = 0.0;
  double period = 50e-6;
  std::size_t K = 1;
};

std::vector<double> pull_emit(Ring const& ring, EmitCursor& cur) {
  std::vector<double> buf;
  buf.resize(cur.K * kChannels);
  for (std::size_t f = 0; f < cur.K; ++f) {
    double const* src = ring.frame(cur.index + f);
    std::copy(src, src + kChannels, buf.data() + f * kChannels);
  }
  cur.index += cur.K;
  return buf;
}

struct Handles {
  FrameEdgeSource* edge_thick = nullptr;
  FrameEdgeSource* edge_dist = nullptr;
  FrameEdgeSource* edge_enc = nullptr;
  FrameChannelFilter* filt_a = nullptr;
  FrameChannelFilter* filt_b = nullptr;
  FrameCoalescer* coal_a = nullptr;
  ThickRules* rules_a = nullptr;
  DistProfileRules* rules_b = nullptr;
  EncMotion* enc = nullptr;
  FeatureLatch* latch_a = nullptr;
  FeatureLatch* latch_b = nullptr;
  MotionLatch* latch_c = nullptr;
  FusionGo* fusion = nullptr;
  EdgePlcGoNoGo* plc = nullptr;
  EdgeMesEvent* mes = nullptr;
  EdgeHmiFlags* hmi = nullptr;
  Node* fusion_node = nullptr;
};

Graph build_graph() {
  Graph g;
  g.add_node("edge_thick", std::make_unique<FrameEdgeSource>());
  g.add_node("edge_dist", std::make_unique<FrameEdgeSource>());
  g.add_node("edge_enc", std::make_unique<FrameEdgeSource>());

  g.add_node("cal_a", std::make_unique<FrameCalibrate>());
  g.add_node("filt_a", std::make_unique<FrameChannelFilter>());
  g.add_node("stats_a", std::make_unique<FrameStats>());
  g.add_node("coal_a", std::make_unique<FrameCoalescer>());
  g.add_node("rules_a", std::make_unique<ThickRules>());
  g.add_node("latch_a", std::make_unique<FeatureLatch>());

  g.add_node("cal_b", std::make_unique<FrameCalibrate>());
  g.add_node("filt_b", std::make_unique<FrameChannelFilter>());
  g.add_node("rules_b", std::make_unique<DistProfileRules>());
  g.add_node("latch_b", std::make_unique<FeatureLatch>());

  g.add_node("enc_m", std::make_unique<EncMotion>());
  g.add_node("latch_c", std::make_unique<MotionLatch>());

  g.add_node("fusion", std::make_unique<FusionGo>());
  g.add_node("edge_plc", std::make_unique<EdgePlcGoNoGo>());
  g.add_node("edge_mes", std::make_unique<EdgeMesEvent>());
  g.add_node("edge_hmi", std::make_unique<EdgeHmiFlags>());

  // Branch A
  g.connect("edge_thick", "out", "cal_a", "in");
  g.connect("cal_a", "out", "filt_a", "in");
  g.connect("filt_a", "out", "stats_a", "in");
  g.connect("stats_a", "out", "coal_a", "in");
  g.connect("stats_a", "mean", "rules_a", "mean");
  g.connect("stats_a", "p2p", "rules_a", "p2p");
  g.connect("edge_thick", "t_stamp", "latch_a", "t_stamp");
  g.connect("rules_a", "in_spec", "latch_a", "b0");
  g.connect("rules_a", "uniform", "latch_a", "b1");
  g.connect("rules_a", "spike", "latch_a", "b2");
  g.connect("rules_a", "last_mean", "latch_a", "s0");
  g.connect("rules_a", "last_p2p", "latch_a", "s1");

  // Branch B
  g.connect("edge_dist", "out", "cal_b", "in");
  g.connect("cal_b", "out", "filt_b", "in");
  g.connect("filt_b", "out", "rules_b", "in");
  g.connect("edge_dist", "t_stamp", "latch_b", "t_stamp");
  g.connect("rules_b", "height_ok", "latch_b", "b0");
  g.connect("rules_b", "flat", "latch_b", "b1");
  g.connect("rules_b", "dent", "latch_b", "b2");
  g.connect("rules_b", "last_mean", "latch_b", "s0");
  g.connect("rules_b", "last_bow", "latch_b", "s1");

  // Branch C
  g.connect("edge_enc", "out", "enc_m", "in");
  g.connect("edge_enc", "t_stamp", "latch_c", "t_stamp");
  g.connect("enc_m", "line_running", "latch_c", "line_running");
  g.connect("enc_m", "speed_stable", "latch_c", "speed_stable");
  g.connect("enc_m", "pos_mm", "latch_c", "pos_mm");
  g.connect("enc_m", "vel", "latch_c", "vel");

  // Late join fusion
  g.connect("latch_a", "t_stamp", "fusion", "t_a");
  g.connect("latch_a", "b0", "fusion", "thick_in_spec");
  g.connect("latch_a", "b1", "fusion", "thick_uniform");
  g.connect("latch_a", "b2", "fusion", "thick_spike");
  g.connect("latch_b", "t_stamp", "fusion", "t_b");
  g.connect("latch_b", "b0", "fusion", "height_ok");
  g.connect("latch_b", "b1", "fusion", "flat");
  g.connect("latch_b", "b2", "fusion", "dent");
  g.connect("latch_c", "t_stamp", "fusion", "t_c");
  g.connect("latch_c", "line_ok", "fusion", "line_ok");
  g.connect("latch_c", "pos_mm", "fusion", "pos_mm");

  g.connect("fusion", "go", "edge_plc", "go");
  g.connect("fusion", "nogo_latch", "edge_plc", "nogo_latch");
  g.connect("fusion", "nogo_latch", "edge_mes", "nogo_latch");
  g.connect("fusion", "fault_code", "edge_mes", "fault_code");
  g.connect("fusion", "grade", "edge_mes", "grade");
  g.connect("fusion", "go", "edge_hmi", "go");
  g.connect("latch_a", "b2", "edge_hmi", "spike");
  g.connect("latch_b", "b2", "edge_hmi", "dent");
  g.connect("latch_c", "line_ok", "edge_hmi", "line_ok");

  return g;
}

bool bind_handles(FlatGraph& flat, Handles& h) {
  h.edge_thick = dynamic_cast<FrameEdgeSource*>(flat.find_node("edge_thick"));
  h.edge_dist = dynamic_cast<FrameEdgeSource*>(flat.find_node("edge_dist"));
  h.edge_enc = dynamic_cast<FrameEdgeSource*>(flat.find_node("edge_enc"));
  h.filt_a = dynamic_cast<FrameChannelFilter*>(flat.find_node("filt_a"));
  h.filt_b = dynamic_cast<FrameChannelFilter*>(flat.find_node("filt_b"));
  h.coal_a = dynamic_cast<FrameCoalescer*>(flat.find_node("coal_a"));
  h.rules_a = dynamic_cast<ThickRules*>(flat.find_node("rules_a"));
  h.rules_b = dynamic_cast<DistProfileRules*>(flat.find_node("rules_b"));
  h.enc = dynamic_cast<EncMotion*>(flat.find_node("enc_m"));
  h.latch_a = dynamic_cast<FeatureLatch*>(flat.find_node("latch_a"));
  h.latch_b = dynamic_cast<FeatureLatch*>(flat.find_node("latch_b"));
  h.latch_c = dynamic_cast<MotionLatch*>(flat.find_node("latch_c"));
  h.fusion = dynamic_cast<FusionGo*>(flat.find_node("fusion"));
  h.fusion_node = flat.find_node("fusion");
  h.plc = dynamic_cast<EdgePlcGoNoGo*>(flat.find_node("edge_plc"));
  h.mes = dynamic_cast<EdgeMesEvent*>(flat.find_node("edge_mes"));
  h.hmi = dynamic_cast<EdgeHmiFlags*>(flat.find_node("edge_hmi"));
  return h.edge_thick && h.edge_dist && h.edge_enc && h.filt_a && h.filt_b && h.coal_a &&
         h.rules_a && h.rules_b && h.enc && h.latch_a && h.latch_b && h.latch_c && h.fusion &&
         h.plc && h.mes && h.hmi && h.fusion_node;
}

void set_policy_literals(FlatGraph& flat) {
  auto* fusion = flat.find_node("fusion");
  fusion->find_input("stale_a")->literal = 0.001;
  fusion->find_input("stale_b")->literal = 0.003;
  fusion->find_input("stale_c")->literal = 0.015;
  fusion->find_input("sustain_ms")->literal = 0.010;
  fusion->find_input("sustain_mm")->literal = 10.0;

  flat.find_node("cal_a")->find_input("scale")->literal = 1.0;
  flat.find_node("cal_a")->find_input("offset")->literal = 0.0;
  flat.find_node("cal_b")->find_input("scale")->literal = 1.0;
  flat.find_node("filt_a")->find_input("alpha")->literal = 1.0;  // pass-through for spike clarity
  flat.find_node("filt_b")->find_input("alpha")->literal = 1.0;
  flat.find_node("coal_a")->find_input("window_frames")->literal = 20.0;
  flat.find_node("rules_a")->find_input("lo")->literal = 1.90;
  flat.find_node("rules_a")->find_input("hi")->literal = 2.10;
  flat.find_node("rules_a")->find_input("uniform_max")->literal = 0.15;
  flat.find_node("rules_a")->find_input("spike_delta")->literal = 0.25;
  flat.find_node("rules_b")->find_input("height_lo")->literal = -0.5;
  flat.find_node("rules_b")->find_input("height_hi")->literal = 0.5;
  flat.find_node("rules_b")->find_input("flat_max")->literal = 0.35;
  flat.find_node("rules_b")->find_input("dent_thr")->literal = 0.4;
  flat.find_node("enc_m")->find_input("v_min")->literal = 0.5;
  flat.find_node("enc_m")->find_input("vel_std_max")->literal = 5.0;
}

void clear_dirty(FlatGraph& flat) {
  for (auto& n : flat.nodes) {
    if (n.node) {
      n.node->dirty = false;
    }
  }
}

void arm_and_run(Engine& engine, FlatGraph& flat, Handles& h, char const* edge_id,
                 FrameEdgeSource* edge, std::vector<double> buf, double t0, std::size_t K,
                 double t_now) {
  edge->set_emit(std::move(buf), t0, K);
  h.fusion_node->find_input("t_now")->literal = t_now;
  clear_dirty(flat);
  engine.triggers().push(edge_id);
  if (!engine.poll_trigger_and_run(flat)) {
    throw std::runtime_error(std::string("poll failed for ") + edge_id);
  }
}

bool out_bool(Node& n, char const* pin) {
  auto* p = n.find_output(pin);
  if (!p || !std::holds_alternative<bool>(p->buffer)) {
    return false;
  }
  return std::get<bool>(p->buffer);
}

// Multi-root check: no node is a common parent of all three edges (edges are roots).
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
  // Edges must have no inbound edges.
  for (auto const& e : flat.edges) {
    if (e.to_node == "edge_thick" || e.to_node == "edge_dist" || e.to_node == "edge_enc") {
      return false;
    }
  }
  // No single node fans out to all three edge ids as children (would be hub-before-edges).
  return true;
}

struct RunResult {
  bool saw_spike = false;
  bool saw_dent = false;
  bool saw_go_true = false;
  bool saw_go_false_on_stop = false;
  bool saw_go_false_on_stale = false;
  bool nogo_latched = false;
  std::uint64_t mes_events = 0;
  std::uint64_t coal_ready = 0;
  std::size_t frames_a = 0;
  std::size_t emits_a = 0;
};

RunResult run_scenario(Engine& engine, FlatGraph& flat, Handles& h, std::size_t K_thick,
                       bool do_stale_c_test) {
  // Plants must fall inside t_end at each edge's frame rate.
  // A @ 20 kHz, B @ 7 kHz, C @ 2 kHz; stop ~40 ms on C, spike ~5 ms on A, dent ~8 ms on B.
  constexpr std::size_t kN = 8000;
  constexpr std::size_t kSpike = 100;   // t≈5.0 ms @ 20 kHz
  constexpr std::size_t kDent = 56;     // t≈8.0 ms @ 7 kHz
  constexpr std::size_t kStop0 = 80;    // t≈40 ms @ 2 kHz
  constexpr std::size_t kStopN = 40;    // 20 ms stop gap

  Ring ring_a = make_thickness_ring(kN, kSpike);
  Ring ring_b = make_distance_ring(kN, kDent);
  Ring ring_c = make_encoder_ring(kN, kStop0, kStopN, 10.0);

  EmitCursor ca{.index = 0, .t_frame = 0.0, .period = 50e-6, .K = K_thick};
  EmitCursor cb{.index = 0, .t_frame = 0.0, .period = 1.0 / 7000.0, .K = 1};
  EmitCursor cc{.index = 0, .t_frame = 0.0, .period = 1.0 / 2000.0, .K = 1};

  h.filt_a->reset_state();
  h.filt_b->reset_state();
  h.coal_a->reset();
  h.rules_a->reset_state();
  h.enc->reset_state();
  h.fusion->reset_state();
  h.plc->reset_counts();
  h.mes->reset_counts();

  RunResult rr;
  double t_end = 0.10;  // 100 ms virtual — covers spike, dent, stop
  double t_now = 0.0;

  // Prime each branch once so fusion LKG inputs are valid (multi-root, staggered start).
  {
    std::size_t k0 = ca.K;
    auto ba = pull_emit(ring_a, ca);
    arm_and_run(engine, flat, h, "edge_thick", h.edge_thick, std::move(ba), 0.0, k0, 0.0);
    ca.t_frame = static_cast<double>(k0) * ca.period;
    auto bb = pull_emit(ring_b, cb);
    arm_and_run(engine, flat, h, "edge_dist", h.edge_dist, std::move(bb), 0.0, 1, 0.0);
    cb.t_frame = cb.period;
    auto bc = pull_emit(ring_c, cc);
    arm_and_run(engine, flat, h, "edge_enc", h.edge_enc, std::move(bc), 0.0, 1, 0.0);
    cc.t_frame = cc.period;
    t_now = 0.0;
    rr.frames_a += k0;
    ++rr.emits_a;
    if (h.plc->last_go()) {
      rr.saw_go_true = true;
    }
  }

  // Event queue: next deadline per edge.
  double next_a = ca.t_frame;
  double next_b = cb.t_frame;
  double next_c = cc.t_frame;

  auto step_edge = [&](std::string const& id, FrameEdgeSource* edge, Ring const& ring,
                       EmitCursor& cur, double& next_deadline) {
    if (cur.index + cur.K > ring.frames) {
      next_deadline = 1e100;
      return;
    }
    double t0 = cur.t_frame;
    auto buf = pull_emit(ring, cur);
    std::size_t K = cur.K;
    cur.t_frame += static_cast<double>(K) * cur.period;
    next_deadline = cur.t_frame;
    // Consumer "now" is at least the last frame time in this emit.
    t_now = std::max(t_now, t0 + static_cast<double>(K > 0 ? K - 1 : 0) * cur.period);
    arm_and_run(engine, flat, h, id.c_str(), edge, std::move(buf), t0, K, t_now);

    if (id == "edge_thick") {
      rr.frames_a += K;
      ++rr.emits_a;
      if (out_bool(*flat.find_node("rules_a"), "spike") || h.latch_a->b2()) {
        rr.saw_spike = true;
      }
      auto* ready = flat.find_node("coal_a")->find_output("ready");
      if (ready && std::holds_alternative<bool>(ready->buffer) && std::get<bool>(ready->buffer)) {
        ++rr.coal_ready;
      }
    } else if (id == "edge_dist") {
      if (out_bool(*flat.find_node("rules_b"), "dent") || h.latch_b->b2()) {
        rr.saw_dent = true;
      }
    } else if (id == "edge_enc") {
      if (!h.latch_c->line_ok()) {
        // Encoder stop gap (or other motion fail) while geometry may still be fresh.
        if (!h.plc->last_go()) {
          rr.saw_go_false_on_stop = true;
        }
      }
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
    // Drive virtual time to the next sensor deadline before processing it.
    t_now = std::max(t_now, n);
    if (n == next_a) {
      step_edge("edge_thick", h.edge_thick, ring_a, ca, next_a);
    } else if (n == next_b) {
      step_edge("edge_dist", h.edge_dist, ring_b, cb, next_b);
    } else {
      step_edge("edge_enc", h.edge_enc, ring_c, cc, next_c);
    }
  }

  // Stale-C test: freeze encoder, keep A/B updating until C exceeds stale_c.
  if (do_stale_c_test) {
    double t_c_last = h.latch_c->t_stamp();
    double stale_c = 0.015;
    for (int i = 0; i < 5; ++i) {
      if (ca.index + ca.K > ring_a.frames) {
        break;
      }
      double t0 = ca.t_frame;
      auto buf = pull_emit(ring_a, ca);
      std::size_t K = ca.K;
      ca.t_frame += static_cast<double>(K) * ca.period;
      t_now = t_c_last + stale_c + 0.005 + static_cast<double>(i) * 0.001;
      arm_and_run(engine, flat, h, "edge_thick", h.edge_thick, std::move(buf), t0, K, t_now);
      rr.frames_a += K;
      ++rr.emits_a;
      if (cb.index + cb.K <= ring_b.frames) {
        double tb = cb.t_frame;
        auto bb = pull_emit(ring_b, cb);
        cb.t_frame += cb.period;
        arm_and_run(engine, flat, h, "edge_dist", h.edge_dist, std::move(bb), tb, 1, t_now);
      }
      if (!h.plc->last_go()) {
        rr.saw_go_false_on_stale = true;
      }
    }
  }

  rr.nogo_latched = h.fusion->nogo_latched() || h.plc->last_nogo();
  rr.mes_events = h.mes->events();
  return rr;
}

}  // namespace

int main() {
  constexpr std::size_t kWorkers = 4;

  Engine engine{kWorkers};
  std::cout << "line_quality_monitor_demo\n";
  std::cout << "session workers: " << engine.scheduler().worker_threads() << '\n';
  std::cout << "topology: edge_thick|edge_dist|edge_enc -> branches -> fusion -> sinks\n";
  std::cout << "no sensor mux; dual format K=1 and K=10 on edge_thick\n\n";

  Graph g = build_graph();
  // Authoring: three roots present.
  if (!g.find_node("edge_thick") || !g.find_node("edge_dist") || !g.find_node("edge_enc")) {
    return fail("missing edge roots in authoring graph");
  }
  if (g.find_node("edge_daq_all") || g.find_node("sensor_mux")) {
    return fail("unexpected choke-point node in authoring graph");
  }

  FlatGraph flat = engine.compile(g);
  Handles h;
  if (!bind_handles(flat, h)) {
    return fail("bind flat handles");
  }
  set_policy_literals(flat);

  if (!assert_multi_root(flat)) {
    return fail("multi-root topology broken (edge has inbound or missing)");
  }
  std::cout << "multi-root: edge_thick/dist/enc have no inbound wires ok\n";

  // --- Phase A: K=1 ---
  std::cout << "\n--- Phase A K_thick=1 ---\n";
  RunResult r1 = run_scenario(engine, flat, h, /*K_thick=*/1, /*stale=*/true);
  std::cout << "frames_a=" << r1.frames_a << " emits_a=" << r1.emits_a
            << " coal_ready=" << r1.coal_ready << " spike=" << r1.saw_spike
            << " dent=" << r1.saw_dent << " go_true=" << r1.saw_go_true
            << " go_false_stop=" << r1.saw_go_false_on_stop
            << " go_false_stale=" << r1.saw_go_false_on_stale
            << " nogo=" << r1.nogo_latched << " mes_events=" << r1.mes_events << '\n';

  if (!r1.saw_spike) {
    return fail("expected thickness spike detection (K=1)");
  }
  if (!r1.saw_dent) {
    return fail("expected distance dent detection (K=1)");
  }
  if (!r1.saw_go_true) {
    return fail("expected go true while healthy + motion (K=1)");
  }
  if (!r1.saw_go_false_on_stop) {
    return fail("expected go false during encoder stop (K=1)");
  }
  if (!r1.saw_go_false_on_stale) {
    return fail("expected go false when encoder stale (K=1)");
  }
  if (r1.coal_ready == 0) {
    return fail("expected frame coalescer ready at least once (K=1)");
  }

  // --- Phase A: K=10 dual format ---
  std::cout << "\n--- Phase A K_thick=10 ---\n";
  RunResult r10 = run_scenario(engine, flat, h, /*K_thick=*/10, /*stale=*/true);
  std::cout << "frames_a=" << r10.frames_a << " emits_a=" << r10.emits_a
            << " coal_ready=" << r10.coal_ready << " spike=" << r10.saw_spike
            << " dent=" << r10.saw_dent << " go_true=" << r10.saw_go_true
            << " go_false_stop=" << r10.saw_go_false_on_stop
            << " go_false_stale=" << r10.saw_go_false_on_stale
            << " nogo=" << r10.nogo_latched << " mes_events=" << r10.mes_events << '\n';

  if (!r10.saw_spike) {
    return fail("expected thickness spike detection (K=10)");
  }
  if (!r10.saw_dent) {
    return fail("expected distance dent detection (K=10)");
  }
  if (!r10.saw_go_true) {
    return fail("expected go true (K=10)");
  }
  if (!r10.saw_go_false_on_stop) {
    return fail("expected go false on stop (K=10)");
  }
  if (!r10.saw_go_false_on_stale) {
    return fail("expected go false on stale C (K=10)");
  }
  // Dual-format: fewer emits for similar frame count; coalesce still works.
  if (r10.emits_a >= r1.emits_a) {
    return fail("K=10 should produce fewer thick emits than K=1 for similar duration");
  }
  if (r10.coal_ready == 0) {
    return fail("expected coalescer ready with K=10");
  }

  // Coalesce frame accounting: one emit of K=20 should complete W=20.
  {
    std::cout << "\n--- coalesce K=W=20 single emit ---\n";
    h.filt_a->reset_state();
    h.coal_a->reset();
    h.rules_a->reset_state();
    Ring ring = make_thickness_ring(40, 9999);
    EmitCursor c{.index = 0, .t_frame = 0.0, .period = 50e-6, .K = 20};
    auto buf = pull_emit(ring, c);
    arm_and_run(engine, flat, h, "edge_thick", h.edge_thick, std::move(buf), 0.0, 20, 0.001);
    auto* ready = flat.find_node("coal_a")->find_output("ready");
    auto* win = flat.find_node("coal_a")->find_output("window");
    if (!ready || !std::holds_alternative<bool>(ready->buffer) || !std::get<bool>(ready->buffer)) {
      return fail("coalesce should ready on single K=20 emit");
    }
    if (!win || !std::holds_alternative<std::vector<double>>(win->buffer) ||
        std::get<std::vector<double>>(win->buffer).size() != 20 * kChannels) {
      return fail("coalesce window size != 24*20");
    }
    std::cout << "single-emit full window ok\n";
  }

  // --- Phase B light: free-run async producers, measure emit rates ---
  constexpr double kPhaseBWallS = 60.0;
  std::cout << "\n--- Phase B light (async producers, ~" << kPhaseBWallS << "s wall) ---\n";
  {
    Ring ring_a = make_thickness_ring(20000, 99999);
    Ring ring_b = make_distance_ring(20000, 99999);
    Ring ring_c = make_encoder_ring(20000, 99999, 0, 10.0);

    std::atomic<bool> run{true};
    std::atomic<std::uint64_t> emits_a{0}, emits_b{0}, emits_c{0};
    std::atomic<std::uint64_t> frames_a{0}, frames_b{0}, frames_c{0};
    std::atomic<std::uint64_t> q_hwm{0};
    std::mutex arm_mu;

    auto producer = [&](char const* id, FrameEdgeSource* edge, Ring const* ring, double period,
                        std::size_t K, std::atomic<std::uint64_t>* emits,
                        std::atomic<std::uint64_t>* frames) {
      std::size_t idx = 0;
      double t = 0.0;
      auto deadline = clock::now();
      while (run.load(std::memory_order_relaxed)) {
        deadline += std::chrono::duration_cast<clock::duration>(
            std::chrono::duration<double>(period * static_cast<double>(K)));
        std::this_thread::sleep_until(deadline);
        if (!run.load(std::memory_order_relaxed)) {
          break;
        }
        std::vector<double> buf(K * kChannels);
        for (std::size_t f = 0; f < K; ++f) {
          auto const* src = ring->frame(idx + f);
          std::copy(src, src + kChannels, buf.data() + f * kChannels);
        }
        idx += K;
        {
          std::lock_guard<std::mutex> lock(arm_mu);
          edge->set_emit(std::move(buf), t, K);
          engine.triggers().push(id);
        }
        t += period * static_cast<double>(K);
        emits->fetch_add(1, std::memory_order_relaxed);
        frames->fetch_add(K, std::memory_order_relaxed);
      }
    };

    // Slow periods so Debug build can keep up (capacity demo, not full 20 kHz RT).
    std::thread ta(producer, "edge_thick", h.edge_thick, &ring_a, 200e-6, std::size_t{5}, &emits_a,
                   &frames_a);
    std::thread tb(producer, "edge_dist", h.edge_dist, &ring_b, 500e-6, std::size_t{1}, &emits_b,
                   &frames_b);
    std::thread tc(producer, "edge_enc", h.edge_enc, &ring_c, 1e-3, std::size_t{1}, &emits_c,
                   &frames_c);

    auto t0 = clock::now();
    while (std::chrono::duration<double>(clock::now() - t0).count() < kPhaseBWallS) {
      bool ran = false;
      {
        std::lock_guard<std::mutex> lock(arm_mu);
        h.fusion_node->find_input("t_now")->literal =
            std::chrono::duration<double>(clock::now() - t0).count();
        if (!engine.triggers().empty()) {
          q_hwm.fetch_add(1, std::memory_order_relaxed);
        }
        ran = engine.poll_trigger_and_run(flat);
      }
      if (!ran) {
        std::this_thread::sleep_for(std::chrono::microseconds(50));
      }
    }
    run.store(false, std::memory_order_relaxed);
    ta.join();
    tb.join();
    tc.join();
    // Drain remaining
    {
      std::lock_guard<std::mutex> lock(arm_mu);
      while (engine.poll_trigger_and_run(flat)) {
      }
    }

    double wall = std::chrono::duration<double>(clock::now() - t0).count();
    auto hz = [&](std::uint64_t n) {
      return wall > 0.0 ? static_cast<double>(n) / wall : 0.0;
    };
    std::cout << std::fixed << std::setprecision(1);
    std::cout << "wall_s=" << wall << " emit_hz A/B/C=" << hz(emits_a.load()) << '/'
              << hz(emits_b.load()) << '/' << hz(emits_c.load()) << " frame_hz A/B/C="
              << hz(frames_a.load()) << '/' << hz(frames_b.load()) << '/' << hz(frames_c.load())
              << " non_empty_polls~=" << q_hwm.load() << " plc_emits=" << h.plc->emits() << '\n';
    std::cout << std::defaultfloat;
    if (emits_a.load() == 0 || emits_b.load() == 0 || emits_c.load() == 0) {
      return fail("async producers produced zero emits");
    }
  }

  std::cout << "\nline_quality_monitor_demo ok\n";
  return 0;
}
