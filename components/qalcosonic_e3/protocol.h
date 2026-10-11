#pragma once

#include <cstddef>
#include <cstdint>

namespace esphome {
namespace qalcosonic_e3 {
namespace mbus {

constexpr uint8_t SHORT_FRAME_START = 0x10;
constexpr uint8_t LONG_FRAME_START = 0x68;
constexpr uint8_t FRAME_STOP = 0x16;
constexpr uint8_t ACK = 0xE5;
constexpr uint8_t SND_NKE = 0x40;
constexpr uint8_t REQ_UD2_FCB_CLEAR = 0x5B;
constexpr uint8_t REQ_UD2_FCB_SET = 0x7B;
constexpr uint8_t VARIABLE_DATA_CI = 0x72;
constexpr uint8_t POINT_TO_POINT_ADDRESS = 0xFE;
constexpr uint8_t MAX_PRIMARY_ADDRESS = 0xFA;

// Long frame: 68 L L 68 | C A CI + application data | checksum 16.
constexpr size_t LONG_FRAME_PREFIX_SIZE = 4;
constexpr size_t FRAME_TRAILER_SIZE = 2;
constexpr size_t LONG_FRAME_OVERHEAD = LONG_FRAME_PREFIX_SIZE + FRAME_TRAILER_SIZE;
constexpr size_t LINK_HEADER_SIZE = 3;
// Fixed application header: ID, manufacturer, version, medium, counter, status, signature.
constexpr size_t RESPONSE_HEADER_SIZE = LINK_HEADER_SIZE + 12;
constexpr size_t RECORDS_OFFSET = LONG_FRAME_PREFIX_SIZE + RESPONSE_HEADER_SIZE;
constexpr size_t MAX_LONG_FRAME_SIZE = 255 + LONG_FRAME_OVERHEAD;

}  // namespace mbus
}  // namespace qalcosonic_e3
}  // namespace esphome
