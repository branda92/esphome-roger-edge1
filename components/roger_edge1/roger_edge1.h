#pragma once

#include "protocol.h"
#include "esphome/core/component.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/cover/cover.h"
#include "esphome/components/button/button.h"
#include "esphome/components/select/select.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"

namespace esphome {
namespace roger_edge1 {

class RogerCover;
class RogerSelect;

class RogerEdge1 : public Component, public uart::UARTDevice, public ProtocolListener {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }
  void set_address(uint8_t address) { protocol_.set_address(address); }
  void set_update_interval(uint32_t ms) { protocol_.set_update_interval(ms); }
  void set_response_timeout(uint32_t ms) { protocol_.set_response_timeout(ms); }
  void set_offline_timeout(uint32_t ms) {
    protocol_.set_offline_timeout(ms);
    for (auto &filter : position_filters_)
      filter.set_max_age(ms);
  }
  void set_parameter_update_interval(uint32_t ms) { protocol_.set_parameter_update_interval(ms); }
  void set_inputs_update_interval(uint32_t ms) { protocol_.set_inputs_update_interval(ms); }
  void set_inputs_timeout(uint32_t ms) { protocol_.set_inputs_timeout(ms); }
  void set_cover(RogerCover *entity) { cover_ = entity; }
  void set_sensor(uint8_t index, sensor::Sensor *entity) {
    sensors_[index] = entity;
    if (index >= 10)
      protocol_.enable_input_reads();
  }
  void set_text_sensor(uint8_t index, text_sensor::TextSensor *entity) { texts_[index] = entity; }
  void set_online_sensor(binary_sensor::BinarySensor *entity) { online_sensor_ = entity; }
  void set_photocell_sensor(uint8_t index, binary_sensor::BinarySensor *entity) {
    photocells_[index] = entity;
    protocol_.enable_input_reads();
  }
  void set_parameter_select(uint8_t parameter, RogerSelect *entity) {
    selects_[parameter == 80 ? 1 : 0] = entity;
    protocol_.enable_parameter_reads(parameter);
  }
  bool command(uint16_t command);
  bool set_parameter(uint8_t parameter, uint8_t value);
  void on_gate_status(uint16_t value) override;
  void on_write_ack(uint16_t reg, uint16_t value) override;
  void on_parameter_value(uint8_t parameter, uint8_t value) override;
  void on_input_status(const InputStatus &inputs) override;
  void on_inputs_invalidated() override;
  void on_protocol_error(Failure reason, uint16_t reg, uint16_t value, uint8_t exception) override;

 protected:
  void publish_sensor_(uint8_t index, float value);
  void publish_text_(uint8_t index, const char *value);
  void publish_online_(bool online);
  Protocol protocol_{};
  std::array<PositionFilter, 2> position_filters_{};
  RogerCover *cover_{nullptr};
  // Gate raw/positions/states, errors, CRC errors, input words 0x1711/0x1712.
  std::array<sensor::Sensor *, 12> sensors_{};
  // Leaf state 1/2, last transaction result
  std::array<text_sensor::TextSensor *, 3> texts_{};
  std::array<RogerSelect *, 2> selects_{};
  binary_sensor::BinarySensor *online_sensor_{nullptr};
  std::array<binary_sensor::BinarySensor *, 2> photocells_{};
  bool online_{false};
};

class RogerCover : public cover::Cover {
 public:
  void set_parent(RogerEdge1 *parent) { parent_ = parent; }
  cover::CoverTraits get_traits() override;
  void update_status(const GateStatus &status, float position_1, float position_2);

 protected:
  void control(const cover::CoverCall &call) override;
  RogerEdge1 *parent_{nullptr};
};

class RogerButton : public button::Button {
 public:
  void set_parent(RogerEdge1 *parent) { parent_ = parent; }
  void set_command(uint16_t command) { command_ = command; }

 protected:
  void press_action() override { parent_->command(command_); }
  RogerEdge1 *parent_{nullptr};
  uint16_t command_{CMD_STOP};
};

class RogerSelect : public select::Select {
 public:
  void set_parent(RogerEdge1 *parent) { parent_ = parent; }
  void set_parameter(uint8_t parameter) { parameter_ = parameter; }

 protected:
  void control(const std::string &value) override;
  RogerEdge1 *parent_{nullptr};
  uint8_t parameter_{80};
};

}  // namespace roger_edge1
}  // namespace esphome
