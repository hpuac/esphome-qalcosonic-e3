#include "qalcosonic_e3.h"

#include <algorithm>
#include <initializer_list>
#include <limits>

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome {
namespace qalcosonic_e3 {
static const char *const TAG = "qalcosonic_e3";

void QalcosonicE3::setup() {
  // PollingComponent starts its timer before setup(), even if the switch started off.
  if (!this->automatic_readout_enabled_) this->stop_poller();
  if (this->readout_failures_ != nullptr) this->readout_failures_->publish_state(0);
  this->set_timeout("first_read", 10000, [this]() {
    this->ready_ = true;
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
  if (!this->ready_ || this->pending_) {
    ESP_LOGW(TAG, "Cannot discover address before startup completes or while a read is pending");
    return;
  }
  this->manual_discovery_ = true;
  this->link_.start_discovery();
  this->request_read_();
}

void QalcosonicE3::request_read_() {
  if (!this->ready_ || this->pending_) return;
  this->cancel_timeout("post_ack_request");
  this->waiting_for_data_request_ = false;
  ++this->readout_generation_;
  this->link_.prepare_read(millis() - this->last_link_activity_at_);
  if (this->link_.discovering()) ESP_LOGI(TAG, "Starting M-Bus address discovery using 0xFE");
  // Discard bytes left over from a previous request before starting a new one.
  uint8_t byte;
  for (size_t i = 0; i < 512 && this->available(); ++i) this->read_byte(&byte);
  this->receiver_.clear();
  this->uart_trace_size_ = 0;
  this->uart_trace_truncated_ = false;
  this->pending_ = true;
  this->frame_error_ = FrameResult::INCOMPLETE;
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
  for (size_t offset = 0; offset < this->uart_trace_size_; offset += 32) {
    char line[32 * 3];
    size_t length = 0;
    const size_t end = std::min(offset + 32, this->uart_trace_size_);
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

void QalcosonicE3::loop() {
  // Bound work per loop even when the UART is continuously receiving noise.
  for (size_t i = 0; i < 512 && this->available(); ++i) {
    uint8_t byte;
    if (!this->read_byte(&byte)) break;
    if (!this->pending_) continue;
    if (this->uart_trace_size_ < this->uart_trace_.size())
      this->uart_trace_[this->uart_trace_size_++] = byte;
    else
      this->uart_trace_truncated_ = true;
    if (this->waiting_for_data_request_) continue;
    if (this->link_.awaiting_ack()) {
      if (this->link_.accept_ack(byte)) {
        this->last_link_activity_at_ = millis();
        this->waiting_for_data_request_ = true;
        const uint32_t generation = this->readout_generation_;
        ESP_LOGD(TAG, "Scheduling M-Bus REQ_UD2 100 ms after SND_NKE acknowledgement");
        this->set_timeout("post_ack_request", 100, [this, generation]() {
          if (generation != this->readout_generation_ || !this->pending_ || !this->waiting_for_data_request_) return;
          this->waiting_for_data_request_ = false;
          if (this->link_.needs_reset() || this->link_.awaiting_ack()) return;
          this->send_link_request_();
        });
      }
      continue;
    }
    MeterData data;
    FrameResult error;
    if (this->receiver_.push(byte, data, error)) {
      const bool discovering = this->link_.discovering();
      const uint8_t requested_address = this->link_.request_address();
      if (!this->link_.accept_response(data.response_address)) {
        ESP_LOGW(TAG, "Ignoring M-Bus response from address 0x%02X; expected 0x%02X", data.response_address,
                 requested_address);
        continue;
      }
      if (discovering) {
        ESP_LOGI(TAG, "Discovered M-Bus primary address: 0x%02X", data.response_address);
        if (this->link_.auto_address())
          ESP_LOGI(TAG, "Using discovered M-Bus address 0x%02X", this->link_.address());
        else
          ESP_LOGI(TAG, "Keeping configured M-Bus address 0x%02X", this->link_.configured_address());
      } else if (requested_address == 0xFE)
        ESP_LOGI(TAG, "Meter responded with primary address 0x%02X", data.response_address);
      this->pending_ = false;
      this->log_uart_trace_();
      this->last_link_activity_at_ = millis();
      if (this->manual_discovery_) {
        this->manual_discovery_ = false;
        continue;  // Manual discovery only reports the address, not measurements.
      }
      this->consecutive_failures_ = 0;
      this->measurements_invalidated_ = false;
      if (this->readout_failures_ != nullptr) this->readout_failures_->publish_state(0);
      this->publish_(data);
      if (this->readout_successful_ != nullptr) this->readout_successful_->publish_state(true);
      ESP_LOGD(TAG, "QALCOSONIC E3 frame processed successfully");
    } else if (error != FrameResult::INCOMPLETE && this->frame_error_ == FrameResult::INCOMPLETE) {
      this->frame_error_ = error;
    }
  }
  // Process already-buffered UART data before declaring a request timed out.
  // Unsigned subtraction remains correct across millis() wraparound.
  if (this->pending_ && !this->waiting_for_data_request_ && millis() - this->requested_at_ >= 2000) {
    this->log_uart_trace_();
    const bool awaiting_ack = this->link_.awaiting_ack();
    this->link_.timeout();
    if (!this->link_.needs_reset()) {
      // Continue the same logical readout; only exhaustion updates diagnostics.
      ESP_LOGD(TAG, "Retrying M-Bus REQ_UD2 with the same FCB");
      this->receiver_.clear();
      this->uart_trace_size_ = 0;
      this->uart_trace_truncated_ = false;
      this->frame_error_ = FrameResult::INCOMPLETE;
      this->send_link_request_();
      return;
    }
    this->pending_ = false;
    if (this->manual_discovery_) {
      this->link_.end_discovery();
      this->manual_discovery_ = false;
      ESP_LOGW(TAG, "M-Bus address discovery timed out; active address remains 0x%02X", this->link_.address());
      this->receiver_.clear();
      return;
    }
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
}

void QalcosonicE3::invalidate_measurements_() {
  for (auto *entity : {this->energy_, this->volume_, this->power_, this->flow_, this->flow_temperature_,
                       this->return_temperature_, this->temperature_difference_, this->battery_operating_duration_,
                       this->operating_time_without_error_, this->protocol_version_}) {
    if (entity != nullptr) entity->publish_state(std::numeric_limits<float>::quiet_NaN());
  }
}

void QalcosonicE3::publish_(const MeterData &data) {
  if (this->energy_ != nullptr && data.energy.present) this->energy_->publish_state(data.energy.value);
  if (this->volume_ != nullptr && data.volume.present) this->volume_->publish_state(data.volume.value);
  if (this->power_ != nullptr && data.power.present) this->power_->publish_state(data.power.value);
  if (this->flow_ != nullptr && data.flow.present) this->flow_->publish_state(data.flow.value);
  if (this->flow_temperature_ != nullptr && data.flow_temperature.present)
    this->flow_temperature_->publish_state(data.flow_temperature.value);
  if (this->return_temperature_ != nullptr && data.return_temperature.present)
    this->return_temperature_->publish_state(data.return_temperature.value);
  if (this->temperature_difference_ != nullptr && data.temperature_difference.present)
    this->temperature_difference_->publish_state(data.temperature_difference.value);
  if (this->error_code_ != nullptr && data.error_code.present) this->error_code_->publish_state(data.error_code.value);
  if (this->battery_operating_duration_ != nullptr && data.battery_operating_duration.present)
    this->battery_operating_duration_->publish_state(data.battery_operating_duration.value);
  if (this->operating_time_without_error_ != nullptr && data.operating_time_without_error.present)
    this->operating_time_without_error_->publish_state(data.operating_time_without_error.value);
  if (this->protocol_version_ != nullptr) this->protocol_version_->publish_state(data.protocol_version);
  if (this->meter_datetime_ != nullptr && data.meter_datetime.present)
    this->meter_datetime_->publish_state(data.meter_datetime.value);
  if (this->error_start_ != nullptr && data.error_start.present)
    this->error_start_->publish_state(data.error_start.value);
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
