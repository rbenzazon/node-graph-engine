#pragma once

#include "frame_util.hpp"
#include "node_engine/converter_registry.hpp"
#include "node_engine/type_registry.hpp"

#include <cstdint>
#include <stdexcept>
#include <vector>

namespace node_engine::nodes::line_quality {

inline constexpr std::uint32_t kSigThickness = 1;
inline constexpr std::uint32_t kSigDistance = 2;
inline constexpr std::uint32_t kSigEncoder = 3;

// Compare opcodes as int32 pin values (separate pin from operand).
inline constexpr std::int32_t kOpLT = 0;
inline constexpr std::int32_t kOpLE = 1;
inline constexpr std::int32_t kOpGT = 2;
inline constexpr std::int32_t kOpGE = 3;
inline constexpr std::int32_t kOpEQ = 4;
inline constexpr std::int32_t kOpNE = 5;

// Multichannel geometry chunk (raw DAQ-style milli-units as int16).
struct ChannelChunkI16 {
  std::uint32_t signal_id = 0;
  std::uint32_t channels = static_cast<std::uint32_t>(kChannels);
  double t_s = 0;
  std::vector<std::int16_t> samples;  // channels * K, channel-major per frame
};

// Multichannel geometry chunk in engineering float32 units (mm).
struct ChannelChunkF32 {
  std::uint32_t signal_id = 0;
  std::uint32_t channels = static_cast<std::uint32_t>(kChannels);
  double t_s = 0;
  std::vector<float> samples;
};

struct EncoderRawI16 {
  std::uint32_t signal_id = kSigEncoder;
  double t_s = 0;
  std::int16_t x_counts = 0;
  std::int16_t y_counts = 0;
  std::int16_t vel_counts = 0;
  std::uint16_t status = 1;
};

struct EncoderPoseF32 {
  std::uint32_t signal_id = kSigEncoder;
  double t_s = 0;
  float x_mm = 0;
  float y_mm = 0;
  float vel_x = 0;
  std::uint16_t quality = 0;
};

struct FrameFeaturesF32 {
  std::uint32_t signal_id = 0;
  double t_s = 0;
  float mean = 0;
  float min_v = 0;
  float max_v = 0;
  float p2p = 0;
};

inline std::size_t num_frames(ChannelChunkI16 const& c) {
  if (c.channels == 0 || c.samples.size() % c.channels != 0) {
    throw std::runtime_error("ChannelChunkI16 size must be multiple of channels");
  }
  return c.samples.size() / c.channels;
}

inline std::size_t num_frames(ChannelChunkF32 const& c) {
  if (c.channels == 0 || c.samples.size() % c.channels != 0) {
    throw std::runtime_error("ChannelChunkF32 size must be multiple of channels");
  }
  return c.samples.size() / c.channels;
}

inline std::int16_t const* frame_ptr(ChannelChunkI16 const& c, std::size_t f) {
  return c.samples.data() + f * c.channels;
}

inline float const* frame_ptr(ChannelChunkF32 const& c, std::size_t f) {
  return c.samples.data() + f * c.channels;
}

inline float* frame_ptr(ChannelChunkF32& c, std::size_t f) {
  return c.samples.data() + f * c.channels;
}

inline void register_lq_demo_types() {
  register_type<ChannelChunkI16>("lq.ChannelChunkI16");
  register_type<ChannelChunkF32>("lq.ChannelChunkF32");
  register_type<EncoderRawI16>("lq.EncoderRawI16");
  register_type<EncoderPoseF32>("lq.EncoderPoseF32");
  register_type<FrameFeaturesF32>("lq.FrameFeaturesF32");
}

// Direct converters only. I16 milli-units -> f32 mm; encoder counts/100 -> mm.
inline void register_lq_demo_converters() {
  register_converter<ChannelChunkI16, ChannelChunkF32>(
      "lq.chunk_i16_to_f32", [](ChannelChunkI16 const& in, ChannelChunkF32& out) {
        out.signal_id = in.signal_id;
        out.channels = in.channels;
        out.t_s = in.t_s;
        out.samples.resize(in.samples.size());
        for (std::size_t i = 0; i < in.samples.size(); ++i) {
          out.samples[i] = static_cast<float>(in.samples[i]) * (1.0f / 1000.0f);
        }
      });

  register_converter<EncoderRawI16, EncoderPoseF32>(
      "lq.encoder_raw_to_pose", [](EncoderRawI16 const& in, EncoderPoseF32& out) {
        out.signal_id = in.signal_id;
        out.t_s = in.t_s;
        out.x_mm = static_cast<float>(in.x_counts) * (1.0f / 100.0f);
        out.y_mm = static_cast<float>(in.y_counts) * (1.0f / 100.0f);
        out.vel_x = static_cast<float>(in.vel_counts) * (1.0f / 100.0f);
        out.quality = in.status;
      });
}

}  // namespace node_engine::nodes::line_quality
