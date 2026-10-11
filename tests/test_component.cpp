#include <algorithm>
#include <cassert>
#include <cmath>
#include <fstream>
#include <iostream>
#include <vector>

#include "qalcosonic_e3.h"

using namespace esphome;
using namespace esphome::qalcosonic_e3;

enum class Mode { EXPLICIT_FE, AUTO, TARGETED, MANUAL_EXPLICIT, MANUAL_AUTO };

class TestComponent : public QalcosonicE3 {
 public:
  bool pending() const { return this->pending_; }
  bool waiting_for_data_request() const { return this->waiting_for_data_request_; }
  void abort_readout() {
    this->pending_ = false;
    this->waiting_for_data_request_ = false;
    this->manual_discovery_ = false;
    this->link_.reset();
    this->cancel_timeout("post_ack_request");
  }
  uint32_t failures() const { return this->consecutive_failures_; }
  const LinkState &link() const { return this->link_; }
  size_t buffered() const { return this->receiver_.size(); }
};

struct Readout {
  TestComponent device;
  sensor::Sensor failures, energy;
  binary_sensor::BinarySensor successful;
  Mode mode;

  Readout(Transport transport, Mode mode) : mode(mode) {
    test::now = 10000;
    test::logs.clear();
    device.set_transport(transport);
    if (mode == Mode::AUTO || mode == Mode::MANUAL_AUTO)
      device.set_auto_address();
    else
      device.set_address(mode == Mode::EXPLICIT_FE ? 0xFE : (mode == Mode::MANUAL_EXPLICIT ? 0x09 : 0x2C));
    device.set_readout_failures(&failures);
    device.set_readout_successful(&successful);
    device.set_energy(&energy);
    device.set_unavailable_after_failures(1);
    energy.publish_state(100.0f);
    device.set_automatic_readout_enabled(false);
    device.setup();
    device.run_timeout();  // Complete startup without starting a scheduled read.
    if (manual())
      device.discover_address();
    else
      device.read_now();
    assert(device.pending() && device.tx.size() == 1);
    assert(device.tx.back()[1] == 0x40);
  }

  bool manual() const { return mode == Mode::MANUAL_EXPLICIT || mode == Mode::MANUAL_AUTO; }
  void ack() {
    device.rx.push_back(0xE5);
    device.loop();
    assert(device.tx.size() == 1 && device.pending());
    assert(device.waiting_for_data_request());
    unchanged_diagnostics();
    device.read_now();
    device.set_automatic_readout_enabled(true);
    device.update();
    device.set_automatic_readout_enabled(false);
    assert(device.tx.size() == 1);
    test::now += 99;
    device.run_scheduler();
    device.loop();
    assert(device.tx.size() == 1 && device.pending());
    unchanged_diagnostics();
    ++test::now;
    device.run_scheduler();
    assert(device.tx.size() == 2 && device.tx.back()[1] == 0x7B);
    assert(device.pending() && !device.waiting_for_data_request());
    unchanged_diagnostics();
  }
  void timeout() {
    test::now += 2000;
    device.loop();
  }
  void receive(const std::vector<uint8_t> &frame) {
    device.rx.insert(device.rx.end(), frame.begin(), frame.end());
    device.loop();
  }
  void unchanged_diagnostics() const {
    assert(device.failures() == 0);
    assert(failures.states == std::vector<float>{0});
    assert(successful.states.empty());
    assert(energy.states == std::vector<float>{100});
  }
};

std::vector<uint8_t> with_address(std::vector<uint8_t> frame, uint8_t address) {
  frame[5] = address;
  uint8_t checksum = 0;
  for (size_t i = 4; i < frame.size() - 2; ++i) checksum += frame[i];
  frame[frame.size() - 2] = checksum;
  return frame;
}

int main(int argc, char **argv) {
  assert(argc == 2);
  std::ifstream input(argv[1]);
  std::vector<uint8_t> frame;
  unsigned byte;
  while (input >> std::hex >> byte) frame.push_back(static_cast<uint8_t>(byte));
  assert(frame.size() == 99);
  for (Transport transport : {Transport::OPTICAL, Transport::WIRED}) {
    for (Mode mode : {Mode::EXPLICIT_FE, Mode::AUTO, Mode::TARGETED, Mode::MANUAL_EXPLICIT, Mode::MANUAL_AUTO}) {
      // A late ACK and a delayed scheduler must not consume the data timeout.
      Readout delayed(transport, mode);
      test::now += 1900;
      delayed.device.rx.push_back(0xE5);
      delayed.device.loop();
      assert(delayed.device.pending() && delayed.device.tx.size() == 1);
      const auto old_callback = delayed.device.timeout_callback("post_ack_request");
      delayed.receive(frame);  // Unsolicited data before REQ_UD2 cannot finish the readout.
      assert(delayed.device.pending() && delayed.device.tx.size() == 1);
      delayed.unchanged_diagnostics();
      test::now += 5000;
      delayed.device.loop();  // Scheduler has not run yet: no data request, no timeout.
      assert(delayed.device.pending() && delayed.device.tx.size() == 1);
      delayed.unchanged_diagnostics();
      delayed.device.run_scheduler();
      const auto delayed_request = delayed.device.tx.back();
      assert(delayed.device.tx.size() == 2 && delayed_request[1] == 0x7B);
      test::now += 1999;
      delayed.device.loop();
      assert(delayed.device.tx.size() == 2 && delayed.device.pending());
      delayed.unchanged_diagnostics();
      ++test::now;
      delayed.device.loop();
      assert(delayed.device.tx.size() == 3 && delayed.device.tx.back() == delayed_request);
      delayed.unchanged_diagnostics();  // Retry remains immediate, without another timer.
      delayed.receive(frame);
      assert(!delayed.device.pending());
      old_callback();  // A duplicated callback cannot send after completion.
      assert(delayed.device.tx.size() == 3);

      // Cancelled/stale callbacks cannot enter an aborted or newer readout.
      Readout restarted(transport, mode);
      restarted.device.rx.push_back(0xE5);
      restarted.device.loop();
      const auto stale_callback = restarted.device.timeout_callback("post_ack_request");
      restarted.device.abort_readout();
      stale_callback();
      assert(restarted.device.tx.size() == 1);
      restarted.device.read_now();
      assert(restarted.device.tx.size() == 2 && restarted.device.tx.back()[1] == 0x40);
      restarted.device.rx.push_back(0xE5);
      restarted.device.loop();
      stale_callback();  // New readout is pending and waiting, but generation differs.
      assert(restarted.device.pending() && restarted.device.tx.size() == 2);
      test::now += 100;
      restarted.device.run_scheduler();
      assert(restarted.device.tx.size() == 3 && restarted.device.tx.back()[1] == 0x7B);
      stale_callback();
      assert(restarted.device.tx.size() == 3);

      Readout reset(transport, mode);
      reset.device.rx.push_back(0xE5);
      reset.device.loop();
      reset.device.set_address(0x2C);  // Reset link before the scheduled request fires.
      test::now += 100;
      reset.device.run_scheduler();
      assert(reset.device.tx.size() == 1);
      reset.unchanged_diagnostics();

      for (unsigned timeouts : {1U, 2U}) {
        Readout readout(transport, mode);
        readout.ack();
        const auto request = readout.device.tx.back();
        assert(request[2] == (mode == Mode::TARGETED ? 0x2C : 0xFE));
        for (unsigned i = 0; i < timeouts; ++i) {
          // Partial/corrupt input from the attempt must not survive into the retry.
          readout.device.rx.insert(readout.device.rx.end(), {0x68, 0xFF, 0xFF, 0x68});
          readout.device.loop();
          readout.timeout();
          assert(readout.device.pending());
          assert(readout.device.tx.size() == 3 + i);
          assert(readout.device.tx.back() == request);
          assert(readout.device.buffered() == 0);
          readout.unchanged_diagnostics();
          const auto writes = readout.device.tx.size();
          readout.device.read_now();  // No external trigger is needed or allowed to restart it.
          assert(readout.device.tx.size() == writes);
        }
        readout.receive(frame);
        assert(!readout.device.pending());
        assert(readout.device.failures() == 0);
        if (readout.manual())
          readout.unchanged_diagnostics();
        else {
          assert(readout.successful.states == std::vector<bool>{true});
          assert(readout.failures.states == (std::vector<float>{0, 0}));
          assert(readout.energy.states.size() == 2 && std::fabs(readout.energy.states.back() - 0.260f) < 0.0001f);
        }
        if (mode == Mode::AUTO || mode == Mode::MANUAL_AUTO || mode == Mode::MANUAL_EXPLICIT) {
          assert(!readout.device.link().discovering());
          const uint8_t target = mode == Mode::MANUAL_EXPLICIT ? 0x09 : 0x2C;
          assert(readout.device.link().address() == target);
          readout.device.read_now();
          assert(readout.device.tx.back()[1] == 0x40 && readout.device.tx.back()[2] == target);
          if (mode != Mode::MANUAL_EXPLICIT) {
            // Normal reads after discovery also execute retries internally.
            const auto failure_states = readout.failures.states;
            const auto success_states = readout.successful.states;
            const auto energy_states = readout.energy.states;
            readout.device.rx.push_back(0xE5);
            readout.device.loop();
            assert(readout.device.waiting_for_data_request());
            test::now += 100;
            readout.device.run_scheduler();
            const auto targeted_request = readout.device.tx.back();
            assert(targeted_request[1] == 0x7B && targeted_request[2] == 0x2C);
            readout.timeout();
            assert(readout.device.pending() && readout.device.tx.back() == targeted_request);
            assert(readout.failures.states == failure_states);
            assert(readout.successful.states == success_states);
            assert(readout.energy.states == energy_states);
            readout.receive(frame);
            assert(!readout.device.pending() && readout.device.failures() == 0);
            assert(readout.successful.states.size() == success_states.size() + 1);
            assert(readout.successful.states.back());
          }
        } else {
          readout.device.read_now();
          assert(readout.device.tx.back()[1] == 0x5B);  // One accepted response advances FCB once.
        }
      }

      // Three data timeouts constitute one failed logical readout.
      Readout exhausted(transport, mode);
      exhausted.ack();
      const auto request = exhausted.device.tx.back();
      for (unsigned attempt = 0; attempt < 3; ++attempt) {
        exhausted.timeout();
        if (attempt < 2) {
          assert(exhausted.device.pending() && exhausted.device.tx.back() == request);
          exhausted.unchanged_diagnostics();
        }
      }
      assert(!exhausted.device.pending() && exhausted.device.tx.size() == 4);
      assert(exhausted.device.link().needs_reset());
      exhausted.device.loop();  // Exhaustion is reported once, not on each loop.
      if (exhausted.manual()) {
        exhausted.unchanged_diagnostics();
        assert(std::count(test::logs.begin(), test::logs.end(),
                          "M-Bus address discovery timed out; active address remains 0x%02X") == 1);
      } else {
        assert(exhausted.device.failures() == 1);
        assert(exhausted.failures.states == (std::vector<float>{0, 1}));
        assert(exhausted.successful.states == std::vector<bool>{false});
        assert(exhausted.energy.states.size() == 2 && std::isnan(exhausted.energy.states.back()));
      }
      if (mode == Mode::AUTO) assert(exhausted.device.link().discovering());
      exhausted.device.read_now();
      assert(exhausted.device.tx.back()[1] == 0x40);
      if (!exhausted.manual()) {
        // A retry must also preserve an existing failure streak until recovery.
        exhausted.device.rx.push_back(0xE5);
        exhausted.device.loop();
        assert(exhausted.device.waiting_for_data_request());
        test::now += 100;
        exhausted.device.run_scheduler();
        const auto recovery_request = exhausted.device.tx.back();
        exhausted.timeout();
        assert(exhausted.device.pending() && exhausted.device.tx.back() == recovery_request);
        assert(exhausted.device.failures() == 1);
        assert(exhausted.failures.states == (std::vector<float>{0, 1}));
        assert(exhausted.successful.states == std::vector<bool>{false});
        assert(exhausted.energy.states.size() == 2);
        exhausted.receive(frame);
        assert(!exhausted.device.pending() && exhausted.device.failures() == 0);
        assert(exhausted.failures.states == (std::vector<float>{0, 1, 0}));
        assert(exhausted.successful.states == (std::vector<bool>{false, true}));
      }

      // Missing SND_NKE acknowledgement terminates immediately without a data retry.
      Readout no_ack(transport, mode);
      no_ack.timeout();
      assert(!no_ack.device.pending() && no_ack.device.tx.size() == 1);
      assert(no_ack.device.link().needs_reset());
      if (no_ack.manual())
        no_ack.unchanged_diagnostics();
      else {
        assert(no_ack.device.failures() == 1);
        assert(no_ack.successful.states == std::vector<bool>{false});
      }

      // Filtering stays active across retries and never publishes rejected frames.
      Readout filtered(transport, mode);
      filtered.ack();
      for (uint8_t invalid : {0xFB, 0xFC, 0xFD, 0xFE, 0xFF}) {
        filtered.receive(with_address(frame, invalid));
        assert(filtered.device.pending() && filtered.device.tx.size() == 2);
        filtered.unchanged_diagnostics();
      }
      if (mode == Mode::TARGETED) {
        filtered.receive(with_address(frame, 0x01));
        assert(filtered.device.pending());
        filtered.unchanged_diagnostics();
      }
      filtered.timeout();
      assert(filtered.device.tx.size() == 3 && filtered.device.tx[1] == filtered.device.tx[2]);
      filtered.receive(frame);
      assert(!filtered.device.pending());
    }
  }
  std::cout << "Component readout retry tests passed\n";
}
