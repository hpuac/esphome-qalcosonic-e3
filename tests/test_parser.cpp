#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <vector>

#include "link.h"
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
  assert(argc == 3);
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
  assert(data.error_code.present && data.error_code.value == "0000");
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
  auto find_pattern = [](std::vector<uint8_t> &bytes, std::initializer_list<uint8_t> pattern) {
    return std::search(bytes.begin(), bytes.end(), pattern.begin(), pattern.end());
  };
  // A record marker inside an earlier record's value is not a new record.
  auto bad = frame;
  auto battery = find_pattern(bad, {0x04, 0x20});
  assert(battery != bad.end());
  const std::array<uint8_t, 4> embedded_marker{{0x04, 0x13, 0xFF, 0xFF}};
  std::copy(embedded_marker.begin(), embedded_marker.end(), battery + 2);
  checksum(bad);
  assert(parse_frame(bad.data(), bad.size(), data) == FrameResult::OK);
  near(data.volume, 3.032f);
  // Signed M-Bus integers can represent reverse flow and sub-zero temperatures.
  bad = frame;
  auto flow = find_pattern(bad, {0x04, 0x3B});
  auto power = find_pattern(bad, {0x04, 0x2B});
  auto temperature = find_pattern(bad, {0x02, 0x59});
  assert(flow != bad.end() && power != bad.end() && temperature != bad.end());
  std::fill(flow + 2, flow + 6, 0xFF);
  std::fill(power + 2, power + 6, 0xFF);
  temperature[2] = 0x9C;
  temperature[3] = 0xFF;
  checksum(bad);
  assert(parse_frame(bad.data(), bad.size(), data) == FrameResult::OK);
  near(data.flow, -0.001f);
  near(data.power, -0.001f);
  near(data.flow_temperature, -1.0f);
  for (size_t n = 0; n < frame.size(); ++n) assert(parse_frame(frame.data(), n, data) == FrameResult::INCOMPLETE);
  bad = frame;
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
  assert(data.raw_error_status.value == 1 && data.error_code.value == "0000" &&
         data.error_start.value == "2000-01-01 00:00");
  // Protocol Table 10 examples and simultaneous flags preserve LCD formatting.
  struct ErrorCase {
    uint32_t raw;
    const char *lcd;
  };
  for (const auto &test : std::initializer_list<ErrorCase>{
           {0x00000004, "8000"}, {0x00000008, "8000"}, {0x00000010, "1000"}, {0x00000020, "0008"}, {0x00000400, "0001"},
           {0x00000800, "0002"}, {0x00010000, "0080"}, {0x00020000, "0080"}, {0x00040000, "00C0"}, {0x00080000, "0080"},
           {0x00100000, "0800"}, {0x00200000, "0800"}, {0x00400000, "0C00"}, {0x00800000, "0800"}, {0x01000000, "0880"},
           {0x04000000, "4000"}, {0x08000000, "2000"}, {0x10000000, "0004"}, {0x20000000, "8000"}, {0x80000000, "8000"},
           {0x04140000, "48C0"}, {0x00001000, "0000"}, {0xFFFFFFFF, "FCCF"}}) {
    bad = frame;
    auto status = find_pattern(bad, {0x34, 0xFD, 0x17});
    assert(status != bad.end());
    for (unsigned i = 0; i < 4; ++i) status[3 + i] = static_cast<uint8_t>(test.raw >> (8 * i));
    checksum(bad);
    assert(parse_frame(bad.data(), bad.size(), data) == FrameResult::OK);
    assert(data.error_code.present && data.error_code.value == test.lcd);
    assert(data.raw_error_status.present && data.raw_error_status.value == test.raw);
    assert(data.error_start.value == "2000-01-01 00:00");
    // Active status without an error-start record must not invent a timestamp.
    auto timestamp = find_pattern(bad, {0x34, 0x6D});
    assert(timestamp != bad.end());
    bad.erase(timestamp, timestamp + 6);
    bad[1] = bad[2] = static_cast<uint8_t>(bad.size() - 6);
    checksum(bad);
    assert(parse_frame(bad.data(), bad.size(), data) == FrameResult::OK);
    assert(!data.error_start.present && data.error_code.value == test.lcd);
  }
  // Header-only responses remain successful, with no invented record values.
  bad.assign(frame.begin(), frame.begin() + 19);
  bad[1] = bad[2] = 15;
  bad.push_back(0);
  bad.push_back(0x16);
  checksum(bad);
  assert(parse_frame(bad.data(), bad.size(), data) == FrameResult::OK);
  assert(!data.energy.present && !data.error_start.present);
  assert(!data.error_code.present && !data.raw_error_status.present);
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
  // A valid frame's second 0x68 (and any payload 0x68) is not a malformed frame.
  auto expect_clean_frame = [&](const std::vector<uint8_t> &bytes) {
    receiver.clear();
    for (size_t i = 0; i < bytes.size(); ++i) {
      const bool complete = receiver.push(bytes[i], data, error);
      assert(complete == (i + 1 == bytes.size()));
      if (!complete) assert(error == FrameResult::INCOMPLETE);
    }
  };
  expect_clean_frame(frame);
  bad = frame;
  bad[50] = 0x68;
  checksum(bad);
  expect_clean_frame(bad);
  // Real errors at the leading candidate must still be reported.
  bad = frame;
  bad[2]++;
  for (size_t i = 0; i < 4; ++i) assert(!receiver.push(bad[i], data, error));
  assert(error == FrameResult::MALFORMED);
  receiver.clear();
  bad = frame;
  bad[50] ^= 1;
  for (uint8_t b : bad) assert(!receiver.push(b, data, error));
  assert(error == FrameResult::CHECKSUM);
  receiver.clear();
  bad = frame;
  bad.back() = 0;
  for (uint8_t b : bad) assert(!receiver.push(b, data, error));
  assert(error == FrameResult::INVALID_STOP);
  receiver.clear();
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
  // Both serial fields were anonymized to 01234567 and the checksum recalculated.
  std::ifstream wired_input(argv[2]);
  std::vector<uint8_t> wired;
  while (wired_input >> std::hex >> byte) wired.push_back(static_cast<uint8_t>(byte));
  assert(wired.size() == 99);
  assert(parse_frame(wired.data(), wired.size(), data) == FrameResult::OK);
  assert(data.response_address == 0x2C);
  assert(data.serial_number == "01234567");
  assert(data.manufacturer == "AXI" && data.protocol_version == 11);
  assert(data.error_code.present && data.error_code.value == "0000");
  near(data.energy, 0.260f);
  near(data.volume, 12.793f);
  near(data.power, 0.213f);
  near(data.flow, 0.015f);
  near(data.flow_temperature, 62.33f);
  near(data.return_temperature, 50.28f);
  near(data.temperature_difference, 12.05f);
  expect_clean_frame(wired);
  assert(data.response_address == 0x2C);
  LinkState auto_link;
  auto_link.set_auto_address();
  assert(auto_link.start()[2] == 0xFE);
  assert(auto_link.accept_ack(0xE5));
  const auto discovery_request = auto_link.start();
  assert(discovery_request[1] == 0x7B && discovery_request[2] == 0xFE);
  auto_link.timeout();
  assert(auto_link.discovering() && auto_link.start() == discovery_request);
  receiver.clear();
  for (uint8_t b : wired) {
    if (receiver.push(b, data, error)) assert(auto_link.accept_response(data.response_address));
  }
  assert(auto_link.address() == 0x2C && auto_link.start()[2] == 0x2C);
  // Feed complete optical and wired frames through the receiver and address gate.
  // An ignored frame must not prevent a matching frame in the same readout.
  for (uint8_t address : {0x2C, 0xFE}) {
    LinkState link;
    link.set_address(address);
    link.start();
    assert(link.accept_ack(0xE5));
    link.start();
    receiver.clear();
    unsigned responses = 0;
    for (const auto *bytes : {&frame, &wired}) {
      for (uint8_t b : *bytes) {
        if (!receiver.push(b, data, error)) continue;
        if (!link.accept_response(data.response_address)) continue;
        ++responses;
        if (address == 0xFE && responses == 1) link.start();
      }
    }
    assert(responses == (address == 0xFE ? 2U : 1U));
  }
  // Real wired captures: both serial fields anonymized to 01234567,
  // with the M-Bus checksum recalculated after replacing the BCD serial bytes.
  struct CaptureCase {
    const char *path;
    float power, flow, flow_temperature, return_temperature, delta_t;
    const char *error_code;
    uint32_t raw_error_status;
  };
  const CaptureCase captures[] = {
      {"tests/fixtures/wired_zero_flow.hex", 0, 0, 65.22f, 37.75f, 27.46f, "0000", 0},
      {"tests/fixtures/wired_low_delta_t_error.hex", 0, 0, 24.95f, 23.68f, 1.26f, "4000", 0x04000000},
      {"tests/fixtures/wired_high_flow.hex", 29.266f, 0.726f, 79.51f, 41.44f, 38.06f, "0000", 0},
  };
  for (const auto &expected : captures) {
    std::ifstream capture_input(expected.path);
    std::vector<uint8_t> capture;
    while (capture_input >> std::hex >> byte) capture.push_back(static_cast<uint8_t>(byte));
    assert(capture.size() == 99);
    const std::array<uint8_t, 4> serial{{0x67, 0x45, 0x23, 0x01}};
    assert(std::equal(serial.begin(), serial.end(), capture.begin() + 7));
    assert(std::equal(serial.begin(), serial.end(), capture.begin() + 89));
    assert(parse_frame(capture.data(), capture.size(), data) == FrameResult::OK);
    assert(data.response_address == 0x2C && data.serial_number == "01234567");
    assert(data.manufacturer == "AXI" && data.protocol_version == 11);
    near(data.power, expected.power);
    near(data.flow, expected.flow);
    near(data.flow_temperature, expected.flow_temperature);
    near(data.return_temperature, expected.return_temperature);
    near(data.temperature_difference, expected.delta_t);
    assert(data.error_code.present && data.error_code.value == expected.error_code);
    assert(data.raw_error_status.present && data.raw_error_status.value == expected.raw_error_status);
    assert(data.error_start.present);
    assert(data.error_start.value == (expected.raw_error_status == 0 ? "No error" : "2000-01-01 00:00"));
    expect_clean_frame(capture);
    assert(data.error_code.value == expected.error_code);
  }
  std::cout << "Parser and incremental receiver tests passed\n";
}
