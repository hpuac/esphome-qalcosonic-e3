#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>

#include "link.h"

using esphome::qalcosonic_e3::LinkState;
using esphome::qalcosonic_e3::Transport;

int main() {
  LinkState link;
  for (uint8_t address : {0x01, 0x2C, 0xFE}) {
    link.set_address(address);
    const uint8_t reset_checksum = address == 0x01 ? 0x41 : (address == 0x2C ? 0x6C : 0x3E);
    const uint8_t request_checksum = address == 0x01 ? 0x5C : (address == 0x2C ? 0x87 : 0x59);
    assert((link.start() == std::array<uint8_t, 5>{{0x10, 0x40, address, reset_checksum, 0x16}}));
    assert(link.accept_ack(0xE5));
    assert((link.start() == std::array<uint8_t, 5>{{0x10, 0x7B, address, static_cast<uint8_t>(0x7B + address), 0x16}}));
    assert(link.accept_response(address == 0xFE ? 0x2C : address));
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

  // Only optical transport expires its link session after inactivity.
  for (Transport transport : {Transport::OPTICAL, Transport::WIRED}) {
    link.set_transport(transport);
    link.set_address(0x2C);
    assert(link.start()[1] == 0x40);
    assert(link.accept_ack(0xE5));
    link.start();
    assert(link.accept_response(0x2C));
    link.prepare_read(299999);
    assert(!link.needs_reset());
    link.prepare_read(300001);
    assert(link.needs_reset() == (transport == Transport::OPTICAL));
    link.prepare_read(60 * 60 * 1000);
    auto request = link.start();
    assert(request[1] == (transport == Transport::OPTICAL ? 0x40 : 0x5B));
    if (transport == Transport::OPTICAL) {
      assert(link.accept_ack(0xE5));
      request = link.start();
    }
    // Failure recovery remains active for wired even with long polling intervals.
    for (unsigned i = 0; i < 3; ++i) {
      link.timeout();
      if (i < 2) assert(link.start() == request);
    }
    assert(link.needs_reset() && link.start()[1] == 0x40);
  }

  link.set_transport(Transport::WIRED);
  link.set_auto_address();
  assert(link.discovering() && link.request_address() == 0xFE);
  assert(link.start()[1] == 0x40);
  link.timeout();  // Missing discovery ACK retries 0xFE, never 0x01.
  assert(link.discovering() && link.start()[2] == 0xFE);
  assert(link.accept_ack(0xE5));
  const auto auto_request = link.start();
  assert(auto_request[1] == 0x7B && auto_request[2] == 0xFE);
  link.timeout();  // Hardware regression: retry REQ_UD2 without another SND_NKE.
  assert(link.discovering() && !link.needs_reset());
  assert(link.address() == 0xFE && link.start() == auto_request);
  assert(!link.accept_response(0xFD));
  assert(link.accept_response(0x2C));
  assert(!link.discovering() && link.address() == 0x2C);
  assert(link.start()[2] == 0x2C);  // Initialize the discovered targeted link.
  assert(link.accept_ack(0xE5));
  assert(link.start()[2] == 0x2C);
  assert(!link.accept_response(0x01));
  assert(link.accept_response(0x2C));
  link.prepare_read(60 * 60 * 1000);
  assert(link.start()[1] == 0x5B && link.address() == 0x2C);

  link.start_discovery();  // Manual rediscovery updates only auto mode.
  assert(link.start()[2] == 0xFE);
  assert(link.accept_ack(0xE5));
  link.start();
  assert(link.accept_response(0x01) && link.address() == 0x01);
  link.start_discovery();
  link.start();
  link.timeout();
  assert(link.discovering());
  link.end_discovery();                                   // Component ends a manual operation after ACK failure.
  assert(!link.discovering() && link.address() == 0x01);  // Keep last known address on failure.
  link.set_auto_address();                                // Reboot configuration discovers again; nothing is persisted.
  assert(link.discovering() && link.start()[2] == 0xFE);

  link.set_address(0x2C);
  link.start_discovery();
  assert(link.configured_address() == 0x2C && link.address() == 0x2C);
  assert(link.start()[2] == 0xFE);
  assert(link.accept_ack(0xE5));
  assert(link.start()[2] == 0xFE);
  assert(link.accept_response(0x01));
  assert(link.configured_address() == 0x2C && link.address() == 0x2C);
  assert(link.start()[2] == 0x2C);
  link.start_discovery();
  link.start();
  link.timeout();
  assert(link.discovering());
  link.end_discovery();
  assert(!link.discovering() && link.start()[2] == 0x2C);

  // All 0xFE requests accept only real primary addresses, on either transport.
  for (Transport transport : {Transport::OPTICAL, Transport::WIRED}) {
    for (unsigned mode = 0; mode < 3; ++mode) {
      for (uint8_t valid_address : {0x00, 0x01, 0x2C, 0xFA}) {
        link.set_transport(transport);
        if (mode == 0)
          link.set_address(0xFE);
        else if (mode == 1)
          link.set_auto_address();
        else {
          link.set_address(0x2C);
          link.start_discovery();
        }
        assert(link.start()[2] == 0xFE);
        assert(link.accept_ack(0xE5));
        const auto discovery_request = link.start();
        for (uint8_t invalid_address : {0xFB, 0xFC, 0xFD, 0xFE, 0xFF}) {
          assert(!link.accept_response(invalid_address));
          assert(link.request_address() == 0xFE);
          assert(!link.needs_reset());
        }
        // Invalid responses neither complete the read nor advance its FCB.
        if (mode == 0) {
          link.timeout();
          assert(link.start() == discovery_request);
        }
        assert(link.accept_response(valid_address));
        if (mode == 0)
          assert(link.start()[1] == 0x5B);
        else if (mode == 1)
          assert(link.address() == valid_address && !link.discovering());
        else
          assert(link.address() == 0x2C && !link.discovering());
      }
    }
  }

  enum class ReadMode { EXPLICIT_FE, AUTO, MANUAL_EXPLICIT, MANUAL_AUTO };
  auto prepare_discovery = [&](ReadMode mode) {
    if (mode == ReadMode::EXPLICIT_FE)
      link.set_address(0xFE);
    else if (mode == ReadMode::AUTO)
      link.set_auto_address();
    else if (mode == ReadMode::MANUAL_EXPLICIT) {
      link.set_address(0x09);
      link.start_discovery();
    } else {
      link.set_auto_address();
      link.start();
      assert(link.accept_ack(0xE5));
      link.start();
      assert(link.accept_response(0x09));
      link.start_discovery();
    }
  };
  for (Transport transport : {Transport::OPTICAL, Transport::WIRED}) {
    link.set_transport(transport);
    for (ReadMode mode : {ReadMode::EXPLICIT_FE, ReadMode::AUTO, ReadMode::MANUAL_EXPLICIT, ReadMode::MANUAL_AUTO}) {
      // The hardware responds to the same REQ_UD2 on retry, without another reset.
      for (unsigned timeouts : {1U, 2U}) {
        prepare_discovery(mode);
        assert(link.start()[1] == 0x40);
        assert(link.accept_ack(0xE5));
        const auto request = link.start();
        assert(request[1] == 0x7B && request[2] == 0xFE);
        for (unsigned i = 0; i < timeouts; ++i) {
          link.timeout();
          assert(!link.needs_reset());
          assert(link.discovering() == (mode != ReadMode::EXPLICIT_FE));
          assert(link.start() == request);
        }
        assert(!link.accept_response(0xFE));
        assert(link.accept_response(0x2C));
        assert(!link.discovering());
        if (mode == ReadMode::EXPLICIT_FE)
          assert(link.start()[1] == 0x5B);
        else {
          const uint8_t target = mode == ReadMode::MANUAL_EXPLICIT ? 0x09 : 0x2C;
          assert(link.address() == target);
          const auto reset_request = link.start();
          assert(reset_request[1] == 0x40 && reset_request[2] == target);
          assert(link.accept_ack(0xE5));
          assert(link.start()[2] == target);
          assert(!link.accept_response(target == 0x09 ? 0x2C : 0x09));
          assert(link.accept_response(target));
        }
      }

      prepare_discovery(mode);
      link.start();
      link.timeout();  // ACK failure still resets immediately.
      assert(link.needs_reset());
      assert(link.discovering() == (mode != ReadMode::EXPLICIT_FE));
      assert(link.start()[1] == 0x40);
      assert(link.accept_ack(0xE5));
      const auto request = link.start();
      for (unsigned i = 0; i < 3; ++i) {
        link.timeout();
        assert(link.discovering() == (mode != ReadMode::EXPLICIT_FE));
        assert(link.needs_reset() == (i == 2));
        if (i < 2) assert(link.start() == request);
      }
      if (mode == ReadMode::MANUAL_EXPLICIT || mode == ReadMode::MANUAL_AUTO) {
        link.end_discovery();  // Manual operation gives up only after exhaustion.
        assert(!link.discovering() && link.address() == 0x09);
        if (mode == ReadMode::MANUAL_EXPLICIT) assert(link.configured_address() == 0x09);
      } else {
        assert(link.address() == 0xFE && link.request_address() == 0xFE);
      }
      const auto reset_request = link.start();
      assert(reset_request[1] == 0x40);
      assert(reset_request[2] == (mode == ReadMode::MANUAL_EXPLICIT || mode == ReadMode::MANUAL_AUTO ? 0x09 : 0xFE));
    }
  }

  std::cout << "M-Bus link state tests passed\n";
}
