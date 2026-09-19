#pragma once
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/button/button.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/uart/uart.h"
#include "esphome/core/component.h"
#include "parser.h"

namespace esphome {
namespace qalcosonic_e3 {
class QalcosonicE3 : public PollingComponent, public uart::UARTDevice {
 public:
  void setup() override;
  void loop() override;
  void update() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::LATE; }
  void set_energy(sensor::Sensor *entity) { this->energy_ = entity; }
  void set_volume(sensor::Sensor *entity) { this->volume_ = entity; }
  void set_power(sensor::Sensor *entity) { this->power_ = entity; }
  void set_flow(sensor::Sensor *entity) { this->flow_ = entity; }
  void set_flow_temperature(sensor::Sensor *entity) { this->flow_temperature_ = entity; }
  void set_return_temperature(sensor::Sensor *entity) { this->return_temperature_ = entity; }
  void set_temperature_difference(sensor::Sensor *entity) { this->temperature_difference_ = entity; }
  void set_error_code(sensor::Sensor *entity) { this->error_code_ = entity; }
  void set_battery_operating_duration(sensor::Sensor *entity) { this->battery_operating_duration_ = entity; }
  void set_operating_time_without_error(sensor::Sensor *entity) { this->operating_time_without_error_ = entity; }
  void set_protocol_version(sensor::Sensor *entity) { this->protocol_version_ = entity; }
  void set_readout_failures(sensor::Sensor *entity) { this->readout_failures_ = entity; }
  void set_meter_datetime(text_sensor::TextSensor *entity) { this->meter_datetime_ = entity; }
  void set_error_start(text_sensor::TextSensor *entity) { this->error_start_ = entity; }
  void set_serial_number(text_sensor::TextSensor *entity) { this->serial_number_ = entity; }
  void set_manufacturer(text_sensor::TextSensor *entity) { this->manufacturer_ = entity; }
  void set_readout_successful(binary_sensor::BinarySensor *entity) { this->readout_successful_ = entity; }

 protected:
  void publish_(const MeterData &data);
  FrameReceiver receiver_;
  bool ready_{false};
  bool pending_{false};
  bool warned_{false};
  uint32_t requested_at_{0};
  uint32_t failures_{0};
  sensor::Sensor *energy_{nullptr};
  sensor::Sensor *volume_{nullptr};
  sensor::Sensor *power_{nullptr};
  sensor::Sensor *flow_{nullptr};
  sensor::Sensor *flow_temperature_{nullptr};
  sensor::Sensor *return_temperature_{nullptr};
  sensor::Sensor *temperature_difference_{nullptr};
  sensor::Sensor *error_code_{nullptr};
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
  void press_action() override { this->parent_->update(); }
  QalcosonicE3 *parent_{nullptr};
};
}  // namespace qalcosonic_e3
}  // namespace esphome
