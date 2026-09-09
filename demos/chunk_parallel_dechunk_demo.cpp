// Demo 4/6 — chunk_parallel_dechunk_demo
//
// Session Engine: compile once, feed scalars one tick at a time.
//
// Graph:
//   ScalarProducer
//     -> FixedChunkCoalescer          (scalar stream → fixed FloatBuffer)
//     -> ParallelScaleFilter          (OnCollection parallel on full chunks)
//     -> BufferToStream               (buffer → one scalar per sub-tick)
//     -> StreamRecorder               (append each dechunked sample)
//
// Insight: temporal coalesce + intra-node parallel on the chunk + de-chunk
// drain via scheduler sub-ticks, all on one session executor.

#include "node_engine/engine.hpp"
#include "node_engine/graph.hpp"
#include "node_engine/node.hpp"

#include "buffer_to_stream.hpp"
#include "chunk_coalescer.hpp"
#include "examples/parallel_scale_filter.hpp"
#include "examples/producer_consumer.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

using namespace node_engine;
using namespace node_engine::nodes;

// Demo-local sink: appends a sample only when gate (BufferToStream.active) is true.
// Needed because the scheduler dirty-cone re-runs the whole chain on every src tick;
// without a gate, the last stream sample would be re-appended during coalesce mid-fill.
class StreamRecorder : public NodeBase<StreamRecorder> {
 public:
  static constexpr Meta meta{.type_id = "stream_recorder", .role = Role::Consumer};

  static void ports(Ports& p) {
    p.in<double>("in");
    p.in<bool>("gate").literal(false);
  }

  void compute(Slice) override {
    if (get<bool>("gate")) {
      samples_.push_back(get<double>("in"));
    }
    suppress();
  }

  std::vector<double> const& samples() const { return samples_; }
  void clear() { samples_.clear(); }

 private:
  std::vector<double> samples_;
};

int fail(char const* msg) {
  std::cerr << "FAIL: " << msg << '\n';
  return 1;
}

bool ready_flag(Node& coal) {
  auto* pin = coal.find_output("ready");
  if (!pin || !std::holds_alternative<bool>(pin->buffer)) {
    return false;
  }
  return std::get<bool>(pin->buffer);
}

bool active_flag(Node& stream) {
  auto* pin = stream.find_output("active");
  if (!pin || !std::holds_alternative<bool>(pin->buffer)) {
    return false;
  }
  return std::get<bool>(pin->buffer);
}

std::size_t chunk_len(Node& coal) {
  auto* pin = coal.find_output("chunk");
  if (!pin || !std::holds_alternative<std::vector<double>>(pin->buffer)) {
    return 0;
  }
  return std::get<std::vector<double>>(pin->buffer).size();
}

std::size_t filter_out_len(Node& filt) {
  auto* pin = filt.find_output("out");
  if (!pin || !std::holds_alternative<std::vector<double>>(pin->buffer)) {
    return 0;
  }
  return std::get<std::vector<double>>(pin->buffer).size();
}

}  // namespace

int main() {
  constexpr std::size_t kChunk = 8;
  constexpr std::size_t kWorkers = 4;
  constexpr std::size_t kGrain = 2;  // force multi-slice on 8-wide chunks
  constexpr std::size_t kBurn = 2;
  constexpr double kGain = 2.0;
  constexpr std::size_t kTotalSamples = kChunk * 2;  // two full chunks

  Engine engine{kWorkers};
  std::cout << "chunk_parallel_dechunk_demo\n";
  std::cout << "session workers: " << engine.scheduler().worker_threads()
            << " (executor reused; sub-ticks drain BufferToStream)\n";
  std::cout << "chunk_size=" << kChunk << " gain=" << kGain << " grain=" << kGrain
            << " burn=" << kBurn << '\n';
  std::cout << "graph: ScalarProducer -> FixedChunkCoalescer -> ParallelScaleFilter"
               " -> BufferToStream -> StreamRecorder\n\n";

  Graph g;
  g.add_node("src", std::make_unique<ScalarProducer>());
  g.add_node("coal", std::make_unique<FixedChunkCoalescer>());
  auto filt = std::make_unique<ParallelScaleFilter>();
  filt->mode = ParallelMode::OnCollection;
  filt->grain_size = kGrain;
  filt->burn_iters = kBurn;
  g.add_node("filt", std::move(filt));
  g.add_node("stream", std::make_unique<BufferToStream>());
  g.add_node("rec", std::make_unique<StreamRecorder>());

  g.find_node("coal")->find_input("chunk_size")->literal = static_cast<double>(kChunk);
  g.find_node("filt")->find_input("gain")->literal = kGain;

  g.connect("src", "out", "coal", "sample");
  g.connect("coal", "chunk", "filt", "in");
  g.connect("filt", "out", "stream", "chunk");
  g.connect("stream", "sample", "rec", "in");
  g.connect("stream", "active", "rec", "gate");

  FlatGraph flat = engine.compile(g);

  Node* src = flat.find_node("src");
  Node* coal = flat.find_node("coal");
  auto* filt_ptr = dynamic_cast<ParallelScaleFilter*>(flat.find_node("filt"));
  Node* stream = flat.find_node("stream");
  auto* rec = dynamic_cast<StreamRecorder*>(flat.find_node("rec"));
  if (!src || !coal || !filt_ptr || !stream || !rec) {
    return fail("missing flat nodes");
  }

  // Re-bind runtime knobs after clone.
  filt_ptr->mode = ParallelMode::OnCollection;
  filt_ptr->grain_size = kGrain;
  filt_ptr->burn_iters = kBurn;
  flat.find_node("filt")->find_input("gain")->literal = kGain;

  auto expected_scaled = [&](double sample) {
    // Match ParallelScaleFilter burn chain: x = x * gain + 1e-7, burn times.
    double x = sample;
    for (std::size_t b = 0; b < kBurn; ++b) {
      x = x * kGain + 0.0000001;
    }
    return x;
  };

  std::size_t chunks_emitted = 0;

  for (std::size_t i = 0; i < kTotalSamples; ++i) {
    double sample = static_cast<double>(i + 1);  // 1..16
    src->find_input("value")->literal = sample;
    engine.mark_dirty_reachable(flat, "src");
    engine.tick(flat);

    bool ready = ready_flag(*coal);
    bool active = active_flag(*stream);
    std::size_t c_len = chunk_len(*coal);
    std::size_t f_len = filter_out_len(*filt_ptr);
    std::size_t rec_n = rec->samples().size();

    std::cout << "tick " << (i + 1) << " sample=" << sample << " ready=" << (ready ? "true" : "false")
              << " coal_n=" << c_len << " filt_n=" << f_len << " stream_active=" << (active ? "true" : "false")
              << " recorded=" << rec_n << '\n';

    bool boundary = ((i + 1) % kChunk) == 0;
    if (!boundary) {
      if (ready) {
        return fail("ready true before chunk full");
      }
      if (c_len != 0) {
        return fail("coal chunk should be empty mid-fill");
      }
      // Recorded count should stay on the last completed chunk boundary.
      std::size_t expect_rec = chunks_emitted * kChunk;
      if (rec_n != expect_rec) {
        return fail("recorder grew mid-fill");
      }
    } else {
      if (!ready) {
        return fail("ready false on full chunk");
      }
      if (c_len != kChunk) {
        return fail("coal chunk size mismatch on emit");
      }
      if (f_len != kChunk) {
        return fail("filter out size mismatch on emit");
      }
      // One outer tick drains the whole scaled chunk via BufferToStream sub-ticks.
      // active stays true on the last emitted sample (recorder gate); the next
      // mid-fill tick clears it when the stream runs idle on an empty buffer.
      ++chunks_emitted;
      std::size_t expect_rec = chunks_emitted * kChunk;
      if (rec_n != expect_rec) {
        return fail("recorder size after chunk drain");
      }

      // Verify the just-drained window.
      std::size_t base = (chunks_emitted - 1) * kChunk;
      for (std::size_t k = 0; k < kChunk; ++k) {
        double raw = static_cast<double>(base + k + 1);
        double exp = expected_scaled(raw);
        double got = rec->samples()[base + k];
        if (std::abs(got - exp) > 1e-6 * std::max(1.0, std::abs(exp))) {
          std::cerr << "value mismatch at " << (base + k) << " raw=" << raw << " exp=" << exp
                    << " got=" << got << '\n';
          return fail("scaled dechunk value mismatch");
        }
      }
    }
  }

  if (chunks_emitted != 2) {
    return fail("expected exactly 2 chunk emits");
  }
  if (rec->samples().size() != kTotalSamples) {
    return fail("final recorder size");
  }

  // Spot-check endpoints.
  if (std::abs(rec->samples().front() - expected_scaled(1.0)) > 1e-9 ||
      std::abs(rec->samples().back() - expected_scaled(static_cast<double>(kTotalSamples))) > 1e-6) {
    return fail("endpoint scaled values");
  }

  std::cout << "\nrecorded " << rec->samples().size() << " scaled samples:";
  for (double v : rec->samples()) {
    std::cout << ' ' << v;
  }
  std::cout << "\nchunks_emitted=" << chunks_emitted << " (expected 2)\n";
  std::cout << "chunk_parallel_dechunk_demo ok\n";
  return 0;
}
