#include "parser.h"

#include <algorithm>
#include <cstdio>
#include <initializer_list>

namespace esphome {
namespace qalcosonic_e3 {
namespace {
uint16_t read_u16(const uint8_t *p) {
  return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}
uint32_t read_u32(const uint8_t *p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
std::string datetime(const uint8_t *p) {
  const unsigned year = 2000U + ((p[2] >> 5) & 7U) + ((p[3] >> 4) & 15U) * 8U;
  char text[20];
  std::snprintf(text, sizeof(text), "%04u-%02u-%02u %02u:%02u", year,
                p[3] & 15U, p[2] & 31U, p[1] & 31U, p[0] & 63U);
  return text;
}
}  // namespace

const char *frame_result_message(FrameResult result) {
  switch (result) {
    case FrameResult::OK: return "Frame processed successfully";
    case FrameResult::INCOMPLETE: return "Incomplete M-Bus frame";
    case FrameResult::MALFORMED: return "Malformed M-Bus long frame";
    case FrameResult::INVALID_STOP: return "Invalid M-Bus stop byte";
    case FrameResult::CHECKSUM: return "M-Bus checksum mismatch";
    case FrameResult::UNEXPECTED_CI: return "Unexpected M-Bus CI field";
    case FrameResult::SHORT_HEADER: return "RSP_UD2 header is too short";
  }
  return "Invalid M-Bus frame";
}

FrameResult parse_frame(const uint8_t *data, size_t size, MeterData &result) {
  if (size < 4) return FrameResult::INCOMPLETE;
  if (data[0] != 0x68 || data[1] != data[2] || data[3] != 0x68) return FrameResult::MALFORMED;
  const size_t total = static_cast<size_t>(data[1]) + 6;
  if (size < total) return FrameResult::INCOMPLETE;
  if (data[total - 1] != 0x16) return FrameResult::INVALID_STOP;
  uint8_t checksum = 0;
  for (size_t i = 4; i < total - 2; ++i) checksum += data[i];
  if (checksum != data[total - 2]) return FrameResult::CHECKSUM;
  if (data[1] < 3) return FrameResult::SHORT_HEADER;
  if (data[6] != 0x72) return FrameResult::UNEXPECTED_CI;
  if (data[1] < 15) return FrameResult::SHORT_HEADER;

  MeterData decoded;
  char serial[9];
  std::snprintf(serial, sizeof(serial), "%02X%02X%02X%02X", static_cast<unsigned>(data[10]),
                static_cast<unsigned>(data[9]), static_cast<unsigned>(data[8]), static_cast<unsigned>(data[7]));
  decoded.serial_number = serial;
  const uint16_t man = read_u16(data + 11);
  char manufacturer[4] = {static_cast<char>(((man >> 10) & 31) + 64),
                          static_cast<char>(((man >> 5) & 31) + 64),
                          static_cast<char>((man & 31) + 64), '\0'};
  decoded.manufacturer = manufacturer;
  decoded.protocol_version = data[13];
  const size_t end = total - 2;
  // Preserve the reference lambda's first byte-pattern match, including its
  // behavior when a pattern occurs inside another record's value.
  auto find = [&](std::initializer_list<uint8_t> pattern, size_t width) -> const uint8_t * {
    for (size_t p = 19; p + pattern.size() <= end; ++p) {
      if (std::equal(pattern.begin(), pattern.end(), data + p)) {
        return p + pattern.size() + width <= end ? data + p + pattern.size() : nullptr;
      }
    }
    return nullptr;
  };
  const uint8_t *p;
  if ((p = find({0x04, 0x6D}, 4))) decoded.meter_datetime.set(datetime(p));
  if ((p = find({0x34, 0xFD, 0x17}, 4))) {
    decoded.error_code.set(read_u32(p));
    if (decoded.error_code.value == 0) decoded.error_start.set("No error");
    else if ((p = find({0x34, 0x6D}, 4))) decoded.error_start.set(datetime(p));
  }
  if ((p = find({0x04, 0x20}, 4))) decoded.battery_operating_duration.set(read_u32(p) / 3600.0f / 24.0f);
  if ((p = find({0x04, 0x24}, 4))) decoded.operating_time_without_error.set(read_u32(p) / 3600.0f / 24.0f);
  if ((p = find({0x04, 0x86, 0x3B}, 4))) decoded.energy.set(read_u32(p) / 1000.0f);
  if ((p = find({0x04, 0x13}, 4))) decoded.volume.set(read_u32(p) / 1000.0f);
  if ((p = find({0x04, 0x2B}, 4))) decoded.power.set(read_u32(p) / 1000.0f);
  if ((p = find({0x04, 0x3B}, 4))) decoded.flow.set(read_u32(p) / 1000.0f);
  if ((p = find({0x02, 0x59}, 2))) decoded.flow_temperature.set(read_u16(p) / 100.0f);
  if ((p = find({0x02, 0x5D}, 2))) decoded.return_temperature.set(read_u16(p) / 100.0f);
  if ((p = find({0x02, 0x61}, 2))) decoded.temperature_difference.set(read_u16(p) / 100.0f);
  result = decoded;
  return FrameResult::OK;
}

bool FrameReceiver::push(uint8_t byte, MeterData &result, FrameResult &error) {
  error = FrameResult::INCOMPLETE;
  if (this->size_ == this->buffer_.size()) {
    std::move(this->buffer_.begin() + 1, this->buffer_.end(), this->buffer_.begin());
    --this->size_;
  }
  this->buffer_[this->size_++] = byte;
  size_t keep = this->size_;
  for (size_t i = 0; i < this->size_; ++i) {
    if (this->buffer_[i] != 0x68) continue;
    MeterData decoded;
    const auto status = parse_frame(this->buffer_.data() + i, this->size_ - i, decoded);
    if (status == FrameResult::OK) {
      result = decoded;
      this->clear();
      return true;
    }
    if (status == FrameResult::INCOMPLETE) keep = std::min(keep, i);
    else error = status;
  }
  if (keep != 0) {
    std::move(this->buffer_.begin() + keep, this->buffer_.begin() + this->size_, this->buffer_.begin());
    this->size_ -= keep;
  }
  return false;
}

}  // namespace qalcosonic_e3
}  // namespace esphome
