#include "node_engine/converter_registry.hpp"
#include "node_engine/engine.hpp"
#include "node_engine/graph.hpp"
#include "node_engine/node.hpp"
#include "node_engine/type_registry.hpp"
#include "node_engine/value.hpp"

#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

using namespace node_engine;

struct SensorSample {
  double t_s = 0;
  double value = 0;
};

struct SampleBatch {
  std::vector<double> values;
};

struct SampleSource : NodeBase<SampleSource> {
  static constexpr Meta meta{.type_id = "demo.sample_source", .role = Role::Producer};
  static void ports(Ports& p) { p.out<SensorSample>("out"); }
  void compute(Slice) override {
    set_out("out", SensorSample{0.25, 10.0});
    suppress();
  }
};

struct BatchSink : NodeBase<BatchSink> {
  static constexpr Meta meta{.type_id = "demo.batch_sink", .role = Role::Consumer};
  SampleBatch last{};
  static void ports(Ports& p) { p.in<SampleBatch>("in"); }
  void compute(Slice) override {
    last = get<SampleBatch>("in");
    suppress();
  }
};

struct Float32Source : NodeBase<Float32Source> {
  static constexpr Meta meta{.type_id = "demo.f32_source", .role = Role::Producer};
  static void ports(Ports& p) { p.out<float>("out"); }
  void compute(Slice) override {
    set_out<float>("out", 1.5f);
    suppress();
  }
};

struct Float64Sink : NodeBase<Float64Sink> {
  static constexpr Meta meta{.type_id = "demo.f64_sink", .role = Role::Consumer};
  double last = 0;
  static void ports(Ports& p) { p.in<double>("in"); }
  void compute(Slice) override {
    last = get<double>("in");
    suppress();
  }
};

struct Unrelated {
  int n = 0;
};

struct UnrelatedSink : NodeBase<UnrelatedSink> {
  static constexpr Meta meta{.type_id = "demo.unrelated_sink", .role = Role::Consumer};
  static void ports(Ports& p) { p.in<Unrelated>("in"); }
  void compute(Slice) override { suppress(); }
};

}  // namespace

int main() {
  using namespace node_engine;

  std::cout << "typed pins + direct converters demo\n";

  register_type<SensorSample>("demo.SensorSample");
  register_type<SampleBatch>("demo.SampleBatch");
  register_type<Unrelated>("demo.Unrelated");
  register_converter<SensorSample, SampleBatch>(
      "demo.sample_to_batch", [](SensorSample const& in, SampleBatch& out) {
        out.values = {in.t_s, in.value * 2.0};
      });

  Engine engine{2};

  // Struct A -> B via registered direct converter (applied on edge copy).
  {
    Graph g;
    g.add_node("src", std::make_unique<SampleSource>());
    g.add_node("snk", std::make_unique<BatchSink>());
    g.connect("src", "out", "snk", "in");
    auto errs = validate_graph(g);
    if (!errs.empty()) {
      std::cerr << "validate failed: " << errs.front().message << '\n';
      return 1;
    }
    FlatGraph flat = engine.run_once(g);
    auto* snk = dynamic_cast<BatchSink*>(flat.find_node("snk"));
    if (!snk || snk->last.values.size() != 2) {
      std::cerr << "object convert pipeline failed\n";
      return 1;
    }
    std::cout << "SensorSample -> SampleBatch: t=" << snk->last.values[0]
              << " 2*value=" << snk->last.values[1] << '\n';
  }

  // Builtin Float32 -> Float64 converter.
  {
    Graph g;
    g.add_node("src", std::make_unique<Float32Source>());
    g.add_node("snk", std::make_unique<Float64Sink>());
    g.connect("src", "out", "snk", "in");
    FlatGraph flat = engine.run_once(g);
    auto* snk = dynamic_cast<Float64Sink*>(flat.find_node("snk"));
    if (!snk || snk->last != 1.5) {
      std::cerr << "float32->float64 failed\n";
      return 1;
    }
    std::cout << "Float32 -> Float64: " << snk->last << '\n';
  }

  // Fail-closed: missing direct pair rejects at validate (no multi-hop).
  {
    Graph g;
    g.add_node("src", std::make_unique<SampleSource>());
    g.add_node("snk", std::make_unique<UnrelatedSink>());
    g.connect("src", "out", "snk", "in");
    auto errs = validate_graph(g);
    if (errs.empty()) {
      std::cerr << "expected type mismatch without direct converter\n";
      return 1;
    }
    std::cout << "expected reject: " << errs.front().message << '\n';
  }

  std::cout << "demo ok\n";
  return 0;
}
