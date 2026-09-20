#include "protocol.h"
#include <cmath>

namespace esphome {
namespace roger_edge1 {

GateStatus GateStatus::decode(uint16_t word) {
  return {uint8_t((word >> 8) & 15), uint8_t((word >> 12) & 15), uint8_t(word & 15),
          uint8_t((word >> 4) & 15)};
}

const char *GateStatus::state_name(uint8_t state) {
  static const char *const NAMES[] = {
      "Sconosciuto", "Apertura", "Stop durante apertura", "Chiusura", "Stop durante chiusura",
      "Aperta", "Chiusa", "Sbloccata", "Posizione sconosciuta", "Apertura (pos. sconosciuta)",
      "Stop apertura (pos. sconosciuta)", "Chiusura (pos. sconosciuta)",
      "Stop chiusura / aperta (pos. sconosciuta)", "Chiusa (pos. sconosciuta)",
      "Sbloccata (pos. sconosciuta)", "Stato non documentato (15)"};
  return NAMES[state & 15];
}

float PositionFilter::apply(uint8_t raw, uint8_t state, uint32_t now) {
  if (!GateStatus::position_known(state) || raw > 15) {
    reset();
    return NAN;
  }
  if (have_value_ && uint32_t(now - last_sample_) >= max_age_)
    reset();
  last_sample_ = now;
  const bool endpoint_jump = have_value_ &&
      ((accepted_ == 15 && raw == 0) || (accepted_ == 0 && raw == 15));
  if (endpoint_jump && !candidate_) {
    candidate_ = true;
    return GateStatus::position(accepted_);
  }
  // The only possible candidate is the opposite endpoint of accepted_. If it
  // recurs it is confirmed; any other next value discards the candidate.
  candidate_ = false;
  have_value_ = true;
  accepted_ = raw;
  return GateStatus::position(accepted_);
}

uint16_t modbus_crc(const uint8_t *data, size_t length) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < length; i++) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; bit++)
      crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : crc >> 1;
  }
  return crc;
}

Frame make_request(uint8_t address, uint8_t function, uint16_t reg, uint16_t value) {
  Frame frame{address, function, uint8_t(reg >> 8), uint8_t(reg), uint8_t(value >> 8), uint8_t(value), 0, 0};
  const auto crc = modbus_crc(frame.data(), 6);
  frame[6] = uint8_t(crc);
  frame[7] = uint8_t(crc >> 8);
  return frame;
}

bool Protocol::enqueue_command(uint16_t command, uint32_t now) {
  if (command == CMD_STOP) {
    // STOP supersedes every movement that has not reached the wire.
    motion_.valid = false;
    stop_ = {REG_COMMAND, command, now, true};
    return true;
  }
  if (!online(now) || (command != CMD_OPEN && command != CMD_CLOSE && command != CMD_PEDESTRIAN))
    return false;
  // At most the latest requested movement is queued. Never build up delayed movements.
  motion_ = {REG_COMMAND, command, now, true};
  return true;
}

bool Protocol::enqueue_parameter(uint8_t parameter, uint8_t value, uint32_t now) {
  if (!online(now) || value > 1 || (parameter != 38 && parameter != 80))
    return false;
  const bool is_80 = parameter == 80;
  parameters_[is_80 ? 1 : 0] = {is_80 ? REG_PARAMETER_80 : REG_PARAMETER_38,
                                uint16_t((is_80 ? 0x5000 : 0x2600) | value), now, true};
  return true;
}

void Protocol::enable_parameter_reads(uint8_t parameter) {
  if (parameter != 38 && parameter != 80)
    return;
  auto &poll = parameter_polls_[parameter == 80 ? 1 : 0];
  poll.enabled = true;
  poll.refresh = true;
}

void Protocol::begin_(uint8_t function, uint16_t reg, uint16_t value, uint32_t now, Frame &frame) {
  pending_ = true;
  function_ = function;
  reg_ = reg;
  value_ = value;
  sent_at_ = last_activity_ = now;
  rx_size_ = 0;
  frame = make_request(address_, function, reg, value);
}

void Protocol::finish_error_(Failure reason, uint8_t exception, uint32_t now) {
  pending_ = false;
  rx_size_ = 0;
  last_activity_ = now;
  errors_++;
  // No automatic replay of writes: a lost ACK does not prove the command was not executed.
  if (listener_ != nullptr)
    listener_->on_protocol_error(reason, reg_, value_, exception);
}

bool Protocol::take_write_(Write &slot, uint32_t now, Frame &frame) {
  if (!slot.valid)
    return false;
  const Write write = slot;
  slot.valid = false;
  if (uint32_t(now - write.queued_at) >= WRITE_MAX_AGE || (write.value != CMD_STOP && !online(now))) {
    errors_++;
    if (listener_ != nullptr)
      listener_->on_protocol_error(Failure::EXPIRED, write.reg, write.value, 0);
    return false;
  }
  begin_(0x06, write.reg, write.value, now, frame);
  // Read back even if the ACK is lost: the write may already have succeeded.
  // This flag schedules a READ, never a repeated write.
  if (write.reg == REG_PARAMETER_38 || write.reg == REG_PARAMETER_80)
    parameter_polls_[write.reg == REG_PARAMETER_80 ? 1 : 0].refresh = true;
  return true;
}

bool Protocol::take_parameter_read_(uint32_t now, Frame &frame) {
  if (!online(now))
    return false;
  for (uint8_t step = 0; step < 2; step++) {
    const uint8_t index = (next_parameter_read_ + step) % 2;
    auto &poll = parameter_polls_[index];
    const bool due = poll.refresh || (poll.enabled &&
        (!poll.have_polled || uint32_t(now - poll.last_poll) >= parameter_update_interval_));
    if (!due)
      continue;
    poll.refresh = false;
    poll.have_polled = true;
    poll.last_poll = now;  // Failed reads back off until the next interval.
    next_parameter_read_ = (index + 1) % 2;
    begin_(0x03, index == 0 ? BLOCK_PARAMETER_38 : BLOCK_PARAMETER_80, PARAMETER_BLOCK_WORDS, now, frame);
    return true;
  }
  return false;
}

void Protocol::expire_inputs_(uint32_t now) {
  if (have_inputs_ && !inputs_online(now)) {
    have_inputs_ = false;
    if (listener_ != nullptr)
      listener_->on_inputs_invalidated();
  }
}

bool Protocol::take_telemetry_(uint32_t now, Frame &frame) {
  const bool state_due = force_poll_ || uint32_t(now - last_poll_) >= update_interval_;
  const bool inputs_due = inputs_enabled_ &&
      (!inputs_polled_ || uint32_t(now - last_inputs_poll_) >= inputs_update_interval_);
  if (!state_due && !inputs_due)
    return false;
  // When both are due, alternate even if a slow/failed request exceeded its interval.
  if (inputs_due && (!state_due || prefer_inputs_)) {
    inputs_polled_ = true;
    last_inputs_poll_ = now;
    prefer_inputs_ = false;
    begin_(0x03, REG_INPUTS, INPUT_BLOCK_WORDS, now, frame);
  } else {
    force_poll_ = false;
    last_poll_ = now;
    prefer_inputs_ = true;
    begin_(0x03, REG_STATE, 1, now, frame);
  }
  poll_after_motion_ = false;
  return true;
}

bool Protocol::next_request(uint32_t now, Frame &frame) {
  expire_inputs_(now);
  if (pending_ && uint32_t(now - sent_at_) >= response_timeout_)
    finish_error_(Failure::TIMEOUT, 0, now);
  if (pending_ || uint32_t(now - last_activity_) < FRAME_GAP)
    return false;
  if (take_write_(stop_, now, frame))
    return true;
  // With inputs enabled, a stream of movement clicks must still leave room for telemetry.
  if (inputs_enabled_ && poll_after_motion_ && take_telemetry_(now, frame))
    return true;
  if (take_write_(motion_, now, frame)) {
    poll_after_motion_ = true;
    return true;
  }
  if (take_telemetry_(now, frame))
    return true;
  for (auto &parameter : parameters_)
    if (take_write_(parameter, now, frame))
      return true;
  return take_parameter_read_(now, frame);
}

void Protocol::discard_prefix_() {
  for (size_t i = 1; i < rx_size_; i++)
    rx_[i - 1] = rx_[i];
  rx_size_--;
}

void Protocol::receive(uint8_t byte, uint32_t now) {
  expire_inputs_(now);
  // Reject a late ACK even if loop() has not yet had an opportunity to run the watchdog.
  if (pending_ && uint32_t(now - sent_at_) >= response_timeout_)
    finish_error_(Failure::TIMEOUT, 0, now);
  if (uint32_t(now - last_activity_) >= PARTIAL_FRAME_TIMEOUT)
    rx_size_ = 0;
  last_activity_ = now;
  if (!pending_)
    return;
  if (rx_size_ == rx_.size())
    discard_prefix_();
  rx_[rx_size_++] = byte;
  while (rx_size_ >= 2) {
    if (rx_[0] != address_ || (rx_[1] != function_ && rx_[1] != (function_ | 0x80))) {
      discard_prefix_();
      continue;
    }
    const bool exception = (rx_[1] & 0x80) != 0;
    if (!exception && function_ == 3 && rx_size_ >= 3 && rx_[2] != 2 * value_) {
      discard_prefix_();
      continue;
    }
    const size_t length = exception ? 5 : (function_ == 3 ? 5 + 2 * value_ : 8);
    if (rx_size_ < length)
      return;
    const auto crc = modbus_crc(rx_.data(), length - 2);
    if (rx_[length - 2] != uint8_t(crc) || rx_[length - 1] != uint8_t(crc >> 8)) {
      crc_errors_++;
      discard_prefix_();
      continue;
    }
    if (exception) {
      finish_error_(Failure::EXCEPTION, rx_[2], now);
      return;
    }
    if (function_ == 6 && (rx_[2] != uint8_t(reg_ >> 8) || rx_[3] != uint8_t(reg_) ||
                           rx_[4] != uint8_t(value_ >> 8) || rx_[5] != uint8_t(value_))) {
      rx_size_ = 0;  // An ACK for a different register/value never completes this request.
      return;
    }
    if (function_ == 3) {
      if (reg_ == REG_STATE) {
        const uint16_t state = (uint16_t(rx_[3]) << 8) | rx_[4];
        const bool was_online = online(now);
        have_state_ = true;
        last_state_ = now;
        if (!was_online)
          for (auto &poll : parameter_polls_)
            poll.refresh = poll.refresh || poll.enabled;
        pending_ = false;
        rx_size_ = 0;
        if (listener_ != nullptr)
          listener_->on_gate_status(state);
      } else if (reg_ == REG_INPUTS) {
        const InputStatus inputs{uint16_t((uint16_t(rx_[3]) << 8) | rx_[4]),
                                 uint16_t((uint16_t(rx_[5]) << 8) | rx_[6])};
        have_inputs_ = true;
        last_inputs_ = now;
        pending_ = false;
        rx_size_ = 0;
        if (listener_ != nullptr)
          listener_->on_input_status(inputs);
      } else if (reg_ == BLOCK_PARAMETER_38 || reg_ == BLOCK_PARAMETER_80) {
        const bool is_80 = reg_ == BLOCK_PARAMETER_80;
        const uint16_t parameter_reg = is_80 ? REG_PARAMETER_80 : REG_PARAMETER_38;
        const size_t offset = 3 + 2 * (parameter_reg - reg_);
        const uint16_t value = (uint16_t(rx_[offset]) << 8) | rx_[offset + 1];
        // Read values are plain 0/1, not the 0x26xx/0x50xx write encoding.
        if (value > 1) {
          finish_error_(Failure::INVALID_VALUE, 0, now);
          return;
        }
        pending_ = false;
        rx_size_ = 0;
        if (listener_ != nullptr)
          listener_->on_parameter_value(is_80 ? 80 : 38, value);
      } else {
        finish_error_(Failure::INVALID_VALUE, 0, now);
      }
    } else {
      pending_ = false;
      rx_size_ = 0;
      force_poll_ = true;
      if (listener_ != nullptr)
        listener_->on_write_ack(reg_, value_);
    }
    return;
  }
}

}  // namespace roger_edge1
}  // namespace esphome
