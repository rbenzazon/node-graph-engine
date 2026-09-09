// Demo 6/6 — nested_depth_validate_demo
//
// Insight: validate_graph rejects bad authoring; compile_flat splices nested
// GraphNode macros to a pure flat DAG (no wrappers/boundaries); deep nesting
// multiplies gains; sibling instances stay independent.

#include "node_engine/engine.hpp"
#include "node_engine/flatten.hpp"
#include "node_engine/graph.hpp"
#include "node_engine/graph_node.hpp"

#include "boundary.hpp"
#include "examples/producer_consumer.hpp"
#include "examples/scale_filter.hpp"

#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace node_engine;
using namespace node_engine::nodes;

int fail(char const* msg) {
  std::cerr << "FAIL: " << msg << '\n';
  return 1;
}

bool has_message_substr(std::vector<ValidationError> const& errs, std::string_view needle) {
  for (auto const& e : errs) {
    if (e.message.find(needle) != std::string::npos) {
      return true;
    }
  }
  return false;
}

// Shared blueprint: BoundaryIn -> ScaleFilter(gain) -> BoundaryOut
std::shared_ptr<Graph> make_scale_blueprint(double gain) {
  auto bp = std::make_shared<Graph>();
  bp->add_node("in", make_boundary_in(TypeId::FloatBuffer));
  bp->add_node("scale", std::make_unique<ScaleFilter>());
  bp->add_node("out", make_boundary_out(TypeId::FloatBuffer));
  bp->find_node("scale")->find_input("gain")->literal = gain;
  bp->connect("in", "out", "scale", "in");
  bp->connect("scale", "out", "out", "in");
  return bp;
}

// Blueprint that wraps another GraphNode (one nesting level of macro).
std::shared_ptr<Graph> make_wrap_blueprint(std::shared_ptr<Graph> inner_bp, std::string name,
                                           double /*unused*/) {
  auto bp = std::make_shared<Graph>();
  bp->add_node("in", make_boundary_in(TypeId::FloatBuffer));
  bp->add_node("child", std::make_unique<GraphNode>(std::move(inner_bp), std::move(name)));
  bp->add_node("out", make_boundary_out(TypeId::FloatBuffer));
  bp->connect("in", "out", "child", "in");
  bp->connect("child", "out", "out", "in");
  return bp;
}

void assert_no_nesting_artifacts(FlatGraph const& flat, char const* label) {
  for (auto const& named : flat.nodes) {
    if (!named.node) {
      throw std::runtime_error(std::string(label) + ": null node");
    }
    Role r = named.node->role();
    if (r == Role::GraphWrapper || r == Role::BoundaryIn || r == Role::BoundaryOut) {
      throw std::runtime_error(std::string(label) + ": nesting artifact " + named.id);
    }
  }
}

int count_type(FlatGraph const& flat, std::string_view type) {
  int n = 0;
  for (auto const& named : flat.nodes) {
    if (named.node && named.node->type_id() == type) {
      ++n;
    }
  }
  return n;
}

}  // namespace

int main() {
  std::cout << "nested_depth_validate_demo\n";
  std::cout << "insight: validate_graph + deep GraphNode flatten/splice\n\n";

  Engine engine{2};

  // -------------------------------------------------------------------------
  // A) validate_graph — expected failures
  // -------------------------------------------------------------------------
  std::cout << "== validate_graph failures ==\n";

  {
    Graph g;
    g.add_node("a", std::make_unique<SequenceProducer>());
    g.add_node("b", std::make_unique<BufferConsumer>());
    g.connect("a", "out", "missing", "in");
    auto errs = validate_graph(g);
    if (!has_message_substr(errs, "unknown node")) {
      return fail("expected unknown node wire error");
    }
    std::cout << "  unknown node: " << errs.front().message << '\n';
  }

  {
    Graph g;
    g.add_node("a", std::make_unique<SequenceProducer>());
    g.add_node("b", std::make_unique<BufferConsumer>());
    g.connect("a", "nope", "b", "in");
    auto errs = validate_graph(g);
    if (!has_message_substr(errs, "missing output pin")) {
      return fail("expected missing output pin");
    }
    std::cout << "  missing pin: " << errs.front().message << '\n';
  }

  {
    Graph g;
    g.add_node("a", std::make_unique<SequenceProducer>());
    g.add_node("b", std::make_unique<ScalarConsumer>());  // expects Float, not buffer
    g.connect("a", "out", "b", "in");
    auto errs = validate_graph(g);
    if (!has_message_substr(errs, "incompatible types")) {
      return fail("expected incompatible types");
    }
    std::cout << "  type mismatch: " << errs.front().message << '\n';
  }

  {
    Graph g;
    g.add_node("a", std::make_unique<ScaleFilter>());
    g.add_node("b", std::make_unique<ScaleFilter>());
    g.connect("a", "out", "b", "in");
    g.connect("b", "out", "a", "in");
    auto errs = validate_graph(g);
    if (!has_message_substr(errs, "cycle")) {
      return fail("expected cycle");
    }
    std::cout << "  cycle: " << errs.front().message << '\n';
  }

  {
    Graph g;
    g.add_node("a", std::make_unique<ScaleFilter>());
    g.connect("a", "out", "a", "in");
    auto errs = validate_graph(g);
    if (!has_message_substr(errs, "self-cycle")) {
      return fail("expected self-cycle");
    }
    std::cout << "  self-cycle: " << errs.front().message << '\n';
  }

  {
    Graph g;
    g.add_node("a", std::make_unique<SequenceProducer>());
    try {
      g.add_node("a", std::make_unique<BufferConsumer>());
      return fail("duplicate id should throw on add_node");
    } catch (std::exception const& ex) {
      std::cout << "  duplicate id (add_node throw): " << ex.what() << '\n';
    }
  }

  {
    // compile() must refuse invalid graphs
    Graph g;
    g.add_node("a", std::make_unique<ScaleFilter>());
    g.connect("a", "out", "a", "in");
    try {
      engine.compile(g);
      return fail("compile should throw on invalid graph");
    } catch (std::exception const& ex) {
      std::cout << "  compile rejects invalid: " << ex.what() << '\n';
    }
  }

  std::cout << "validate failures: ok\n\n";

  // -------------------------------------------------------------------------
  // B) depth-1 nest (smoke-equivalent)
  // -------------------------------------------------------------------------
  std::cout << "== depth-1 GraphNode ==\n";
  {
    auto bp = make_scale_blueprint(3.0);
    Graph outer;
    outer.add_node("src", std::make_unique<SequenceProducer>());
    outer.add_node("box", std::make_unique<GraphNode>(bp, "box"));
    outer.add_node("sink", std::make_unique<BufferConsumer>());
    outer.find_node("src")->find_input("count")->literal = 4.0;
    outer.connect("src", "out", "box", "in");
    outer.connect("box", "out", "sink", "in");

    auto errs = validate_graph(outer);
    if (!errs.empty()) {
      return fail(errs.front().message.c_str());
    }

    FlatGraph flat = engine.compile(outer);
    assert_no_nesting_artifacts(flat, "depth1");
    if (count_type(flat, "scale_filter") != 1) {
      return fail("depth1 expected 1 scale");
    }
    // Prefixed id from splice: box/scale
    if (!flat.find_node("box/scale")) {
      return fail("depth1 missing box/scale");
    }
    if (flat.find_node("box") != nullptr) {
      return fail("depth1 wrapper should be gone");
    }

    for (auto& n : flat.nodes) {
      n.node->dirty = true;
    }
    engine.tick(flat);
    auto* sink = dynamic_cast<BufferConsumer*>(flat.find_node("sink"));
    if (!sink || sink->last().size() != 4 || sink->last().front() != 0.0 ||
        sink->last().back() != 9.0) {
      return fail("depth1 values");
    }
    std::cout << "  flat nodes:";
    for (auto const& n : flat.nodes) {
      std::cout << ' ' << n.id;
    }
    std::cout << "\n  sink last=" << sink->last().back() << " ok\n\n";
  }

  // -------------------------------------------------------------------------
  // C) depth-3 nest: outer -> L1 -> L2 -> scale(gain=2)
  //    sequence [0..3] * 2 => last=6
  // -------------------------------------------------------------------------
  std::cout << "== depth-3 nested GraphNodes ==\n";
  {
    auto leaf = make_scale_blueprint(2.0);                  // gain 2
    auto mid = make_wrap_blueprint(leaf, "mid", 0.0);       // wraps leaf
    auto top = make_wrap_blueprint(mid, "top_inner", 0.0);  // wraps mid

    Graph outer;
    outer.add_node("src", std::make_unique<SequenceProducer>());
    outer.add_node("nest", std::make_unique<GraphNode>(top, "nest"));
    outer.add_node("sink", std::make_unique<BufferConsumer>());
    outer.find_node("src")->find_input("count")->literal = 4.0;
    outer.connect("src", "out", "nest", "in");
    outer.connect("nest", "out", "sink", "in");

    auto errs = validate_graph(outer);
    if (!errs.empty()) {
      return fail(errs.front().message.c_str());
    }

    FlatGraph flat = engine.compile(outer);
    assert_no_nesting_artifacts(flat, "depth3");
    if (count_type(flat, "scale_filter") != 1) {
      return fail("depth3 expected single leaf scale");
    }
    if (count_type(flat, "sequence_producer") != 1 || count_type(flat, "buffer_consumer") != 1) {
      return fail("depth3 producer/consumer count");
    }

    // Deepest scale id: nest/child/child/scale  (top child=mid, mid child=leaf scale)
    Node* scale = nullptr;
    std::string scale_id;
    for (auto const& n : flat.nodes) {
      if (n.node && n.node->type_id() == "scale_filter") {
        scale = n.node.get();
        scale_id = n.id;
      }
    }
    if (!scale || scale_id.find("nest/") != 0 || scale_id.find("scale") == std::string::npos) {
      return fail("depth3 scale id prefix");
    }
    // Expect three path segments under nest: child / child / scale
    // nest/child/child/scale
    if (scale_id != "nest/child/child/scale") {
      std::cerr << "  got scale id: " << scale_id << '\n';
      return fail("depth3 unexpected scale path");
    }

    for (auto& n : flat.nodes) {
      n.node->dirty = true;
    }
    engine.tick(flat);
    auto* sink = dynamic_cast<BufferConsumer*>(flat.find_node("sink"));
    if (!sink || sink->last().size() != 4 || sink->last()[1] != 2.0 || sink->last().back() != 6.0) {
      return fail("depth3 values");
    }
    std::cout << "  scale flat id: " << scale_id << '\n';
    std::cout << "  edges=" << flat.edges.size() << " nodes=" << flat.nodes.size() << '\n';
    std::cout << "  sink: ";
    for (double v : sink->last()) {
      std::cout << v << ' ';
    }
    std::cout << "ok\n\n";
  }

  // -------------------------------------------------------------------------
  // D) depth-2 with two stacked gains (2 then 3) => *6
  //    leaf gain 3, mid is just a wrap; add second scale in mid? Better:
  //    chain two scale blueprints: outer wraps bp_a(gain2) which is not wrap-
  //    only — build mid as BoundaryIn -> GraphNode(leaf g=3) is only *3.
  //    For *6: blueprint chain scale2 then scale3 inside one macro.
  // -------------------------------------------------------------------------
  std::cout << "== stacked gains inside one macro (*2 then *3 = *6) ==\n";
  {
    auto bp = std::make_shared<Graph>();
    bp->add_node("in", make_boundary_in(TypeId::FloatBuffer));
    bp->add_node("s2", std::make_unique<ScaleFilter>());
    bp->add_node("s3", std::make_unique<ScaleFilter>());
    bp->add_node("out", make_boundary_out(TypeId::FloatBuffer));
    bp->find_node("s2")->find_input("gain")->literal = 2.0;
    bp->find_node("s3")->find_input("gain")->literal = 3.0;
    bp->connect("in", "out", "s2", "in");
    bp->connect("s2", "out", "s3", "in");
    bp->connect("s3", "out", "out", "in");

    Graph outer;
    outer.add_node("src", std::make_unique<SequenceProducer>());
    outer.add_node("box", std::make_unique<GraphNode>(bp, "box"));
    outer.add_node("sink", std::make_unique<BufferConsumer>());
    outer.find_node("src")->find_input("count")->literal = 5.0;
    outer.connect("src", "out", "box", "in");
    outer.connect("box", "out", "sink", "in");

    FlatGraph flat = engine.compile(outer);
    assert_no_nesting_artifacts(flat, "stack");
    if (count_type(flat, "scale_filter") != 2) {
      return fail("stack expected 2 scales");
    }
    for (auto& n : flat.nodes) {
      n.node->dirty = true;
    }
    engine.tick(flat);
    auto* sink = dynamic_cast<BufferConsumer*>(flat.find_node("sink"));
    // [0,1,2,3,4] *2 *3 = [0,6,12,18,24]
    if (!sink || sink->last().size() != 5 || sink->last()[1] != 6.0 || sink->last().back() != 24.0) {
      return fail("stacked gain values");
    }
    std::cout << "  sink[1]=6 sink[4]=24 ok\n\n";
  }

  // -------------------------------------------------------------------------
  // E) two sibling depth-2 instances, independent clones, different gains
  // -------------------------------------------------------------------------
  std::cout << "== sibling depth-2 instances (gains 2 and 5) ==\n";
  {
    auto leaf2 = make_scale_blueprint(2.0);
    auto leaf5 = make_scale_blueprint(5.0);
    auto wrap2 = make_wrap_blueprint(leaf2, "inner2", 0.0);
    auto wrap5 = make_wrap_blueprint(leaf5, "inner5", 0.0);

    Graph outer;
    outer.add_node("src", std::make_unique<SequenceProducer>());
    outer.add_node("a", std::make_unique<GraphNode>(wrap2, "a"));
    outer.add_node("b", std::make_unique<GraphNode>(wrap5, "b"));
    outer.add_node("sa", std::make_unique<BufferConsumer>());
    outer.add_node("sb", std::make_unique<BufferConsumer>());
    outer.find_node("src")->find_input("count")->literal = 4.0;
    outer.connect("src", "out", "a", "in");
    outer.connect("src", "out", "b", "in");
    outer.connect("a", "out", "sa", "in");
    outer.connect("b", "out", "sb", "in");

    FlatGraph flat = engine.compile(outer);
    assert_no_nesting_artifacts(flat, "siblings");
    if (count_type(flat, "scale_filter") != 2) {
      return fail("siblings expected 2 scales");
    }
    if (!flat.find_node("a/child/scale") || !flat.find_node("b/child/scale")) {
      return fail("siblings missing prefixed scales");
    }

    for (auto& n : flat.nodes) {
      n.node->dirty = true;
    }
    engine.tick(flat);
    auto* sa = dynamic_cast<BufferConsumer*>(flat.find_node("sa"));
    auto* sb = dynamic_cast<BufferConsumer*>(flat.find_node("sb"));
    if (!sa || !sb || sa->last().size() != 4 || sb->last().size() != 4) {
      return fail("sibling sink sizes");
    }
    // [0,1,2,3]*2 vs *5
    if (sa->last()[2] != 4.0 || sb->last()[2] != 10.0) {
      return fail("sibling independence values");
    }
    std::cout << "  sa[2]=" << sa->last()[2] << " sb[2]=" << sb->last()[2] << " ok\n\n";
  }

  // -------------------------------------------------------------------------
  // F) valid empty-ish edge case: validate ok linear (control)
  // -------------------------------------------------------------------------
  {
    Graph g;
    g.add_node("p", std::make_unique<SequenceProducer>());
    g.add_node("c", std::make_unique<BufferConsumer>());
    g.connect("p", "out", "c", "in");
    if (!validate_graph(g).empty()) {
      return fail("control valid graph failed validate");
    }
  }

  std::cout << "nested_depth_validate_demo ok\n";
  return 0;
}
