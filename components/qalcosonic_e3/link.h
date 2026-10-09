#pragma once

#include <array>
#include <cstdint>

namespace esphome {
namespace qalcosonic_e3 {

enum class Transport { OPTICAL, WIRED };

// M-Bus link state shared by optical and wired interfaces.
class LinkState {
 public:
  void set_address(uint8_t address) {
    this->configured_address_ = address;
    this->address_ = address;
    this->auto_address_ = false;
    this->discovering_ = false;
    this->reset();
  }
  void set_auto_address() {
    this->auto_address_ = true;
    this->address_discovered_ = false;
    this->address_ = 0xFE;
    this->start_discovery();
  }
  void set_transport(Transport transport) { this->transport_ = transport; }
  Transport transport() const { return this->transport_; }
  bool auto_address() const { return this->auto_address_; }
  uint8_t configured_address() const { return this->configured_address_; }
  uint8_t address() const { return this->address_; }
  uint8_t request_address() const { return this->discovering_ ? 0xFE : this->address_; }
  bool discovering() const { return this->discovering_; }
  void start_discovery() {
    this->discovering_ = true;
    this->reset();
  }
  void prepare_read(uint32_t idle_ms) {
    if (this->transport_ == Transport::OPTICAL && idle_ms >= 5 * 60 * 1000) this->reset();
  }
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
    const uint8_t requested = this->request_address();
    if (this->phase_ != Phase::AWAITING_DATA || (requested != 0xFE && response_address != requested) ||
        (this->discovering_ && response_address > 0xFA))
      return false;
    if (this->discovering_) {
      if (this->auto_address_) {
        this->address_ = response_address;
        this->address_discovered_ = true;
      }
      this->discovering_ = false;
      // The next targeted read initializes its own link/FCB session.
      this->reset();
      return true;
    }
    this->next_fcb_ = !this->next_fcb_;
    this->failed_requests_ = 0;
    this->phase_ = Phase::READY;
    return true;
  }

  void timeout() {
    if (this->discovering_) {
      // An unresolved auto address retries discovery, never an assumed address.
      this->discovering_ = this->auto_address_ && !this->address_discovered_;
      this->reset();
      return;
    }
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
    const uint8_t address = this->request_address();
    return {{0x10, control, address, static_cast<uint8_t>(control + address), 0x16}};
  }
  Transport transport_{Transport::OPTICAL};  // Required ESPHome setting overrides this before use.
  uint8_t configured_address_{0};
  bool auto_address_{false};
  bool address_discovered_{false};
  bool discovering_{false};
  uint8_t address_{0};  // Set from the required ESPHome address configuration before use.
  enum class Phase { NEED_RESET, AWAITING_ACK, READY, AWAITING_DATA };
  Phase phase_{Phase::NEED_RESET};
  bool next_fcb_{true};
  uint8_t failed_requests_{0};
};

}  // namespace qalcosonic_e3
}  // namespace esphome
