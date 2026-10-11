#include "qalcosonic_e3.h"

#include <algorithm>
#include <initializer_list>
#include <limits>

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome {
namespace qalcosonic_e3 {
static const char *const TAG = "qalcosonic_e3";
namespace {
constexpr uint32_t STARTUP_DELAY_MS = 10000;
constexpr uint32_t POST_ACK_DELAY_MS = 100;
constexpr uint32_t RESPONSE_TIMEOUT_MS = 2000;
constexpr size_t MAX_RX_BYTES_PER_LOOP = 512;

template <typename Entity, typename T>
void publish_if_present(Entity *entity, const Value<T> &value) {
  if (entity != nullptr && value.present) entity->publish_state(value.value);
}
}  // namespace

void QalcosonicE3::setup() {
  // PollingComponent starts its timer before setup(), even if the switch started off.
  if (!this->automatic_readout_enabled_) this->stop_poller();
  if (this->readout_failures_ != nullptr) this->readout_failures_->publish_state(0);
  this->set_timeout("first_read", STARTUP_DELAY_MS, [this]() {
    this->readout_state_ = ReadoutState::IDLE;
    this->update();
  });
}

void QalcosonicE3::update() {
  if (this->automatic_readout_enabled_) this->request_read_();
}

void QalcosonicE3::set_automatic_readout_enabled(bool enabled) {
  if (this->automatic_readout_enabled_ == enabled) return;
  this->automatic_readout_enabled_ = enabled;
  if (enabled)
    this->start_poller();
  else
    this->stop_poller();
}

void QalcosonicE3::read_now() { this->request_read_(); }

void QalcosonicE3::discover_address() {
  if (this->readout_state_ != ReadoutState::IDLE) {
    ESP_LOGW(TAG, "Cannot discover address before startup completes or while a read is pending");
    return;
  }
  this->readout_purpose_ = ReadoutPurpose::ADDRESS_DISCOVERY;
  this->link_.start_discovery();
  this->request_read_();
}

void QalcosonicE3::request_read_() {
  if (this->readout_state_ != ReadoutState::IDLE) return;
  this->cancel_timeout("post_ack_request");
  ++this->readout_generation_;
  this->link_.prepare_read(millis() - this->last_link_activity_at_);
  if (this->link_.discovering()) ESP_LOGI(TAG, "Starting M-Bus address discovery using 0xFE");
  // Discard bytes left over from a previous request before starting a new one.
  uint8_t byte;
  for (size_t i = 0; i < MAX_RX_BYTES_PER_LOOP && this->available(); ++i) this->read_byte(&byte);
  this->reset_receive_attempt_();
  this->readout_state_ = ReadoutState::WAITING_FOR_REPLY;
  this->send_link_request_();
}

void QalcosonicE3::send_link_request_() {
  const bool initializing = this->link_.needs_reset();
  const auto request = this->link_.start();
  this->requested_at_ = millis();
  ESP_LOGD(TAG, "Sending M-Bus %s (0x%02X) to address 0x%02X", initializing ? "SND_NKE" : "REQ_UD2", request[1],
           request[2]);
  this->write_array(request.data(), request.size());
}

void QalcosonicE3::log_uart_trace_() {
#if ESPHOME_LOG_LEVEL >= ESPHOME_LOG_LEVEL_VERBOSE
  ESP_LOGV(TAG, "UART RX: %u byte(s)%s", static_cast<unsigned>(this->uart_trace_size_),
           this->uart_trace_truncated_ ? " (trace truncated)" : "");
  static constexpr char HEX[] = "0123456789ABCDEF";
  static constexpr size_t BYTES_PER_LINE = 32;
  for (size_t offset = 0; offset < this->uart_trace_size_; offset += BYTES_PER_LINE) {
    char line[BYTES_PER_LINE * 3];
    size_t length = 0;
    const size_t end = std::min(offset + BYTES_PER_LINE, this->uart_trace_size_);
    for (size_t i = offset; i < end; ++i) {
      if (i != offset) line[length++] = ' ';
      const uint8_t byte = this->uart_trace_[i];
      line[length++] = HEX[byte >> 4];
      line[length++] = HEX[byte & 0x0F];
    }
    line[length] = '\0';
    ESP_LOGV(TAG, "UART RX [%u-%u]: %s", static_cast<unsigned>(offset), static_cast<unsigned>(end - 1), line);
  }
#endif
}

void QalcosonicE3::reset_receive_attempt_() {
  this->receiver_.clear();
  this->uart_trace_size_ = 0;
  this->uart_trace_truncated_ = false;
  this->frame_error_ = FrameResult::INCOMPLETE;
}

void QalcosonicE3::record_uart_byte_(uint8_t byte) {
  if (this->uart_trace_size_ < this->uart_trace_.size())
    this->uart_trace_[this->uart_trace_size_++] = byte;
  else
    this->uart_trace_truncated_ = true;
}

void QalcosonicE3::handle_ack_(uint8_t byte) {
  if (!this->link_.accept_ack(byte)) return;
  this->last_link_activity_at_ = millis();
  this->readout_state_ = ReadoutState::ACK_DELAY;
  const uint32_t generation = this->readout_generation_;
  ESP_LOGD(TAG, "Scheduling M-Bus REQ_UD2 100 ms after SND_NKE acknowledgement");
  this->set_timeout("post_ack_request", POST_ACK_DELAY_MS, [this, generation]() {
    if (generation != this->readout_generation_ || this->readout_state_ != ReadoutState::ACK_DELAY) return;
    this->readout_state_ = ReadoutState::WAITING_FOR_REPLY;
    if (this->link_.needs_reset() || this->link_.awaiting_ack()) return;
    this->send_link_request_();
  });
}

void QalcosonicE3::handle_response_(const MeterData &data) {
  const bool discovering = this->link_.discovering();
  const uint8_t requested_address = this->link_.request_address();
  if (!this->link_.accept_response(data.response_address)) {
    ESP_LOGW(TAG, "Ignoring M-Bus response from address 0x%02X; expected 0x%02X", data.response_address,
             requested_address);
    return;
  }
  if (discovering) {
    ESP_LOGI(TAG, "Discovered M-Bus primary address: 0x%02X", data.response_address);
    if (this->link_.auto_address())
      ESP_LOGI(TAG, "Using discovered M-Bus address 0x%02X", this->link_.address());
    else
      ESP_LOGI(TAG, "Keeping configured M-Bus address 0x%02X", this->link_.configured_address());
  } else if (requested_address == mbus::POINT_TO_POINT_ADDRESS)
    ESP_LOGI(TAG, "Meter responded with primary address 0x%02X", data.response_address);
  this->readout_state_ = ReadoutState::IDLE;
  this->log_uart_trace_();
  this->last_link_activity_at_ = millis();
  if (this->readout_purpose_ == ReadoutPurpose::ADDRESS_DISCOVERY) {
    this->readout_purpose_ = ReadoutPurpose::MEASUREMENTS;
    return;  // Manual discovery only reports the address, not measurements.
  }
  this->consecutive_failures_ = 0;
  this->measurements_invalidated_ = false;
  if (this->readout_failures_ != nullptr) this->readout_failures_->publish_state(0);
  this->publish_(data);
  if (this->readout_successful_ != nullptr) this->readout_successful_->publish_state(true);
  ESP_LOGD(TAG, "QALCOSONIC E3 frame processed successfully");
}

void QalcosonicE3::retry_readout_() {
  // Continue the same logical readout; only exhaustion updates diagnostics.
  ESP_LOGD(TAG, "Retrying M-Bus REQ_UD2 with the same FCB");
  this->reset_receive_attempt_();
  this->send_link_request_();
}

void QalcosonicE3::finish_failed_readout_(bool awaiting_ack) {
  if (this->consecutive_failures_ != std::numeric_limits<uint32_t>::max()) ++this->consecutive_failures_;
  if (this->unavailable_after_failures_ != 0 && !this->measurements_invalidated_ &&
      this->consecutive_failures_ >= this->unavailable_after_failures_) {
    this->invalidate_measurements_();
    this->measurements_invalidated_ = true;
  }
  if (this->readout_failures_ != nullptr) this->readout_failures_->publish_state(this->consecutive_failures_);
  if (this->readout_successful_ != nullptr) this->readout_successful_->publish_state(false);
  if (this->frame_error_ != FrameResult::INCOMPLETE) ESP_LOGW(TAG, "%s", frame_result_message(this->frame_error_));
  ESP_LOGW(TAG, "%s within 2 seconds (%u buffered bytes); consecutive readout failures=%lu",
           awaiting_ack ? "No SND_NKE acknowledgement" : "No valid response",
           static_cast<unsigned>(this->receiver_.size()), static_cast<unsigned long>(this->consecutive_failures_));
  this->receiver_.clear();
}

void QalcosonicE3::handle_timeout_() {
  // Unsigned subtraction remains correct across millis() wraparound.
  if (this->readout_state_ != ReadoutState::WAITING_FOR_REPLY || millis() - this->requested_at_ < RESPONSE_TIMEOUT_MS)
    return;
  this->log_uart_trace_();
  const bool awaiting_ack = this->link_.awaiting_ack();
  this->link_.timeout();
  if (!this->link_.needs_reset()) {
    this->retry_readout_();
    return;
  }
  this->readout_state_ = ReadoutState::IDLE;
  if (this->readout_purpose_ == ReadoutPurpose::ADDRESS_DISCOVERY) {
    this->link_.end_discovery();
    this->readout_purpose_ = ReadoutPurpose::MEASUREMENTS;
    ESP_LOGW(TAG, "M-Bus address discovery timed out; active address remains 0x%02X", this->link_.address());
    this->receiver_.clear();
    return;
  }
  this->finish_failed_readout_(awaiting_ack);
}

void QalcosonicE3::loop() {
  // Bound work per loop even when the UART is continuously receiving noise.
  for (size_t i = 0; i < MAX_RX_BYTES_PER_LOOP && this->available(); ++i) {
    uint8_t byte;
    if (!this->read_byte(&byte)) break;
    if (!this->readout_pending_()) continue;
    this->record_uart_byte_(byte);
    if (this->readout_state_ == ReadoutState::ACK_DELAY) continue;
    if (this->link_.awaiting_ack()) {
      this->handle_ack_(byte);
      continue;
    }
    MeterData data;
    FrameResult error;
    if (this->receiver_.push(byte, data, error))
      this->handle_response_(data);
    else if (error != FrameResult::INCOMPLETE && this->frame_error_ == FrameResult::INCOMPLETE)
      this->frame_error_ = error;
  }
  // Process already-buffered UART data before declaring a request timed out.
  this->handle_timeout_();
}

void QalcosonicE3::invalidate_measurements_() {
  for (auto *entity : {this->energy_, this->volume_, this->power_, this->flow_, this->flow_temperature_,
                       this->return_temperature_, this->temperature_difference_, this->battery_operating_duration_,
                       this->operating_time_without_error_, this->protocol_version_}) {
    if (entity != nullptr) entity->publish_state(std::numeric_limits<float>::quiet_NaN());
  }
}

void QalcosonicE3::publish_(const MeterData &data) {
  publish_if_present(this->energy_, data.energy);
  publish_if_present(this->volume_, data.volume);
  publish_if_present(this->power_, data.power);
  publish_if_present(this->flow_, data.flow);
  publish_if_present(this->flow_temperature_, data.flow_temperature);
  publish_if_present(this->return_temperature_, data.return_temperature);
  publish_if_present(this->temperature_difference_, data.temperature_difference);
  publish_if_present(this->error_code_, data.error_code);
  publish_if_present(this->battery_operating_duration_, data.battery_operating_duration);
  publish_if_present(this->operating_time_without_error_, data.operating_time_without_error);
  if (this->protocol_version_ != nullptr) this->protocol_version_->publish_state(data.protocol_version);
  publish_if_present(this->meter_datetime_, data.meter_datetime);
  publish_if_present(this->error_start_, data.error_start);
  if (this->serial_number_ != nullptr) this->serial_number_->publish_state(data.serial_number);
  if (this->manufacturer_ != nullptr) this->manufacturer_->publish_state(data.manufacturer);
}

void QalcosonicE3::dump_config() {
  ESP_LOGCONFIG(TAG, "QALCOSONIC E3 (M-Bus, address 0x%02X)", this->link_.address());
  ESP_LOGCONFIG(TAG, "  Transport: %s", this->link_.transport() == Transport::OPTICAL ? "optical" : "wired");
  if (this->link_.auto_address())
    ESP_LOGCONFIG(TAG, "  Configured address: auto (discovered at runtime, not persisted)");
  else
    ESP_LOGCONFIG(TAG, "  Configured address: 0x%02X", this->link_.configured_address());
  LOG_UPDATE_INTERVAL(this);
  ESP_LOGCONFIG(TAG, "  Unavailable after consecutive failures: %lu (0 = disabled)",
                static_cast<unsigned long>(this->unavailable_after_failures_));
  LOG_SENSOR("  ", "Energy", this->energy_);
  LOG_SENSOR("  ", "Volume", this->volume_);
  LOG_SENSOR("  ", "Power", this->power_);
  LOG_SENSOR("  ", "Flow", this->flow_);
  LOG_SENSOR("  ", "Flow Temperature", this->flow_temperature_);
  LOG_SENSOR("  ", "Return Temperature", this->return_temperature_);
  LOG_SENSOR("  ", "Temperature Difference", this->temperature_difference_);
  LOG_TEXT_SENSOR("  ", "Error Code", this->error_code_);
  LOG_SENSOR("  ", "Battery Operating Duration", this->battery_operating_duration_);
  LOG_SENSOR("  ", "Operating Time Without Error", this->operating_time_without_error_);
  LOG_SENSOR("  ", "Protocol Version", this->protocol_version_);
  LOG_SENSOR("  ", "Consecutive Readout Failures", this->readout_failures_);
  LOG_TEXT_SENSOR("  ", "Meter Datetime", this->meter_datetime_);
  LOG_TEXT_SENSOR("  ", "Error Start", this->error_start_);
  LOG_TEXT_SENSOR("  ", "Serial Number", this->serial_number_);
  LOG_TEXT_SENSOR("  ", "Manufacturer", this->manufacturer_);
  LOG_BINARY_SENSOR("  ", "Readout Successful", this->readout_successful_);
}

void AutomaticReadoutSwitch::setup() {
  auto initial_state = this->get_initial_state_with_restore_mode();
  if (initial_state.has_value()) this->write_state(*initial_state);
}

void AutomaticReadoutSwitch::write_state(bool state) {
  this->parent_->set_automatic_readout_enabled(state);
  this->publish_state(state);
}

void AutomaticReadoutSwitch::dump_config() { LOG_SWITCH("  ", "Automatic Readout", this); }
}  // namespace qalcosonic_e3
}  // namespace esphome
