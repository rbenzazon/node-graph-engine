// Demo 2/6 — bench_parallel_scale
//
// Session Engine (reused tf::Executor) + large FloatBuffer through a parallel
// scale filter. Sweeps workers × grain × N × burn intensity.
//
// Metrics: wall ms/tick, effective GB/s (read+write of doubles), checksum,
// speedup vs 1-worker baseline for the same (N, grain, burn).

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
  std::size_t n = 0;
  std::size_t grain = 0;
  std::size_t burn = 0;
  int timed_ticks = 0;
  double ms_per_tick = 0.0;
  double gb_per_s = 0.0;
  double checksum = 0.0;
  double speedup = 1.0;
};

double checksum_of(std::vector<double> const& v) {
  // Stable-ish sum; not crypto — just correctness across configs.
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

BenchResult run_config(std::size_t workers, std::size_t n, std::size_t grain, std::size_t burn,
                       double gain, int warmup, int timed) {
  using namespace node_engine;
  using namespace node_engine::nodes;

  Engine engine{workers};

  auto src = std::make_unique<BufferSource>();
  src->set_data(make_input(n));
  auto* src_ptr = src.get();

  auto filt = std::make_unique<ParallelScaleFilter>();
  filt->mode = ParallelMode::OnCollection;
  filt->grain_size = grain;
  filt->burn_iters = burn;
  auto* filt_ptr = filt.get();

  Graph g;
  g.add_node("src", std::move(src));
  g.add_node("filt", std::move(filt));
  g.add_node("sink", std::make_unique<BufferConsumer>());
  g.find_node("filt")->find_input("gain")->literal = gain;
  g.connect("src", "out", "filt", "in");
  g.connect("filt", "out", "sink", "in");

  FlatGraph flat = engine.compile(g);
  // Re-bind after compile (nodes were cloned).
  src_ptr = dynamic_cast<BufferSource*>(flat.find_node("src"));
  filt_ptr = dynamic_cast<ParallelScaleFilter*>(flat.find_node("filt"));
  auto* sink = dynamic_cast<BufferConsumer*>(flat.find_node("sink"));
  if (!src_ptr || !filt_ptr || !sink) {
    throw std::runtime_error("bench_parallel_scale: flat nodes missing");
  }
  src_ptr->set_data(make_input(n));
  filt_ptr->mode = ParallelMode::OnCollection;
  filt_ptr->grain_size = grain;
  filt_ptr->burn_iters = burn;
  flat.find_node("filt")->find_input("gain")->literal = gain;

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

  double ms_total =
      std::chrono::duration<double, std::milli>(t1 - t0).count();
  double ms = ms_total / static_cast<double>(timed);

  // Bytes touched roughly: producer out copy + filter read + filter write + sink copy.
  // Report filter-centric: 2 * N * sizeof(double) per tick (in+out).
  double bytes = 2.0 * static_cast<double>(n) * static_cast<double>(sizeof(double));
  double gb_s = (bytes / 1e9) / (ms / 1000.0);

  BenchResult r;
  r.workers = engine.scheduler().worker_threads();
  r.n = n;
  r.grain = grain;
  r.burn = burn;
  r.timed_ticks = timed;
  r.ms_per_tick = ms;
  r.gb_per_s = gb_s;
  r.checksum = checksum_of(sink->last());
  return r;
}

void print_header() {
  std::cout << std::left << std::setw(8) << "workers" << std::setw(12) << "N"
            << std::setw(10) << "grain" << std::setw(8) << "burn" << std::setw(12) << "ms/tick"
            << std::setw(12) << "GB/s" << std::setw(10) << "speedup" << std::setw(14) << "checksum"
            << '\n';
  std::cout << std::string(86, '-') << '\n';
}

void print_row(BenchResult const& r) {
  std::cout << std::left << std::setw(8) << r.workers << std::setw(12) << r.n << std::setw(10)
            << r.grain << std::setw(8) << r.burn << std::setw(12) << std::fixed
            << std::setprecision(3) << r.ms_per_tick << std::setw(12) << std::setprecision(2)
            << r.gb_per_s << std::setw(10) << std::setprecision(2) << r.speedup << std::setw(14)
            << std::setprecision(4) << std::scientific << r.checksum << std::defaultfloat
            << '\n';
}

}  // namespace

int main() {
  using namespace node_engine;

  std::size_t hw = std::max<std::size_t>(1, std::thread::hardware_concurrency());
  std::cout << "bench_parallel_scale\n";
  std::cout << "hardware_concurrency=" << hw << '\n';
  std::cout << "session model: Engine{workers} kept for each config; Taskflow reused\n";
  std::cout << "graph: BufferSource -> ParallelScaleFilter -> BufferConsumer\n";
  std::cout << "note: GB/s uses 2*N*8 bytes (filter in+out only); edge copies add overhead\n\n";

  std::vector<std::size_t> workers_list = {1, 2};
  if (hw >= 4) {
    workers_list.push_back(4);
  }
  if (hw > 4 && hw != 4 && hw != 2) {
    workers_list.push_back(hw);
  } else if (hw > 4) {
    // already have 4; add hw if distinct
  }
  // Ensure unique sorted
  std::sort(workers_list.begin(), workers_list.end());
  workers_list.erase(std::unique(workers_list.begin(), workers_list.end()), workers_list.end());
  if (hw > 4 && std::find(workers_list.begin(), workers_list.end(), hw) == workers_list.end()) {
    workers_list.push_back(hw);
  }

  std::vector<std::size_t> grains = {0, 256, 1024, 4096, 16384};  // 0 = scheduler default
  std::vector<std::size_t> sizes = {1u << 16, 1u << 20};          // 64Ki, 1Mi elements
  if (hw >= 4) {
    sizes.push_back(1u << 21);  // 2Mi on bigger machines
  }
  std::vector<std::size_t> burns = {1, 8};  // light vs more arithmetic
  constexpr double kGain = 1.5;
  constexpr int kWarmup = 3;
  constexpr int kTimed = 8;

  print_header();

  // baseline_ms[(n<<32)|(grain<<16)|burn] for workers==1 — simple nested key via map of tuple
  struct Key {
    std::size_t n, grain, burn;
    bool operator==(Key const& o) const {
      return n == o.n && grain == o.grain && burn == o.burn;
    }
  };
  struct KeyHash {
    std::size_t operator()(Key const& k) const {
      return k.n ^ (k.grain << 1) ^ (k.burn << 11);
    }
  };
  std::unordered_map<Key, double, KeyHash> baseline_ms;

  int failures = 0;
  double ref_checksum = 0.0;
  bool have_ref = false;

  try {
    for (std::size_t burn : burns) {
      for (std::size_t n : sizes) {
        for (std::size_t grain : grains) {
          // Skip absurd tiny grains on huge N? keep all for insight.
          for (std::size_t w : workers_list) {
            BenchResult r = run_config(w, n, grain, burn, kGain, kWarmup, kTimed);
            Key key{n, grain, burn};
            if (w == 1) {
              baseline_ms[key] = r.ms_per_tick;
              r.speedup = 1.0;
            } else {
              auto it = baseline_ms.find(key);
              r.speedup = (it != baseline_ms.end() && it->second > 0.0)
                              ? (it->second / r.ms_per_tick)
                              : 0.0;
            }

            if (!have_ref) {
              ref_checksum = r.checksum;
              have_ref = true;
            } else {
              // Same N/burn/gain must match; grain/workers must not change results.
              Key ref_key = key;
              (void)ref_key;
              // Compare only when same n and burn (gain fixed).
              static std::unordered_map<Key, double, KeyHash> checksums;
              auto [it, inserted] = checksums.emplace(key, r.checksum);
              if (!inserted) {
                double a = it->second;
                double b = r.checksum;
                if (std::abs(a - b) > 1e-6 * std::max(1.0, std::abs(a))) {
                  std::cerr << "CHECKSUM MISMATCH workers=" << w << " N=" << n
                            << " grain=" << grain << " burn=" << burn << " a=" << a
                            << " b=" << b << '\n';
                  ++failures;
                }
              }
            }

            print_row(r);
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
    std::cerr << "bench_parallel_scale FAILED checksums=" << failures << '\n';
    return 1;
  }
  std::cout << "bench_parallel_scale ok\n";
  return 0;
}
