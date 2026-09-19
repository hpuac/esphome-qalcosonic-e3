#include <cassert>
#include <cmath>
#include <fstream>
#include <iostream>
#include <vector>

#include "parser.h"
using namespace esphome::qalcosonic_e3;
void near(const Value<float> &value, float expected) {
  assert(value.present && std::fabs(value.value - expected) < 0.0001f);
}
void checksum(std::vector<uint8_t> &frame) {
  uint8_t sum = 0;
  for (size_t i = 4; i < frame.size() - 2; ++i) sum += frame[i];
  frame[frame.size() - 2] = sum;
}
int main(int argc, char **argv) {
  assert(argc == 2);
  std::ifstream input(argv[1]);
  std::vector<uint8_t> frame;
  unsigned byte;
  while (input >> std::hex >> byte) frame.push_back(static_cast<uint8_t>(byte));
  assert(frame.size() == 99);
  MeterData data;
  assert(parse_frame(frame.data(), frame.size(), data) == FrameResult::OK);
  assert(data.serial_number == "01234567");
  assert(data.manufacturer == "AXI");
  assert(data.protocol_version == 11);
  assert(data.meter_datetime.present && data.meter_datetime.value == "2026-09-15 16:02");
  assert(data.error_code.present && data.error_code.value == 0);
  assert(data.error_start.present && data.error_start.value == "No error");
  near(data.energy, 0.073f);
  near(data.volume, 3.032f);
  near(data.power, 0.323f);
  near(data.flow, 0.015f);
  near(data.flow_temperature, 65.20f);
  near(data.return_temperature, 46.49f);
  near(data.temperature_difference, 18.70f);
  near(data.battery_operating_duration, 13424185 / 3600.0f / 24.0f);
  near(data.operating_time_without_error, 13424185 / 3600.0f / 24.0f);
  for (size_t n = 0; n < frame.size(); ++n) assert(parse_frame(frame.data(), n, data) == FrameResult::INCOMPLETE);
  auto bad = frame;
  bad[50] ^= 1;
  assert(parse_frame(bad.data(), bad.size(), data) == FrameResult::CHECKSUM);
  bad = frame;
  bad.back() = 0;
  assert(parse_frame(bad.data(), bad.size(), data) == FrameResult::INVALID_STOP);
  bad = frame;
  bad[2]++;
  assert(parse_frame(bad.data(), bad.size(), data) == FrameResult::MALFORMED);
  bad = frame;
  bad[6] = 0x73;
  checksum(bad);
  assert(parse_frame(bad.data(), bad.size(), data) == FrameResult::UNEXPECTED_CI);
  bad = frame;
  bad[34] = 1;
  checksum(bad);
  assert(parse_frame(bad.data(), bad.size(), data) == FrameResult::OK);
  assert(data.error_code.value == 1 && data.error_start.value == "2000-01-01 00:00");
  // Header-only responses remain successful, with no invented record values.
  bad.assign(frame.begin(), frame.begin() + 19);
  bad[1] = bad[2] = 15;
  bad.push_back(0);
  bad.push_back(0x16);
  checksum(bad);
  assert(parse_frame(bad.data(), bad.size(), data) == FrameResult::OK);
  assert(!data.energy.present && !data.error_start.present);
  bad = {0x68, 3, 3, 0x68, 8, 1, 0x72, 0x7B, 0x16};
  assert(parse_frame(bad.data(), bad.size(), data) == FrameResult::SHORT_HEADER);
  // An incomplete final record must not read its checksum as value bytes.
  bad.assign(frame.begin(), frame.begin() + 19);
  bad.insert(bad.end(), {0x04, 0x13, 1, 2, 0, 0x16});
  bad[1] = bad[2] = static_cast<uint8_t>(bad.size() - 6);
  checksum(bad);
  assert(parse_frame(bad.data(), bad.size(), data) == FrameResult::OK && !data.volume.present);
  FrameReceiver receiver;
  FrameResult error;
  for (unsigned i = 0; i < 10000; ++i) {
    assert(!receiver.push(0xE5, data, error));
    assert(receiver.size() <= 261);
  }
  // A false long length must not block a subsequent complete valid frame.
  for (uint8_t b : {0x68, 0xFF, 0xFF, 0x68}) receiver.push(b, data, error);
  unsigned accepted = 0;
  for (uint8_t b : frame) accepted += receiver.push(b, data, error);
  assert(accepted == 1 && receiver.size() == 0);
  bad = frame;
  bad[50] ^= 1;
  for (uint8_t b : bad) assert(!receiver.push(b, data, error));
  for (uint8_t b : frame) accepted += receiver.push(b, data, error);
  for (uint8_t b : frame) accepted += receiver.push(b, data, error);
  assert(accepted == 3);
  std::cout << "Parser and incremental receiver tests passed\n";
}
