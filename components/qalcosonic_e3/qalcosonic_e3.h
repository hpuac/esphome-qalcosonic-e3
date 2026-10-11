#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/button/button.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/uart/uart.h"
#include "esphome/core/component.h"
#include "link.h"
#include "parser.h"

namespace esphome {
namespace qalcosonic_e3 {
class QalcosonicE3 : public PollingComponent, public uart::UARTDevice {
 public:
  void setup() override;
  void loop() override;
  void update() override;
  void read_now();
  void discover_address();
  void set_automatic_readout_enabled(bool enabled);
  void dump_config() override;
  void set_address(uint8_t address) { this->link_.set_address(address); }
  void set_auto_address() { this->link_.set_auto_address(); }
  void set_transport(Transport transport) { this->link_.set_transport(transport); }
  float get_setup_priority() const override { return setup_priority::LATE; }
  void set_energy(sensor::Sensor *entity) { this->energy_ = entity; }
  void set_volume(sensor::Sensor *entity) { this->volume_ = entity; }
  void set_power(sensor::Sensor *entity) { this->power_ = entity; }
  void set_flow(sensor::Sensor *entity) { this->flow_ = entity; }
  void set_flow_temperature(sensor::Sensor *entity) { this->flow_temperature_ = entity; }
  void set_return_temperature(sensor::Sensor *entity) { this->return_temperature_ = entity; }
  void set_temperature_difference(sensor::Sensor *entity) { this->temperature_difference_ = entity; }
  void set_error_code(text_sensor::TextSensor *entity) { this->error_code_ = entity; }
  void set_battery_operating_duration(sensor::Sensor *entity) { this->battery_operating_duration_ = entity; }
  void set_operating_time_without_error(sensor::Sensor *entity) { this->operating_time_without_error_ = entity; }
  void set_protocol_version(sensor::Sensor *entity) { this->protocol_version_ = entity; }
  void set_readout_failures(sensor::Sensor *entity) { this->readout_failures_ = entity; }
  void set_meter_datetime(text_sensor::TextSensor *entity) { this->meter_datetime_ = entity; }
  void set_error_start(text_sensor::TextSensor *entity) { this->error_start_ = entity; }
  void set_serial_number(text_sensor::TextSensor *entity) { this->serial_number_ = entity; }
  void set_manufacturer(text_sensor::TextSensor *entity) { this->manufacturer_ = entity; }
  void set_readout_successful(binary_sensor::BinarySensor *entity) { this->readout_successful_ = entity; }
  void set_unavailable_after_failures(uint32_t failures) { this->unavailable_after_failures_ = failures; }

 protected:
  void publish_(const MeterData &data);
  void invalidate_measurements_();
  void request_read_();
  void send_link_request_();
  void log_uart_trace_();
  void record_uart_byte_(uint8_t byte);
  void reset_receive_attempt_();
  void handle_ack_(uint8_t byte);
  void handle_response_(const MeterData &data);
  void handle_timeout_();
  void retry_readout_();
  void finish_failed_readout_(bool awaiting_ack);
  // Readout scheduling lives here; LinkState owns ACK/data phases and FCB retries.
  enum class ReadoutState { STARTING, IDLE, WAITING_FOR_REPLY, ACK_DELAY };
  enum class ReadoutPurpose { MEASUREMENTS, ADDRESS_DISCOVERY };
  bool readout_pending_() const {
    return this->readout_state_ == ReadoutState::WAITING_FOR_REPLY || this->readout_state_ == ReadoutState::ACK_DELAY;
  }
  static constexpr size_t UART_TRACE_CAPACITY = 512;
  FrameReceiver receiver_;
  LinkState link_;
  std::array<uint8_t, UART_TRACE_CAPACITY> uart_trace_{};
  size_t uart_trace_size_{0};
  bool uart_trace_truncated_{false};
  ReadoutState readout_state_{ReadoutState::STARTING};
  ReadoutPurpose readout_purpose_{ReadoutPurpose::MEASUREMENTS};
  FrameResult frame_error_{FrameResult::INCOMPLETE};
  bool measurements_invalidated_{false};
  bool automatic_readout_enabled_{true};
  uint32_t requested_at_{0};
  uint32_t readout_generation_{0};
  uint32_t last_link_activity_at_{0};
  uint32_t consecutive_failures_{0};
  uint32_t unavailable_after_failures_{0};
  sensor::Sensor *energy_{nullptr};
  sensor::Sensor *volume_{nullptr};
  sensor::Sensor *power_{nullptr};
  sensor::Sensor *flow_{nullptr};
  sensor::Sensor *flow_temperature_{nullptr};
  sensor::Sensor *return_temperature_{nullptr};
  sensor::Sensor *temperature_difference_{nullptr};
  text_sensor::TextSensor *error_code_{nullptr};
  sensor::Sensor *battery_operating_duration_{nullptr};
  sensor::Sensor *operating_time_without_error_{nullptr};
  sensor::Sensor *protocol_version_{nullptr};
  sensor::Sensor *readout_failures_{nullptr};
  text_sensor::TextSensor *meter_datetime_{nullptr};
  text_sensor::TextSensor *error_start_{nullptr};
  text_sensor::TextSensor *serial_number_{nullptr};
  text_sensor::TextSensor *manufacturer_{nullptr};
  binary_sensor::BinarySensor *readout_successful_{nullptr};
};

class ReadNowButton : public button::Button {
 public:
  void set_parent(QalcosonicE3 *parent) { this->parent_ = parent; }

 protected:
  void press_action() override { this->parent_->read_now(); }
  QalcosonicE3 *parent_{nullptr};
};

class AutomaticReadoutSwitch : public switch_::Switch, public Component {
 public:
  void set_parent(QalcosonicE3 *parent) { this->parent_ = parent; }
  void setup() override;
  void dump_config() override;

 protected:
  void write_state(bool state) override;
  QalcosonicE3 *parent_{nullptr};
};

class DiscoverAddressButton : public button::Button {
 public:
  void set_parent(QalcosonicE3 *parent) { this->parent_ = parent; }

 protected:
  void press_action() override { this->parent_->discover_address(); }
  QalcosonicE3 *parent_{nullptr};
};
}  // namespace qalcosonic_e3
}  // namespace esphome
