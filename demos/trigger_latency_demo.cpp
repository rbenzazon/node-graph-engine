// Demo 5/6 — trigger_latency_demo
//
// Session Engine + TriggerQueue:
//   producers push node ids; main loop poll_trigger_and_run / wait_pop.
//
// Graph: SequenceProducer -> ScaleFilter -> BufferConsumer
//
// Insight: inbound trigger wake cost, FIFO ordering, empty-queue behavior,
// and a cross-thread push + wait_pop handoff on the session executor.

#include "node_engine/engine.hpp"
#include "node_engine/graph.hpp"

#include "examples/producer_consumer.hpp"
#include "examples/scale_filter.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

using clock = std::chrono::steady_clock;

int fail(char const* msg) {
  std::cerr << "FAIL: " << msg << '\n';
  return 1;
}

double percentile_us(std::vector<double> sorted, double p) {
  if (sorted.empty()) {
    return 0.0;
  }
  if (sorted.size() == 1) {
    return sorted.front();
  }
  double idx = p * static_cast<double>(sorted.size() - 1);
  std::size_t lo = static_cast<std::size_t>(idx);
  std::size_t hi = std::min(lo + 1, sorted.size() - 1);
  double frac = idx - static_cast<double>(lo);
  return sorted[lo] * (1.0 - frac) + sorted[hi] * frac;
}

void print_stats(char const* label, std::vector<double> samples_us) {
  if (samples_us.empty()) {
    std::cout << label << ": (no samples)\n";
    return;
  }
  std::sort(samples_us.begin(), samples_us.end());
  double sum = std::accumulate(samples_us.begin(), samples_us.end(), 0.0);
  double mean = sum / static_cast<double>(samples_us.size());
  std::cout << std::fixed << std::setprecision(1);
  std::cout << label << ": n=" << samples_us.size()
            << " min=" << samples_us.front() << " us"
            << " p50=" << percentile_us(samples_us, 0.50) << " us"
            << " p90=" << percentile_us(samples_us, 0.90) << " us"
            << " p99=" << percentile_us(samples_us, 0.99) << " us"
            << " max=" << samples_us.back() << " us"
            << " mean=" << mean << " us\n";
  std::cout << std::defaultfloat;
}

}  // namespace

int main() {
  using namespace node_engine;
  using namespace node_engine::nodes;

  constexpr std::size_t kWorkers = 2;
  constexpr double kCount = 8.0;
  constexpr double kGain = 2.0;
  constexpr int kWarmup = 20;
  constexpr int kTimed = 200;
  constexpr int kBurst = 32;
  constexpr int kAsyncPushes = 64;

  Engine engine{kWorkers};
  std::cout << "trigger_latency_demo\n";
  std::cout << "session workers: " << engine.scheduler().worker_threads() << '\n';
  std::cout << "graph: SequenceProducer -> ScaleFilter -> BufferConsumer\n";
  std::cout << "path: triggers().push(src) + poll_trigger_and_run / wait_pop\n\n";

  Graph g;
  g.add_node("src", std::make_unique<SequenceProducer>());
  g.add_node("filt", std::make_unique<ScaleFilter>());
  g.add_node("sink", std::make_unique<BufferConsumer>());
  g.find_node("src")->find_input("count")->literal = kCount;
  g.find_node("filt")->find_input("gain")->literal = kGain;
  g.connect("src", "out", "filt", "in");
  g.connect("filt", "out", "sink", "in");

  FlatGraph flat = engine.compile(g);
  Node* src = flat.find_node("src");
  auto* sink = dynamic_cast<BufferConsumer*>(flat.find_node("sink"));
  if (!src || !sink) {
    return fail("missing flat nodes");
  }
  // Re-bind literals after clone.
  src->find_input("count")->literal = kCount;
  flat.find_node("filt")->find_input("gain")->literal = kGain;

  // --- empty queue ---
  if (!engine.triggers().empty()) {
    return fail("queue should start empty");
  }
  if (engine.poll_trigger_and_run(flat)) {
    return fail("poll on empty queue should return false");
  }
  std::cout << "empty poll: ok (false)\n";

  // --- single trigger correctness ---
  for (auto& n : flat.nodes) {
    if (n.node) {
      n.node->dirty = false;
    }
  }
  sink->last();  // ensure object live
  engine.triggers().push("src");
  if (engine.triggers().empty()) {
    return fail("queue should be non-empty after push");
  }
  if (!engine.poll_trigger_and_run(flat)) {
    return fail("poll should consume one trigger");
  }
  if (!engine.triggers().empty()) {
    return fail("queue should be empty after one poll");
  }
  if (sink->last().size() != static_cast<std::size_t>(kCount)) {
    return fail("sink size after first trigger");
  }
  // sequence [0..7] * gain 2 => 0,2,...,14
  if (sink->last().front() != 0.0 || sink->last().back() != 14.0) {
    return fail("sink values after first trigger");
  }
  std::cout << "single trigger: sink n=" << sink->last().size()
            << " first=" << sink->last().front() << " last=" << sink->last().back() << " ok\n";

  // --- FIFO ordering of distinct seeds (src vs filt) ---
  // Push filt then src; first poll should run filt cone only if we could observe
  // order via try_pop. Directly verify queue order with try_pop on a side queue pattern:
  engine.triggers().push("filt");
  engine.triggers().push("src");
  auto a = engine.triggers().try_pop();
  auto b = engine.triggers().try_pop();
  if (!a || !b || *a != "filt" || *b != "src") {
    return fail("FIFO order broken");
  }
  std::cout << "FIFO try_pop: filt then src ok\n";

  // --- latency: push + poll_trigger_and_run ---
  auto one_trigger_tick = [&]() {
    for (auto& n : flat.nodes) {
      if (n.node) {
        n.node->dirty = false;
      }
    }
    engine.triggers().push("src");
    if (!engine.poll_trigger_and_run(flat)) {
      throw std::runtime_error("poll failed in timed loop");
    }
  };

  for (int i = 0; i < kWarmup; ++i) {
    one_trigger_tick();
  }

  std::vector<double> lat_us;
  lat_us.reserve(static_cast<std::size_t>(kTimed));
  for (int i = 0; i < kTimed; ++i) {
    for (auto& n : flat.nodes) {
      if (n.node) {
        n.node->dirty = false;
      }
    }
    auto t0 = clock::now();
    engine.triggers().push("src");
    if (!engine.poll_trigger_and_run(flat)) {
      return fail("timed poll failed");
    }
    auto t1 = clock::now();
    lat_us.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
  }
  if (sink->last().size() != static_cast<std::size_t>(kCount)) {
    return fail("sink size after latency loop");
  }
  print_stats("push+poll_trigger_and_run", lat_us);

  // --- burst: enqueue many, drain with poll loop ---
  for (auto& n : flat.nodes) {
    if (n.node) {
      n.node->dirty = false;
    }
  }
  auto b0 = clock::now();
  for (int i = 0; i < kBurst; ++i) {
    engine.triggers().push("src");
  }
  int drained = 0;
  while (engine.poll_trigger_and_run(flat)) {
    ++drained;
  }
  auto b1 = clock::now();
  if (drained != kBurst) {
    std::cerr << "drained=" << drained << " expected=" << kBurst << '\n';
    return fail("burst drain count");
  }
  double burst_ms = std::chrono::duration<double, std::milli>(b1 - b0).count();
  std::cout << std::fixed << std::setprecision(3);
  std::cout << "burst push " << kBurst << " + drain: " << burst_ms << " ms ("
            << (burst_ms * 1000.0 / static_cast<double>(kBurst)) << " us/trig avg)\n";
  std::cout << std::defaultfloat;

  // --- async: worker thread pushes; main wait_pop + mark + tick ---
  std::vector<double> handoff_us;
  handoff_us.reserve(static_cast<std::size_t>(kAsyncPushes));
  std::thread producer([&engine]() {
    for (int i = 0; i < kAsyncPushes; ++i) {
      // Small stagger so wait_pop actually blocks some of the time.
      if ((i % 7) == 0) {
        std::this_thread::sleep_for(std::chrono::microseconds(50));
      }
      engine.triggers().push("src");
    }
  });

  for (int i = 0; i < kAsyncPushes; ++i) {
    auto t0 = clock::now();
    std::string id = engine.triggers().wait_pop();
    if (id != "src") {
      producer.join();
      return fail("wait_pop id");
    }
    engine.mark_dirty_reachable(flat, id);
    engine.tick(flat);
    auto t1 = clock::now();
    handoff_us.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
  }
  producer.join();
  if (!engine.triggers().empty()) {
    return fail("queue not empty after async drain");
  }
  if (sink->last().size() != static_cast<std::size_t>(kCount) || sink->last().back() != 14.0) {
    return fail("async sink values");
  }
  print_stats("wait_pop+mark+tick (async producer)", handoff_us);

  // Final empty poll
  if (engine.poll_trigger_and_run(flat)) {
    return fail("final empty poll should be false");
  }

  std::cout << "\ntrigger_latency_demo ok\n";
  return 0;
}
