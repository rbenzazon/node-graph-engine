#pragma once

#include "lq_types.hpp"
#include "node_engine/factory.hpp"
#include "node_engine/node.hpp"

namespace node_engine::nodes::line_quality {

class ThickEdgeSourceI16 : public NodeBase<ThickEdgeSourceI16> {
 public:
  static constexpr Meta meta{.type_id = "lq_thick_edge_i16", .role = Role::Producer};

  static void ports(Ports& p) {
    p.out<ChannelChunkI16>("out");
    p.out<double>("t_stamp").literal(0.0);
  }

  void set_emit(ChannelChunkI16 chunk) {
    chunk.signal_id = kSigThickness;
    chunk_ = std::move(chunk);
  }

  void compute(Slice) override {
    set_out("out", chunk_);
    set_out("t_stamp", chunk_.t_s);
    suppress();
  }

 private:
  ChannelChunkI16 chunk_;
};

class DistEdgeSourceI16 : public NodeBase<DistEdgeSourceI16> {
 public:
  static constexpr Meta meta{.type_id = "lq_dist_edge_i16", .role = Role::Producer};

  static void ports(Ports& p) {
    p.out<ChannelChunkI16>("out");
    p.out<double>("t_stamp").literal(0.0);
  }

  void set_emit(ChannelChunkI16 chunk) {
    chunk.signal_id = kSigDistance;
    chunk_ = std::move(chunk);
  }

  void compute(Slice) override {
    set_out("out", chunk_);
    set_out("t_stamp", chunk_.t_s);
    suppress();
  }

 private:
  ChannelChunkI16 chunk_;
};

class EncEdgeSourceRaw : public NodeBase<EncEdgeSourceRaw> {
 public:
  static constexpr Meta meta{.type_id = "lq_enc_edge_raw", .role = Role::Producer};

  static void ports(Ports& p) {
    p.out<EncoderRawI16>("out");
    p.out<double>("t_stamp").literal(0.0);
  }

  void set_emit(EncoderRawI16 raw) {
    raw.signal_id = kSigEncoder;
    raw_ = raw;
  }

  void compute(Slice) override {
    set_out("out", raw_);
    set_out("t_stamp", raw_.t_s);
    suppress();
  }

 private:
  EncoderRawI16 raw_;
};

REGISTER_NODE(ThickEdgeSourceI16);
REGISTER_NODE(DistEdgeSourceI16);
REGISTER_NODE(EncEdgeSourceRaw);

}  // namespace node_engine::nodes::line_quality
