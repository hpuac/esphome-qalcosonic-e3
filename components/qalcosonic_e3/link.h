#pragma once

#include <array>
#include <cstdint>

namespace esphome {
namespace qalcosonic_e3 {

// M-Bus link state shared by optical and wired interfaces.
class LinkState {
 public:
  void set_address(uint8_t address) {
    this->address_ = address;
    this->reset();
  }
  uint8_t address() const { return this->address_; }
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
      return this->short_frame_(0x40);  // SND_NKE
    }
    this->phase_ = Phase::AWAITING_DATA;
    const uint8_t control = this->next_fcb_ ? 0x7B : 0x5B;
    return this->short_frame_(control);
  }

  bool accept_ack(uint8_t byte) {
    if (!this->awaiting_ack() || byte != 0xE5) return false;
    this->phase_ = Phase::READY;
    this->next_fcb_ = true;
    this->failed_requests_ = 0;
    return true;
  }

  bool accept_response(uint8_t response_address) {
    if (this->phase_ != Phase::AWAITING_DATA || (this->address_ != 0xFE && response_address != this->address_))
      return false;
    this->next_fcb_ = !this->next_fcb_;
    this->failed_requests_ = 0;
    this->phase_ = Phase::READY;
    return true;
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
  std::array<uint8_t, 5> short_frame_(uint8_t control) const {
    return {{0x10, control, this->address_, static_cast<uint8_t>(control + this->address_), 0x16}};
  }
  uint8_t address_{0};  // Set from the required ESPHome address configuration before use.
  enum class Phase { NEED_RESET, AWAITING_ACK, READY, AWAITING_DATA };
  Phase phase_{Phase::NEED_RESET};
  bool next_fcb_{true};
  uint8_t failed_requests_{0};
};

}  // namespace qalcosonic_e3
}  // namespace esphome
