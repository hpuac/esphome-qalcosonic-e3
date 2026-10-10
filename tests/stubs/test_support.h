#pragma once
// Minimal ESPHome test doubles; tests compile and execute the real component.
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace esphome {
namespace test {
inline uint32_t now = 0;
inline std::vector<std::string> logs;
template <typename... Args>
void log(const char *, const char *message, Args...) {
  logs.emplace_back(message);
}
template <typename... Args>
void ignore(Args...) {}
}  // namespace test
inline uint32_t millis() { return test::now; }
namespace setup_priority {
constexpr float LATE = -100;
}
class Component {
 public:
  virtual ~Component() = default;
  virtual void setup() {}
  virtual void loop() {}
  virtual void dump_config() {}
  virtual float get_setup_priority() const { return 0; }
  void set_timeout(const char *, uint32_t, std::function<void()> callback) { timeout = callback; }
  void run_timeout() {
    auto callback = timeout;
    timeout = {};
    callback();
  }

 private:
  std::function<void()> timeout;
};
class PollingComponent : public Component {
 public:
  virtual void update() {}
  void start_poller() {}
  void stop_poller() {}
};
namespace uart {
class UARTDevice {
 public:
  std::deque<uint8_t> rx;
  std::vector<std::vector<uint8_t>> tx;
  bool available() const { return !rx.empty(); }
  bool read_byte(uint8_t *value) {
    if (rx.empty()) return false;
    *value = rx.front();
    rx.pop_front();
    return true;
  }
  void write_array(const uint8_t *data, size_t size) { tx.emplace_back(data, data + size); }
};
}  // namespace uart
namespace sensor {
class Sensor {
 public:
  std::vector<float> states;
  void publish_state(float value) { states.push_back(value); }
};
}  // namespace sensor
namespace binary_sensor {
class BinarySensor {
 public:
  std::vector<bool> states;
  void publish_state(bool value) { states.push_back(value); }
};
}  // namespace binary_sensor
namespace text_sensor {
class TextSensor {
 public:
  std::vector<std::string> states;
  void publish_state(const std::string &value) { states.push_back(value); }
};
}  // namespace text_sensor
namespace button {
class Button {
 public:
  virtual ~Button() = default;

 protected:
  virtual void press_action() = 0;
};
}  // namespace button
namespace switch_ {
class Switch {
 public:
  virtual ~Switch() = default;
  std::optional<bool> get_initial_state_with_restore_mode() { return true; }
  void publish_state(bool) {}

 protected:
  virtual void write_state(bool) = 0;
};
}  // namespace switch_
}  // namespace esphome
#define ESPHOME_LOG_LEVEL 5
#define ESPHOME_LOG_LEVEL_VERBOSE 5
#define ESP_LOGD(...) esphome::test::log(__VA_ARGS__)
#define ESP_LOGI(...) esphome::test::log(__VA_ARGS__)
#define ESP_LOGW(...) esphome::test::log(__VA_ARGS__)
#define ESP_LOGV(...) esphome::test::log(__VA_ARGS__)
#define ESP_LOGCONFIG(...) esphome::test::log(__VA_ARGS__)
#define LOG_UPDATE_INTERVAL(...) esphome::test::ignore(__VA_ARGS__)
#define LOG_SENSOR(...) esphome::test::ignore(__VA_ARGS__)
#define LOG_TEXT_SENSOR(...) esphome::test::ignore(__VA_ARGS__)
#define LOG_BINARY_SENSOR(...) esphome::test::ignore(__VA_ARGS__)
#define LOG_SWITCH(...) esphome::test::ignore(__VA_ARGS__)
