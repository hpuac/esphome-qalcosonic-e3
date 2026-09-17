#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace esphome {
namespace qalcosonic_e3 {

// Optional values keep missing records from overwriting previous entity states.
template<typename T> struct Value {
  bool present{false};
  T value{};
  void set(T v) { this->value = v; this->present = true; }
};

struct MeterData {
  std::string serial_number;
  std::string manufacturer;
  uint8_t protocol_version{0};
  Value<std::string> meter_datetime, error_start;
  Value<uint32_t> error_code;
  Value<float> battery_operating_duration, operating_time_without_error;
  Value<float> energy, volume, power, flow, flow_temperature, return_temperature, temperature_difference;
};

enum class FrameResult { OK, INCOMPLETE, MALFORMED, INVALID_STOP, CHECKSUM, UNEXPECTED_CI, SHORT_HEADER };
const char *frame_result_message(FrameResult result);
FrameResult parse_frame(const uint8_t *data, size_t size, MeterData &result);

// A long frame has at most 255 body bytes plus six framing bytes.
// Scan all candidate starts so a corrupt length cannot hide a later valid frame.
class FrameReceiver {
 public:
  bool push(uint8_t byte, MeterData &result, FrameResult &error);
  void clear() { this->size_ = 0; }
  size_t size() const { return this->size_; }
 protected:
  std::array<uint8_t, 261> buffer_{};
  size_t size_{0};
};

}  // namespace qalcosonic_e3
}  // namespace esphome
