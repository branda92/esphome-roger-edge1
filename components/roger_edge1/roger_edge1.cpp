#include "roger_edge1.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include <cmath>
#include <cstdio>

namespace esphome {
namespace roger_edge1 {

static const char *const TAG = "roger_edge1";

void RogerEdge1::setup() {
  protocol_.set_listener(this);
  publish_online_(false);
  on_inputs_invalidated();
  publish_text_(2, "Nessun comando inviato");
}

void RogerEdge1::dump_config() {
  ESP_LOGCONFIG(TAG, "Roger EDGE1 UART/Modbus RTU (115200 8N1)");
  ESP_LOGCONFIG(TAG, "  Stato: 0x1580; comandi: 0x1965; parametri: 38 e 80 (lettura e scrittura)");
  ESP_LOGCONFIG(TAG, "  Ingressi: 0x1711, FT1 bit 4 / FT2 bit 5; 0x1712 solo diagnostica raw");
  ESP_LOGCONFIG(TAG, "  Un solo master UART; nessuna ritrasmissione automatica dei comandi");
  LOG_COVER("  ", "Cancello", cover_);
  LOG_BINARY_SENSOR("  ", "FT1 oscurata", photocells_[0]);
  LOG_BINARY_SENSOR("  ", "FT2 oscurata", photocells_[1]);
}

void RogerEdge1::loop() {
  // Bound work per loop, including during a noisy/disconnected UART.
  size_t budget = 256;
  uint8_t byte;
  while (budget-- > 0 && available() > 0 && read_byte(&byte))
    protocol_.receive(byte, millis());
  const uint32_t now = millis();
  if (online_ && !protocol_.online(now))
    publish_online_(false);
  Frame frame;
  // Drain already buffered input before sending a new request.
  if (available() == 0 && protocol_.next_request(now, frame)) {
    ESP_LOGV(TAG, "TX FC=%02X reg=%02X%02X value=%02X%02X", frame[1], frame[2], frame[3], frame[4], frame[5]);
    write_array(frame.data(), frame.size());
  }
  publish_sensor_(8, protocol_.errors());
  publish_sensor_(9, protocol_.crc_errors());
}

void RogerEdge1::publish_sensor_(uint8_t index, float value) {
  auto *entity = sensors_[index];
  if (entity != nullptr && (!entity->has_state() ||
      !(entity->state == value || (std::isnan(entity->state) && std::isnan(value)))))
    entity->publish_state(value);
}

void RogerEdge1::publish_text_(uint8_t index, const char *value) {
  auto *entity = texts_[index];
  if (entity != nullptr && (!entity->has_state() || entity->state != value))
    entity->publish_state(value);
}

void RogerEdge1::publish_online_(bool online) {
  online_ = online;
  if (online_sensor_ != nullptr)
    online_sensor_->publish_state(online);
  if (online) {
    status_clear_warning();
  } else {
    status_set_warning();
    for (auto &filter : position_filters_)
      filter.reset();
    for (uint8_t i = 0; i < 8; i++)
      publish_sensor_(i, NAN);
    publish_text_(0, "Non disponibile");
    publish_text_(1, "Non disponibile");
    // Native ESPHome covers/selects cannot publish a per-entity unavailable state.
    // Retain their last observed state; the online entity and numeric sensors expose staleness.
  }
}

bool RogerEdge1::command(uint16_t command) {
  const bool accepted = protocol_.enqueue_command(command, millis());
  char result[96];
  std::snprintf(result, sizeof(result), "Comando 0x%04X: %s", command,
                accepted ? "in coda" : "rifiutato (stato UART non recente o comando non valido)");
  publish_text_(2, result);
  if (!accepted)
    ESP_LOGW(TAG, "%s", result);
  return accepted;
}

bool RogerEdge1::set_parameter(uint8_t parameter, uint8_t value) {
  const bool accepted = protocol_.enqueue_parameter(parameter, value, millis());
  char result[96];
  std::snprintf(result, sizeof(result), "Parametro %u = %u: %s", parameter, value,
                accepted ? "in coda" : "rifiutato (stato UART non recente o valore non valido)");
  publish_text_(2, result);
  if (!accepted)
    ESP_LOGW(TAG, "%s", result);
  return accepted;
}

void RogerEdge1::on_gate_status(uint16_t value) {
  if (!online_)
    publish_online_(true);
  const auto status = GateStatus::decode(value);
  publish_sensor_(0, value);
  publish_sensor_(1, status.position_1_raw);
  publish_sensor_(2, status.position_2_raw);
  const uint32_t now = millis();
  const float p1 = position_filters_[0].apply(status.position_1_raw, status.state_1, now);
  const float p2 = position_filters_[1].apply(status.position_2_raw, status.state_2, now);
  publish_sensor_(3, p1 * 100);
  publish_sensor_(4, p2 * 100);
  publish_sensor_(5, (p1 + p2) * 50);
  publish_sensor_(6, status.state_1);
  publish_sensor_(7, status.state_2);
  publish_text_(0, GateStatus::state_name(status.state_1));
  publish_text_(1, GateStatus::state_name(status.state_2));
  if (cover_ != nullptr)
    cover_->update_status(status, p1, p2);
}

void RogerEdge1::on_write_ack(uint16_t reg, uint16_t value) {
  char result[80];
  std::snprintf(result, sizeof(result), "ACK registro 0x%04X valore 0x%04X", reg, value);
  ESP_LOGD(TAG, "%s", result);
  publish_text_(2, result);
}

void RogerEdge1::on_parameter_value(uint8_t parameter, uint8_t value) {
  auto *entity = selects_[parameter == 80 ? 1 : 0];
  if (entity != nullptr)
    entity->publish_state(value == 1 ? "1" : "0");
  ESP_LOGD(TAG, "Parametro %u letto dalla centrale: %u", parameter, value);
}

void RogerEdge1::on_input_status(const InputStatus &inputs) {
  publish_sensor_(10, inputs.raw);
  publish_sensor_(11, inputs.auxiliary_raw);
  if (photocells_[0] != nullptr)
    photocells_[0]->publish_state(inputs.ft1_obstructed());
  if (photocells_[1] != nullptr)
    photocells_[1]->publish_state(inputs.ft2_obstructed());
}

void RogerEdge1::on_inputs_invalidated() {
  for (auto *entity : photocells_)
    if (entity != nullptr)
      entity->invalidate_state();
  publish_sensor_(10, NAN);
  publish_sensor_(11, NAN);
}

void RogerEdge1::on_protocol_error(Failure reason, uint16_t reg, uint16_t value, uint8_t exception) {
  char result[128];
  if (reason == Failure::EXCEPTION)
    std::snprintf(result, sizeof(result), "Eccezione Modbus 0x%02X, registro 0x%04X", exception, reg);
  else
    std::snprintf(result, sizeof(result), "%s, registro 0x%04X valore 0x%04X",
                  reason == Failure::TIMEOUT ? "Timeout risposta"
                  : reason == Failure::INVALID_VALUE ? "Valore parametro letto non supportato"
                  : "Comando scaduto o connessione persa", reg, value);
  ESP_LOGW(TAG, "%s", result);
  // A background poll must not overwrite the result of the most recent write.
  if (reg == REG_COMMAND || reg == REG_PARAMETER_38 || reg == REG_PARAMETER_80)
    publish_text_(2, result);
}

cover::CoverTraits RogerCover::get_traits() {
  cover::CoverTraits traits;
  traits.set_supports_stop(true);
  // The discovered protocol has no target-percentage command. Report percentages
  // with the position sensors, without advertising an unsupported slider in HA.
  traits.set_supports_position(false);
  traits.set_is_assumed_state(true);  // Keep both direction buttons available; telemetry may become stale.
  return traits;
}

void RogerCover::control(const cover::CoverCall &call) {
  if (call.get_stop())
    parent_->command(CMD_STOP);
  else if (call.get_position().has_value()) {
    if (*call.get_position() == cover::COVER_OPEN)
      parent_->command(CMD_OPEN);
    else if (*call.get_position() == cover::COVER_CLOSED)
      parent_->command(CMD_CLOSE);
  }
}

void RogerCover::update_status(const GateStatus &status, float position_1, float position_2) {
  // Keep the last observed position when unknown, but update movement immediately.
  // Without any known position yet, do not publish a fictitious initial position.
  const float measured = (position_1 + position_2) / 2;
  if (std::isnan(measured) && !has_state())
    return;
  const float pos = std::isnan(measured) ? position : measured;
  const bool opening = GateStatus::opening(status.state_1) || GateStatus::opening(status.state_2);
  const bool closing = GateStatus::closing(status.state_1) || GateStatus::closing(status.state_2);
  const auto operation = opening == closing ? cover::COVER_OPERATION_IDLE
                        : opening ? cover::COVER_OPERATION_OPENING : cover::COVER_OPERATION_CLOSING;
  if (!has_state() || position != pos || current_operation != operation) {
    position = pos;
    current_operation = operation;
    publish_state(false);  // Telemetry is never restored or written to flash.
  }
}

void RogerSelect::control(const std::string &value) {
  if (value == "0" || value == "1")
    parent_->set_parameter(parameter_, value == "1" ? 1 : 0);
  // Only on_parameter_value publishes the option; an ACK alone is not readback.
}

}  // namespace roger_edge1
}  // namespace esphome
