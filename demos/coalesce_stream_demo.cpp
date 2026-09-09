// Demo 1/6 — FixedChunkCoalescer under a session Engine.
//
// Session pattern (API consumer owns lifetime):
//   Engine lives for the whole feed loop (reuses tf::Executor).
//   compile() once; tick() per scalar sample.
//
// Graph: ScalarProducer -> FixedChunkCoalescer -> BufferConsumer
// Feed 1..N samples; chunk emits only when full.

#include "node_engine/engine.hpp"
#include "node_engine/graph.hpp"

#include "chunk_coalescer.hpp"
#include "examples/producer_consumer.hpp"

#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

int fail(char const* msg) {
  std::cerr << "FAIL: " << msg << '\n';
  return 1;
}

bool ready_flag(node_engine::Node& coalescer) {
  auto* pin = coalescer.find_output("ready");
  if (!pin || !std::holds_alternative<bool>(pin->buffer)) {
    return false;
  }
  return std::get<bool>(pin->buffer);
}

}  // namespace

int main() {
  using namespace node_engine;
  using namespace node_engine::nodes;

  constexpr std::size_t kChunk = 8;
  constexpr std::size_t kWorkers = 2;

  // --- session setup (keep alive for the whole run) ---
  Engine engine{kWorkers};
  std::cout << "coalesce_stream_demo\n";
  std::cout << "session workers: " << engine.scheduler().worker_threads()
            << " (executor reused across ticks)\n";
  std::cout << "chunk_size: " << kChunk << '\n';

  Graph g;
  g.add_node("src", std::make_unique<ScalarProducer>());
  g.add_node("coal", std::make_unique<FixedChunkCoalescer>());
  g.add_node("sink", std::make_unique<BufferConsumer>());
  g.find_node("coal")->find_input("chunk_size")->literal = static_cast<double>(kChunk);
  g.connect("src", "out", "coal", "sample");
  g.connect("coal", "chunk", "sink", "in");

  FlatGraph flat = engine.compile(g);
  Node* src = flat.find_node("src");
  Node* coal = flat.find_node("coal");
  auto* sink = dynamic_cast<BufferConsumer*>(flat.find_node("sink"));
  if (!src || !coal || !sink) {
    return fail("missing flat nodes");
  }

  std::size_t emit_count = 0;
  std::vector<double> last_good;

  // Feed one scalar per tick — topology stays compiled.
  for (std::size_t i = 0; i < kChunk; ++i) {
    double sample = static_cast<double>(i + 1);  // 1..kChunk
    src->find_input("value")->literal = sample;
    engine.mark_dirty_reachable(flat, "src");
    engine.tick(flat);

    bool ready = ready_flag(*coal);
    auto const& buf = sink->last();

    std::cout << "tick " << (i + 1) << " sample=" << sample << " ready=" << (ready ? "true" : "false")
              << " sink_n=" << buf.size();
    if (!buf.empty()) {
      std::cout << " sink=[" << buf.front() << ".." << buf.back() << "]";
    }
    std::cout << '\n';

    if (i + 1 < kChunk) {
      if (ready) {
        return fail("ready should stay false until chunk is full");
      }
      if (!buf.empty()) {
        return fail("sink should stay empty until first full chunk");
      }
    } else {
      if (!ready) {
        return fail("ready should be true on full chunk");
      }
      if (buf.size() != kChunk) {
        return fail("sink size != chunk_size");
      }
      for (std::size_t k = 0; k < kChunk; ++k) {
        if (buf[k] != static_cast<double>(k + 1)) {
          return fail("chunk contents mismatch");
        }
      }
      last_good = buf;
      ++emit_count;
    }
  }

  // Second chunk: feed another full set; prove coalescer state survives session.
  for (std::size_t i = 0; i < kChunk; ++i) {
    double sample = static_cast<double>(100 + i);
    src->find_input("value")->literal = sample;
    engine.mark_dirty_reachable(flat, "src");
    engine.tick(flat);
  }
  if (!ready_flag(*coal)) {
    return fail("second chunk not ready");
  }
  auto const& second = sink->last();
  if (second.size() != kChunk || second.front() != 100.0 || second.back() != 100.0 + (kChunk - 1)) {
    return fail("second chunk contents");
  }
  ++emit_count;

  // Same executor object identity across the session (sanity).
  auto exec_a = engine.scheduler().shared_executor();
  Engine engine_other{exec_a};  // consumer can share the pool with another façade
  if (engine_other.scheduler().shared_executor().get() != exec_a.get()) {
    return fail("shared executor injection broken");
  }

  std::cout << "emits: " << emit_count << " (expected 2)\n";
  std::cout << "second chunk:";
  for (double v : second) {
    std::cout << ' ' << v;
  }
  std::cout << "\ncoalesce_stream_demo ok\n";
  return 0;
}
