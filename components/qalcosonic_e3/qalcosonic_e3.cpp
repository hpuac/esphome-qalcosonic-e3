#include "qalcosonic_e3.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include <limits>

namespace esphome {
namespace qalcosonic_e3 {
static const char *const TAG = "qalcosonic_e3";

void QalcosonicE3::setup() {
  if (this->readout_failures_ != nullptr) this->readout_failures_->publish_state(0);
  this->set_timeout("first_read", 10000, [this]() {
    this->ready_ = true;
    this->update();
  });
}

void QalcosonicE3::update() {
  if (!this->ready_ || this->pending_) return;
  // Discard bytes left over from a previous request before starting a new one.
  uint8_t byte;
  for (size_t i = 0; i < 512 && this->available(); ++i) this->read_byte(&byte);
  this->receiver_.clear();
  this->pending_ = true;
  this->warned_ = false;
  this->requested_at_ = millis();
  static const uint8_t REQUEST[] = {0x10, 0x5B, 0x01, 0x5C, 0x16};
  ESP_LOGD(TAG, "Requesting QALCOSONIC E3 data");
  this->write_array(REQUEST, sizeof(REQUEST));
}

void QalcosonicE3::loop() {
  // Unsigned subtraction remains correct across millis() wraparound.
  if (this->pending_ && millis() - this->requested_at_ >= 2000) {
    this->pending_ = false;
    if (this->failures_ != std::numeric_limits<uint32_t>::max()) ++this->failures_;
    if (this->readout_failures_ != nullptr) this->readout_failures_->publish_state(this->failures_);
    if (this->readout_successful_ != nullptr) this->readout_successful_->publish_state(false);
    ESP_LOGW(TAG, "No valid response within 2 seconds (%u buffered bytes); readout failures since reboot=%lu",
             static_cast<unsigned>(this->receiver_.size()), static_cast<unsigned long>(this->failures_));
    this->receiver_.clear();
  }
  // Bound work per loop even when the UART is continuously receiving noise.
  for (size_t i = 0; i < 512 && this->available(); ++i) {
    uint8_t byte;
    if (!this->read_byte(&byte)) break;
    if (!this->pending_) continue;
    MeterData data;
    FrameResult error;
    if (this->receiver_.push(byte, data, error)) {
      this->publish_(data);
      this->pending_ = false;
      if (this->readout_successful_ != nullptr) this->readout_successful_->publish_state(true);
      ESP_LOGD(TAG, "QALCOSONIC E3 frame processed successfully");
    } else if (error != FrameResult::INCOMPLETE && !this->warned_) {
      ESP_LOGW(TAG, "%s", frame_result_message(error));
      this->warned_ = true;
    }
  }
}

void QalcosonicE3::publish_(const MeterData &data) {
  if (this->energy_ != nullptr && data.energy.present)
    this->energy_->publish_state(data.energy.value);
  if (this->volume_ != nullptr && data.volume.present)
    this->volume_->publish_state(data.volume.value);
  if (this->power_ != nullptr && data.power.present)
    this->power_->publish_state(data.power.value);
  if (this->flow_ != nullptr && data.flow.present)
    this->flow_->publish_state(data.flow.value);
  if (this->flow_temperature_ != nullptr && data.flow_temperature.present)
    this->flow_temperature_->publish_state(data.flow_temperature.value);
  if (this->return_temperature_ != nullptr && data.return_temperature.present)
    this->return_temperature_->publish_state(data.return_temperature.value);
  if (this->temperature_difference_ != nullptr && data.temperature_difference.present)
    this->temperature_difference_->publish_state(data.temperature_difference.value);
  if (this->error_code_ != nullptr && data.error_code.present)
    this->error_code_->publish_state(data.error_code.value);
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
  ESP_LOGCONFIG(TAG, "QALCOSONIC E3 (optical M-Bus, address 0x01)");
  LOG_UPDATE_INTERVAL(this);
  LOG_SENSOR("  ", "Energy", this->energy_);
  LOG_SENSOR("  ", "Volume", this->volume_);
  LOG_SENSOR("  ", "Power", this->power_);
  LOG_SENSOR("  ", "Flow", this->flow_);
  LOG_SENSOR("  ", "Flow Temperature", this->flow_temperature_);
  LOG_SENSOR("  ", "Return Temperature", this->return_temperature_);
  LOG_SENSOR("  ", "Temperature Difference", this->temperature_difference_);
  LOG_SENSOR("  ", "Error Code", this->error_code_);
  LOG_SENSOR("  ", "Battery Operating Duration", this->battery_operating_duration_);
  LOG_SENSOR("  ", "Operating Time Without Error", this->operating_time_without_error_);
  LOG_SENSOR("  ", "Protocol Version", this->protocol_version_);
  LOG_SENSOR("  ", "Readout Failures", this->readout_failures_);
  LOG_TEXT_SENSOR("  ", "Meter Datetime", this->meter_datetime_);
  LOG_TEXT_SENSOR("  ", "Error Start", this->error_start_);
  LOG_TEXT_SENSOR("  ", "Serial Number", this->serial_number_);
  LOG_TEXT_SENSOR("  ", "Manufacturer", this->manufacturer_);
  LOG_BINARY_SENSOR("  ", "Readout Successful", this->readout_successful_);
}
}  // namespace qalcosonic_e3
}  // namespace esphome
