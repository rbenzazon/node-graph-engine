// Demo 3/6 — bench_branch_fanout
//
// Insight: inter-node parallelism after a fan-out. One BufferSource feeds B
// independent ParallelScaleFilter branches (ParallelMode::None so work is
// per-branch serial). Taskflow precede lets sibling branches run concurrently
// on the session executor.
//
// Graph:
//   src ──► filt_0 ──► sink_0
//       ├──► filt_1 ──► sink_1
//       └──► …      ──► …
//
// Sweeps: workers × branches × N × burn.
// Metrics: ms/tick, branch-effective GB/s (2*N*8*B), speedup vs 1-worker,
// combined checksum (must match across worker counts).

#include "node_engine/engine.hpp"
#include "node_engine/graph.hpp"

#include "examples/buffer_source.hpp"
#include "examples/parallel_scale_filter.hpp"
#include "examples/producer_consumer.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace {

using clock = std::chrono::steady_clock;

struct BenchResult {
  std::size_t workers = 0;
  std::size_t branches = 0;
  std::size_t n = 0;
  std::size_t burn = 0;
  int timed_ticks = 0;
  double ms_per_tick = 0.0;
  double gb_per_s = 0.0;
  double checksum = 0.0;
  double speedup = 1.0;
};

double checksum_of(std::vector<double> const& v) {
  double s = 0.0;
  for (std::size_t i = 0; i < v.size(); ++i) {
    s += v[i] * (1.0 + 0.000001 * static_cast<double>(i & 1023));
  }
  return s;
}

std::vector<double> make_input(std::size_t n) {
  std::vector<double> data(n);
  for (std::size_t i = 0; i < n; ++i) {
    data[i] = 0.001 * static_cast<double>(i % 997) + 1.0;
  }
  return data;
}

double branch_gain(std::size_t b) {
  return 1.0 + 0.1 * static_cast<double>(b);
}

std::string filt_id(std::size_t b) { return "filt_" + std::to_string(b); }
std::string sink_id(std::size_t b) { return "sink_" + std::to_string(b); }

BenchResult run_config(std::size_t workers, std::size_t branches, std::size_t n, std::size_t burn,
                       int warmup, int timed) {
  using namespace node_engine;
  using namespace node_engine::nodes;

  if (branches == 0) {
    throw std::runtime_error("bench_branch_fanout: branches must be >= 1");
  }

  Engine engine{workers};
  auto input = make_input(n);

  Graph g;
  auto src = std::make_unique<BufferSource>();
  src->set_data(input);
  g.add_node("src", std::move(src));

  for (std::size_t b = 0; b < branches; ++b) {
    auto filt = std::make_unique<ParallelScaleFilter>();
    // Isolate inter-node fan-out: no intra-node subflow splits.
    filt->mode = ParallelMode::None;
    filt->grain_size = 0;
    filt->burn_iters = burn;
    g.add_node(filt_id(b), std::move(filt));
    g.add_node(sink_id(b), std::make_unique<BufferConsumer>());
    g.find_node(filt_id(b))->find_input("gain")->literal = branch_gain(b);
    g.connect("src", "out", filt_id(b), "in");
    g.connect(filt_id(b), "out", sink_id(b), "in");
  }

  FlatGraph flat = engine.compile(g);

  auto* src_ptr = dynamic_cast<BufferSource*>(flat.find_node("src"));
  if (!src_ptr) {
    throw std::runtime_error("bench_branch_fanout: missing src");
  }
  src_ptr->set_data(input);

  std::vector<BufferConsumer*> sinks;
  sinks.reserve(branches);
  for (std::size_t b = 0; b < branches; ++b) {
    auto* filt = dynamic_cast<ParallelScaleFilter*>(flat.find_node(filt_id(b)));
    auto* sink = dynamic_cast<BufferConsumer*>(flat.find_node(sink_id(b)));
    if (!filt || !sink) {
      throw std::runtime_error("bench_branch_fanout: missing branch " + std::to_string(b));
    }
    filt->mode = ParallelMode::None;
    filt->grain_size = 0;
    filt->burn_iters = burn;
    flat.find_node(filt_id(b))->find_input("gain")->literal = branch_gain(b);
    sinks.push_back(sink);
  }

  auto one_tick = [&]() {
    engine.mark_dirty_reachable(flat, "src");
    engine.tick(flat);
  };

  for (int i = 0; i < warmup; ++i) {
    one_tick();
  }

  auto t0 = clock::now();
  for (int i = 0; i < timed; ++i) {
    one_tick();
  }
  auto t1 = clock::now();

  double ms_total = std::chrono::duration<double, std::milli>(t1 - t0).count();
  double ms = ms_total / static_cast<double>(timed);

  // Per-branch filter in+out, times B independent branches.
  double bytes =
      2.0 * static_cast<double>(n) * static_cast<double>(sizeof(double)) * static_cast<double>(branches);
  double gb_s = (bytes / 1e9) / (ms / 1000.0);

  double combined = 0.0;
  for (std::size_t b = 0; b < branches; ++b) {
    double cs = checksum_of(sinks[b]->last());
    // Weight by branch index so a swapped/miswired branch cannot cancel out.
    combined += cs * (1.0 + 0.01 * static_cast<double>(b));
    if (sinks[b]->last().size() != n) {
      throw std::runtime_error("bench_branch_fanout: sink size mismatch on branch " +
                               std::to_string(b));
    }
  }

  BenchResult r;
  r.workers = engine.scheduler().worker_threads();
  r.branches = branches;
  r.n = n;
  r.burn = burn;
  r.timed_ticks = timed;
  r.ms_per_tick = ms;
  r.gb_per_s = gb_s;
  r.checksum = combined;
  return r;
}

void print_header() {
  std::cout << std::left << std::setw(8) << "workers" << std::setw(10) << "branches"
            << std::setw(12) << "N" << std::setw(8) << "burn" << std::setw(12) << "ms/tick"
            << std::setw(12) << "GB/s" << std::setw(10) << "speedup" << std::setw(14)
            << "checksum" << '\n';
  std::cout << std::string(86, '-') << '\n';
}

void print_row(BenchResult const& r) {
  std::cout << std::left << std::setw(8) << r.workers << std::setw(10) << r.branches
            << std::setw(12) << r.n << std::setw(8) << r.burn << std::setw(12) << std::fixed
            << std::setprecision(3) << r.ms_per_tick << std::setw(12) << std::setprecision(2)
            << r.gb_per_s << std::setw(10) << std::setprecision(2) << r.speedup << std::setw(14)
            << std::setprecision(4) << std::scientific << r.checksum << std::defaultfloat
            << '\n';
}

}  // namespace

int main() {
  using namespace node_engine;

  std::size_t hw = std::max<std::size_t>(1, std::thread::hardware_concurrency());
  std::cout << "bench_branch_fanout\n";
  std::cout << "hardware_concurrency=" << hw << '\n';
  std::cout << "session model: Engine{workers} per config; Taskflow rebuilt each tick\n";
  std::cout << "graph: BufferSource fan-out -> B x (ParallelScaleFilter[None] -> BufferConsumer)\n";
  std::cout << "insight: inter-node branch concurrency (no intra-node grain splits)\n";
  std::cout << "note: GB/s uses 2*N*8*B bytes (all branch filters in+out); edge copies extra\n\n";

  std::vector<std::size_t> workers_list = {1, 2};
  if (hw >= 4) {
    workers_list.push_back(4);
  }
  if (hw > 4) {
    workers_list.push_back(hw);
  }
  std::sort(workers_list.begin(), workers_list.end());
  workers_list.erase(std::unique(workers_list.begin(), workers_list.end()), workers_list.end());

  std::vector<std::size_t> branch_list = {1, 2, 4, 8};
  if (hw >= 8) {
    branch_list.push_back(16);
  }
  std::vector<std::size_t> sizes = {1u << 16, 1u << 18};  // 64Ki, 256Ki
  if (hw >= 4) {
    sizes.push_back(1u << 20);  // 1Mi
  }
  std::vector<std::size_t> burns = {1, 8};
  constexpr int kWarmup = 3;
  constexpr int kTimed = 8;

  print_header();

  struct Key {
    std::size_t branches = 0;
    std::size_t n = 0;
    std::size_t burn = 0;
    bool operator==(Key const& o) const {
      return branches == o.branches && n == o.n && burn == o.burn;
    }
  };
  struct KeyHash {
    std::size_t operator()(Key const& k) const {
      return k.branches ^ (k.n << 1) ^ (k.burn << 11);
    }
  };
  std::unordered_map<Key, double, KeyHash> baseline_ms;
  std::unordered_map<Key, double, KeyHash> checksums;

  int failures = 0;

  try {
    for (std::size_t burn : burns) {
      for (std::size_t n : sizes) {
        for (std::size_t branches : branch_list) {
          for (std::size_t w : workers_list) {
            BenchResult r = run_config(w, branches, n, burn, kWarmup, kTimed);
            Key key{branches, n, burn};
            if (w == 1) {
              baseline_ms[key] = r.ms_per_tick;
              r.speedup = 1.0;
            } else {
              auto it = baseline_ms.find(key);
              r.speedup = (it != baseline_ms.end() && it->second > 0.0)
                              ? (it->second / r.ms_per_tick)
                              : 0.0;
            }

            auto [cit, inserted] = checksums.emplace(key, r.checksum);
            if (!inserted) {
              double a = cit->second;
              double b = r.checksum;
              if (std::abs(a - b) > 1e-6 * std::max(1.0, std::abs(a))) {
                std::cerr << "CHECKSUM MISMATCH workers=" << w << " branches=" << branches
                          << " N=" << n << " burn=" << burn << " a=" << a << " b=" << b << '\n';
                ++failures;
              }
            }

            print_row(r);
            std::cout.flush();
          }
        }
      }
    }
  } catch (std::exception const& ex) {
    std::cerr << "FAIL: " << ex.what() << '\n';
    return 1;
  }

  std::cout << '\n';
  if (failures != 0) {
    std::cerr << "bench_branch_fanout FAILED checksums=" << failures << '\n';
    return 1;
  }
  std::cout << "bench_branch_fanout ok\n";
  return 0;
}
