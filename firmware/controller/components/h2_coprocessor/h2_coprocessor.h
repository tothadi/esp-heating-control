#pragma once

// ESP Heating Control - ESP32-S3 side of the link to the ESP32-H2 Zigbee co-processor
// (firmware/zigbee-coordinator). Line protocol: docs/coprocessor-protocol.md.

#include <string>
#include <vector>

#include "esphome/components/button/button.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/uart/uart.h"
#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"

namespace esphome::h2_coprocessor {

class H2Coprocessor;

// One value of one co-processor device, matched by device id and ZCL cluster/attribute. Publishes NaN when no
// reading arrived within the timeout.
class CoprocessorSensor : public sensor::Sensor {
 public:
  void set_device_id(const std::string &id) { this->device_id_ = id; }
  void add_match(uint16_t cluster, uint16_t attr) { this->matches_.push_back({cluster, attr}); }
  void set_timeout(uint32_t timeout_ms) { this->timeout_ms_ = timeout_ms; }

  bool matches(const std::string &id, uint16_t cluster, uint16_t attr) const;
  void on_reading(float value, uint32_t age_s);
  void check_timeout(uint32_t now);
  const std::string &get_device_id() const { return this->device_id_; }

 protected:
  struct Match {
    uint16_t cluster;
    uint16_t attr;
  };
  std::string device_id_;
  std::vector<Match> matches_;
  uint32_t timeout_ms_{3600000};
  uint32_t last_ms_{0};
  bool has_value_{false};
};

enum ButtonAction : uint8_t {
  BUTTON_PERMIT_JOIN,
  BUTTON_STOP_JOIN,
  BUTTON_RESET,
  BUTTON_REFRESH,
};

class CoprocessorButton : public button::Button, public Parented<H2Coprocessor> {
 public:
  void set_action(ButtonAction action) { this->action_ = action; }

 protected:
  void press_action() override;
  ButtonAction action_{BUTTON_REFRESH};
};

class H2Coprocessor : public Component, public uart::UARTDevice {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  void set_reset_pin(InternalGPIOPin *pin) { this->reset_pin_ = pin; }
  void set_permit_join_duration(uint8_t seconds) { this->permit_join_s_ = seconds; }
  void register_sensor(CoprocessorSensor *sensor) { this->sensors_.push_back(sensor); }
  void set_device_list_text_sensor(text_sensor::TextSensor *s) { this->device_list_ = s; }
  void set_firmware_text_sensor(text_sensor::TextSensor *s) { this->firmware_ = s; }
  void set_network_text_sensor(text_sensor::TextSensor *s) { this->network_ = s; }

  // Actions
  void permit_join(uint8_t seconds);
  void reset_coprocessor();
  void refresh();
  void send_command(const std::string &body);

 protected:
  void handle_line_(const std::string &line);
  void handle_message_(const std::vector<std::string> &f);
  void handle_reading_(const std::vector<std::string> &f);
  void publish_network_();

  InternalGPIOPin *reset_pin_{nullptr};
  uint8_t permit_join_s_{180};
  std::vector<CoprocessorSensor *> sensors_;
  text_sensor::TextSensor *device_list_{nullptr};
  text_sensor::TextSensor *firmware_{nullptr};
  text_sensor::TextSensor *network_{nullptr};

  std::string rx_line_;
  bool rx_overflow_{false};
  std::string pending_list_;
  int pending_count_{0};
  uint32_t last_rx_ms_{0};
  bool online_{false};
  std::string fw_;
  std::string net_state_;
  int net_channel_{0};
  std::string net_pan_;
  int net_permit_s_{0};
  uint32_t net_permit_at_{0};
  std::vector<std::string> unmapped_logged_;
};

}  // namespace esphome::h2_coprocessor
