#include "node_engine/converter_registry.hpp"
#include "node_engine/engine.hpp"
#include "node_engine/factory.hpp"
#include "node_engine/graph.hpp"
#include "node_engine/graph_node.hpp"
#include "node_engine/type_registry.hpp"
#include "node_engine/value.hpp"

#include "boundary.hpp"
#include "buffer_to_stream.hpp"
#include "chunk_coalescer.hpp"
#include "examples/arm_then_go.hpp"
#include "examples/producer_consumer.hpp"
#include "examples/scale_filter.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>

namespace {

int fail(char const* msg) {
  std::cerr << "FAIL: " << msg << '\n';
  return EXIT_FAILURE;
}

using namespace node_engine;

struct SensorSample {
  double t_s = 0;
  double value = 0;
};

struct SampleBatch {
  std::vector<double> values;
};

struct OtherStruct {
  int x = 0;
};

struct SampleSource : NodeBase<SampleSource> {
  static constexpr Meta meta{.type_id = "test.sample_source", .role = Role::Producer};
  static void ports(Ports& p) { p.out<SensorSample>("out"); }
  void compute(Slice) override {
    set_out("out", SensorSample{1.5, 3.0});
    suppress();
  }
};

struct BatchSink : NodeBase<BatchSink> {
  static constexpr Meta meta{.type_id = "test.batch_sink", .role = Role::Consumer};
  SampleBatch last{};
  static void ports(Ports& p) { p.in<SampleBatch>("in"); }
  void compute(Slice) override {
    last = get<SampleBatch>("in");
    suppress();
  }
};

struct OtherSink : NodeBase<OtherSink> {
  static constexpr Meta meta{.type_id = "test.other_sink", .role = Role::Consumer};
  static void ports(Ports& p) { p.in<OtherStruct>("in"); }
  void compute(Slice) override { suppress(); }
};

struct IntSource : NodeBase<IntSource> {
  static constexpr Meta meta{.type_id = "test.int_source", .role = Role::Producer};
  static void ports(Ports& p) { p.out<std::int32_t>("out"); }
  void compute(Slice) override {
    set_out<std::int32_t>("out", 7);
    suppress();
  }
};

struct IntSink : NodeBase<IntSink> {
  static constexpr Meta meta{.type_id = "test.int_sink", .role = Role::Consumer};
  std::int32_t last = 0;
  static void ports(Ports& p) { p.in<std::int32_t>("in"); }
  void compute(Slice) override {
    last = get<std::int32_t>("in");
    suppress();
  }
};

}  // namespace

int main() {
  using namespace node_engine;
  using namespace node_engine::nodes;

  // Value / builtin converters (Bool↔Float64; Float alias = Float64)
  Value v = 1.0;
  if (!std::holds_alternative<double>(v)) {
    return fail("Value variant");
  }
  if (!can_autoconvert(TypeId::Bool, TypeId::Float) ||
      !can_autoconvert(TypeId::Bool, TypeId::Float64)) {
    return fail("autoconvert table");
  }
  Value b = autoconvert(Value{true}, TypeId::Float);
  if (std::get<double>(b) != 1.0) {
    return fail("bool->float convert");
  }
  if (!can_autoconvert(TypeId::Float32, TypeId::Float64)) {
    return fail("float32->float64 converter missing");
  }
  Value f32 = autoconvert(Value{2.5f}, TypeId::Float64);
  if (std::get<double>(f32) != 2.5) {
    return fail("float32->float64 convert");
  }
  // Fail-closed: no int8→uint8 builtin
  if (can_autoconvert(TypeId::Int8, TypeId::Uint8)) {
    return fail("int8->uint8 should not autoconvert");
  }

  // Factory registration
  auto& factory = NodeFactory::instance();
  if (!factory.contains("scale_filter") || !factory.contains("sequence_producer") ||
      !factory.contains("arm_then_go") || !factory.contains("buffer_to_stream")) {
    return fail("REGISTER_NODE missing types");
  }
  auto n = factory.create("scale_filter");
  if (!n || n->type_id() != "scale_filter") {
    return fail("factory create");
  }

  Engine engine{2};

  // Linear wire + parallel scale
  {
    Graph g;
    g.add_node("p", std::make_unique<SequenceProducer>());
    g.add_node("f", std::make_unique<ScaleFilter>());
    g.add_node("c", std::make_unique<BufferConsumer>());
    g.find_node("p")->find_input("count")->literal = 128.0;
    g.find_node("f")->find_input("gain")->literal = 2.0;
    g.connect("p", "out", "f", "in");
    g.connect("f", "out", "c", "in");

    auto errs = validate_graph(g);
    if (!errs.empty()) {
      return fail(errs.front().message.c_str());
    }

    FlatGraph flat = engine.run_once(g);
    auto* c = dynamic_cast<BufferConsumer*>(flat.find_node("c"));
    if (!c || c->last().size() != 128 || c->last()[3] != 6.0) {
      return fail("linear pipeline values");
    }
  }

  // Two GraphNode instances — independent clone state
  {
    auto bp = std::make_shared<Graph>();
    bp->add_node("in", make_boundary_in(TypeId::FloatBuffer));
    bp->add_node("s", std::make_unique<ScaleFilter>());
    bp->add_node("out", make_boundary_out(TypeId::FloatBuffer));
    bp->find_node("s")->find_input("gain")->literal = 5.0;
    bp->connect("in", "out", "s", "in");
    bp->connect("s", "out", "out", "in");

    Graph outer;
    outer.add_node("src", std::make_unique<SequenceProducer>());
    outer.add_node("g0", std::make_unique<GraphNode>(bp, "g0"));
    outer.add_node("g1", std::make_unique<GraphNode>(bp, "g1"));
    outer.add_node("c0", std::make_unique<BufferConsumer>());
    outer.add_node("c1", std::make_unique<BufferConsumer>());
    outer.find_node("src")->find_input("count")->literal = 4.0;
    outer.connect("src", "out", "g0", "in");
    outer.connect("src", "out", "g1", "in");
    outer.connect("g0", "out", "c0", "in");
    outer.connect("g1", "out", "c1", "in");

    FlatGraph flat = engine.compile(outer);
    // No wrappers/boundaries remain.
    for (auto const& named : flat.nodes) {
      if (named.node->role() == Role::GraphWrapper || named.node->role() == Role::BoundaryIn ||
          named.node->role() == Role::BoundaryOut) {
        return fail("flatten left nesting artifacts");
      }
    }
    // Two independent scale nodes
    int scales = 0;
    for (auto const& named : flat.nodes) {
      if (named.node->type_id() == "scale_filter") {
        ++scales;
      }
    }
    if (scales != 2) {
      return fail("expected 2 cloned scale_filter instances");
    }

    for (auto& named : flat.nodes) {
      named.node->dirty = true;
    }
    engine.tick(flat);
    auto* c0 = dynamic_cast<BufferConsumer*>(flat.find_node("c0"));
    auto* c1 = dynamic_cast<BufferConsumer*>(flat.find_node("c1"));
    if (!c0 || !c1 || c0->last().size() != 4 || c1->last()[1] != 5.0) {
      return fail("nested instance values");
    }
  }

  // BufferToStream sub-ticks
  {
    Graph g;
    g.add_node("p", std::make_unique<SequenceProducer>());
    g.add_node("d", std::make_unique<BufferToStream>());
    g.add_node("c", std::make_unique<ScalarConsumer>());
    g.find_node("p")->find_input("count")->literal = 3.0;
    g.connect("p", "out", "d", "chunk");
    g.connect("d", "sample", "c", "in");
    FlatGraph flat = engine.run_once(g);
    auto* c = dynamic_cast<ScalarConsumer*>(flat.find_node("c"));
    // After full drain, last sample should be 2.
    if (!c || !c->has_value() || c->last() != 2.0) {
      return fail("buffer_to_stream drain");
    }
  }

  // ArmThenGo temporal
  {
    Graph g;
    g.add_node("a", std::make_unique<ArmThenGo>());
    g.add_node("c", std::make_unique<ScalarConsumer>());
    g.connect("a", "value", "c", "in");
    FlatGraph flat = engine.compile(g);
    auto* arm = flat.find_node("a");
    arm->find_input("payload")->literal = 42.0;

    auto tick_with = [&](bool arm_v, bool go_v) {
      arm->find_input("arm")->literal = arm_v;
      arm->find_input("go")->literal = go_v;
      arm->dirty = true;
      engine.tick(flat);
    };

    tick_with(false, false);
    tick_with(true, false);   // arm rising
    tick_with(true, true);    // go rising while armed -> fire
    auto* c = dynamic_cast<ScalarConsumer*>(flat.find_node("c"));
    if (!c || c->last() != 42.0) {
      return fail("arm_then_go fire");
    }
  }

  // Trigger queue
  {
    Graph g;
    g.add_node("p", std::make_unique<SequenceProducer>());
    g.add_node("c", std::make_unique<BufferConsumer>());
    g.find_node("p")->find_input("count")->literal = 2.0;
    g.connect("p", "out", "c", "in");
    FlatGraph flat = engine.compile(g);
    for (auto& n : flat.nodes) {
      n.node->dirty = false;
    }
    engine.triggers().push("p");
    if (!engine.poll_trigger_and_run(flat)) {
      return fail("trigger poll");
    }
    auto* c = dynamic_cast<BufferConsumer*>(flat.find_node("c"));
    if (!c || c->last().size() != 2) {
      return fail("trigger run");
    }
  }

  // Object struct pins + direct converter (no multi-hop)
  {
    register_type<SensorSample>("test.SensorSample");
    register_type<SampleBatch>("test.SampleBatch");
    register_type<OtherStruct>("test.OtherStruct");
    register_converter<SensorSample, SampleBatch>(
        "test.sample_to_batch", [](SensorSample const& in, SampleBatch& out) {
          out.values = {in.t_s, in.value};
        });

    Graph ok;
    ok.add_node("src", std::make_unique<SampleSource>());
    ok.add_node("snk", std::make_unique<BatchSink>());
    ok.connect("src", "out", "snk", "in");
    auto ok_errs = validate_graph(ok);
    if (!ok_errs.empty()) {
      return fail(ok_errs.front().message.c_str());
    }
    FlatGraph flat = engine.run_once(ok);
    auto* snk = dynamic_cast<BatchSink*>(flat.find_node("snk"));
    if (!snk || snk->last.values.size() != 2 || snk->last.values[0] != 1.5 ||
        snk->last.values[1] != 3.0) {
      return fail("object converter pipeline");
    }

    // No direct converter SensorSample → OtherStruct
    Graph bad;
    bad.add_node("src", std::make_unique<SampleSource>());
    bad.add_node("snk", std::make_unique<OtherSink>());
    bad.connect("src", "out", "snk", "in");
    auto bad_errs = validate_graph(bad);
    if (bad_errs.empty()) {
      return fail("expected incompatible object wire error");
    }
  }

  // Int32 pin roundtrip via identity
  {
    Graph g;
    g.add_node("s", std::make_unique<IntSource>());
    g.add_node("k", std::make_unique<IntSink>());
    g.connect("s", "out", "k", "in");
    FlatGraph flat = engine.run_once(g);
    auto* k = dynamic_cast<IntSink*>(flat.find_node("k"));
    if (!k || k->last != 7) {
      return fail("int32 identity wire");
    }
  }

  std::cout << "smoke_test ok\n";
  return EXIT_SUCCESS;
}
