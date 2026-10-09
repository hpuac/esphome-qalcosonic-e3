#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>

#include "link.h"

using esphome::qalcosonic_e3::LinkState;

int main() {
  LinkState link;
  for (uint8_t address : {0x01, 0x2C, 0xFE}) {
    link.set_address(address);
    const uint8_t reset_checksum = address == 0x01 ? 0x41 : (address == 0x2C ? 0x6C : 0x3E);
    const uint8_t request_checksum = address == 0x01 ? 0x5C : (address == 0x2C ? 0x87 : 0x59);
    assert((link.start() == std::array<uint8_t, 5>{{0x10, 0x40, address, reset_checksum, 0x16}}));
    assert(link.accept_ack(0xE5));
    assert((link.start() == std::array<uint8_t, 5>{{0x10, 0x7B, address, static_cast<uint8_t>(0x7B + address), 0x16}}));
    assert(link.accept_response(address));
    assert((link.start() == std::array<uint8_t, 5>{{0x10, 0x5B, address, request_checksum, 0x16}}));
    link.reset();
    assert(link.address() == address);
  }
  link.set_address(0x01);
  constexpr std::array<uint8_t, 5> reset{{0x10, 0x40, 0x01, 0x41, 0x16}};
  constexpr std::array<uint8_t, 5> first{{0x10, 0x7B, 0x01, 0x7C, 0x16}};
  constexpr std::array<uint8_t, 5> second{{0x10, 0x5B, 0x01, 0x5C, 0x16}};

  assert(link.needs_reset() && link.start() == reset);
  assert(link.awaiting_ack());
  assert(!link.accept_ack(0x00));
  link.timeout();
  assert(link.needs_reset() && link.start() == reset);
  assert(link.accept_ack(0xE5));
  assert(link.start() == first);

  // A damaged or missing reply does not advance the FCB.
  link.timeout();
  assert(link.start() == first);
  link.timeout();
  assert(link.start() == first);
  link.timeout();
  assert(link.needs_reset() && link.start() == reset);
  assert(link.accept_ack(0xE5));
  assert(link.start() == first);

  assert(link.accept_response(0x01));
  assert(link.start() == second);

  link.timeout();
  assert(link.start() == second);
  assert(link.accept_response(0x01));
  assert(link.start() == first);
  assert(link.accept_response(0x01));
  assert(link.start() == second);

  // A new optical session starts with SND_NKE and FCB=1.
  link.reset();
  assert(link.start() == reset);
  assert(link.accept_ack(0xE5));
  assert(link.start() == first);

  // Wrong-address frames leave the request pending and do not advance the FCB.
  link.set_address(0x2C);
  link.start();
  assert(link.accept_ack(0xE5));
  const auto wired_request = link.start();
  assert(!link.accept_response(0x01));
  assert(link.accept_response(0x2C));  // A matching response can follow in the same read.
  assert(link.start()[1] == 0x5B);
  assert(!link.accept_response(0x01));
  link.timeout();
  assert(link.start()[1] == 0x5B);  // Timeout retries the same FCB.
  assert(link.accept_response(0x2C));
  assert(link.start() == wired_request);

  // Point-to-point requests accept the slave's real primary address.
  link.set_address(0xFE);
  link.start();
  assert(link.accept_ack(0xE5));
  assert(!link.accept_response(0x2C));  // Only accept while awaiting data.
  link.start();
  assert(link.accept_response(0x2C));
  assert(link.start()[1] == 0x5B);
  assert(link.accept_response(0x01));
  assert(link.start()[1] == 0x7B);

  std::cout << "M-Bus link state tests passed\n";
}
