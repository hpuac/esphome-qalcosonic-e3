#pragma once

#include <array>
#include <cstdint>

namespace esphome {
namespace qalcosonic_e3 {

// M-Bus link state for the meter's primary address 0x01.
class LinkState {
 public:
  bool needs_reset() const { return this->phase_ == Phase::NEED_RESET; }
  bool awaiting_ack() const { return this->phase_ == Phase::AWAITING_ACK; }
  void reset() {
    this->phase_ = Phase::NEED_RESET;
    this->next_fcb_ = true;
    this->failed_requests_ = 0;
  }

  std::array<uint8_t, 5> start() {
    if (this->needs_reset()) {
      this->phase_ = Phase::AWAITING_ACK;
      return {{0x10, 0x40, 0x01, 0x41, 0x16}};  // SND_NKE
    }
    this->phase_ = Phase::AWAITING_DATA;
    const uint8_t control = this->next_fcb_ ? 0x7B : 0x5B;
    return {{0x10, control, 0x01, static_cast<uint8_t>(control + 0x01), 0x16}};
  }

  bool accept_ack(uint8_t byte) {
    if (!this->awaiting_ack() || byte != 0xE5) return false;
    this->phase_ = Phase::READY;
    this->next_fcb_ = true;
    this->failed_requests_ = 0;
    return true;
  }

  void accept_response() {
    if (this->phase_ != Phase::AWAITING_DATA) return;
    this->next_fcb_ = !this->next_fcb_;
    this->failed_requests_ = 0;
    this->phase_ = Phase::READY;
  }

  void timeout() {
    if (this->awaiting_ack()) {
      this->reset();
    } else if (this->phase_ == Phase::AWAITING_DATA) {
      // Retry the same FCB twice; then resynchronize on the next readout.
      if (++this->failed_requests_ >= 3)
        this->reset();
      else
        this->phase_ = Phase::READY;
    }
  }

 private:
  enum class Phase { NEED_RESET, AWAITING_ACK, READY, AWAITING_DATA };
  Phase phase_{Phase::NEED_RESET};
  bool next_fcb_{true};
  uint8_t failed_requests_{0};
};

}  // namespace qalcosonic_e3
}  // namespace esphome
