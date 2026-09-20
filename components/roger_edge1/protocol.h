#pragma once

// No ESPHome dependencies: the exact engine used on the ESP32 is also tested on a host.
#include <array>
#include <cstddef>
#include <cstdint>

namespace esphome {
namespace roger_edge1 {

constexpr uint16_t REG_STATE = 0x1580;
constexpr uint16_t REG_INPUTS = 0x1711;
constexpr uint16_t INPUT_BLOCK_WORDS = 2;
constexpr uint16_t REG_COMMAND = 0x1965;
constexpr uint16_t REG_PARAMETER_38 = 0x1603;
constexpr uint16_t REG_PARAMETER_80 = 0x1622;
constexpr uint16_t BLOCK_PARAMETER_38 = 0x15FE;
constexpr uint16_t BLOCK_PARAMETER_80 = 0x161C;
constexpr uint16_t PARAMETER_BLOCK_WORDS = 10;
constexpr uint16_t CMD_STOP = 0x6801;
constexpr uint16_t CMD_OPEN = 0x6802;
constexpr uint16_t CMD_CLOSE = 0x6804;
constexpr uint16_t CMD_PEDESTRIAN = 0x6810;

struct GateStatus {
  uint8_t position_1_raw;
  uint8_t position_2_raw;
  uint8_t state_1;
  uint8_t state_2;
  static GateStatus decode(uint16_t word);
  static bool position_known(uint8_t state) { return state >= 1 && state <= 6; }
  static float position(uint8_t raw) { return (15.0f - raw) / 15.0f; }
  static bool opening(uint8_t state) { return state == 1 || state == 9; }
  static bool closing(uint8_t state) { return state == 3 || state == 11; }
  static const char *state_name(uint8_t state);
};

struct InputStatus {
  uint16_t raw;
  uint16_t auxiliary_raw;  // 0x1712: captured for diagnostics, meaning not established.
  bool ft1_obstructed() const { return (raw & 0x0010) != 0; }
  bool ft2_obstructed() const { return (raw & 0x0020) != 0; }
};

// Confirm a full-scale endpoint jump with a second consecutive sample. Raw
// telemetry and movement codes are deliberately not altered by this filter.
class PositionFilter {
 public:
  void set_max_age(uint32_t ms) { max_age_ = ms; }
  void reset() { have_value_ = candidate_ = false; }
  float apply(uint8_t raw, uint8_t state, uint32_t now);

 protected:
  uint32_t max_age_{3000};
  uint32_t last_sample_{0};
  uint8_t accepted_{0};
  bool have_value_{false};
  bool candidate_{false};
};

using Frame = std::array<uint8_t, 8>;
uint16_t modbus_crc(const uint8_t *data, size_t length);
Frame make_request(uint8_t address, uint8_t function, uint16_t reg, uint16_t value);

enum class Failure : uint8_t { TIMEOUT, EXCEPTION, EXPIRED, INVALID_VALUE };

class ProtocolListener {
 public:
  virtual ~ProtocolListener() = default;
  virtual void on_gate_status(uint16_t value) = 0;
  virtual void on_write_ack(uint16_t reg, uint16_t value) = 0;
  virtual void on_parameter_value(uint8_t, uint8_t) {}
  virtual void on_input_status(const InputStatus &) {}
  virtual void on_inputs_invalidated() {}
  virtual void on_protocol_error(Failure reason, uint16_t reg, uint16_t value, uint8_t exception) = 0;
};

class Protocol {
 public:
  void set_listener(ProtocolListener *listener) { listener_ = listener; }
  void set_address(uint8_t address) { address_ = address; }
  void set_update_interval(uint32_t ms) { update_interval_ = ms; }
  void set_response_timeout(uint32_t ms) { response_timeout_ = ms; }
  void set_offline_timeout(uint32_t ms) { offline_timeout_ = ms; }
  void set_parameter_update_interval(uint32_t ms) { parameter_update_interval_ = ms; }
  void set_inputs_update_interval(uint32_t ms) { inputs_update_interval_ = ms; }
  void set_inputs_timeout(uint32_t ms) { inputs_timeout_ = ms; }
  void enable_input_reads() { inputs_enabled_ = true; }
  void enable_parameter_reads(uint8_t parameter);
  bool enqueue_command(uint16_t command, uint32_t now);
  bool enqueue_parameter(uint8_t parameter, uint8_t value, uint32_t now);
  // The caller must transmit the returned frame immediately; no blocking delay/retry here.
  bool next_request(uint32_t now, Frame &frame);
  void receive(uint8_t byte, uint32_t now);
  bool online(uint32_t now) const { return have_state_ && uint32_t(now - last_state_) < offline_timeout_; }
  bool inputs_online(uint32_t now) const { return have_inputs_ && uint32_t(now - last_inputs_) < inputs_timeout_; }
  uint32_t errors() const { return errors_; }
  uint32_t crc_errors() const { return crc_errors_; }

 protected:
  struct Write {
    uint16_t reg{0};
    uint16_t value{0};
    uint32_t queued_at{0};
    bool valid{false};
  };
  struct ParameterPoll {
    uint32_t last_poll{0};
    bool enabled{false};
    bool refresh{false};
    bool have_polled{false};
  };
  void finish_error_(Failure reason, uint8_t exception, uint32_t now);
  void discard_prefix_();
  bool take_write_(Write &slot, uint32_t now, Frame &frame);
  bool take_parameter_read_(uint32_t now, Frame &frame);
  bool take_telemetry_(uint32_t now, Frame &frame);
  void expire_inputs_(uint32_t now);
  void begin_(uint8_t function, uint16_t reg, uint16_t value, uint32_t now, Frame &frame);

  ProtocolListener *listener_{nullptr};
  uint8_t address_{0x0A};
  uint32_t update_interval_{500};
  uint32_t response_timeout_{200};
  uint32_t offline_timeout_{3000};
  uint32_t parameter_update_interval_{30000};
  uint32_t inputs_update_interval_{500};
  uint32_t inputs_timeout_{3000};
  static constexpr uint32_t WRITE_MAX_AGE = 2000;
  static constexpr uint32_t FRAME_GAP = 5;  // > 3.5 UART characters at 115200 baud
  static constexpr uint32_t PARTIAL_FRAME_TIMEOUT = 30;
  Write stop_{};
  Write motion_{};
  std::array<Write, 2> parameters_{};
  std::array<ParameterPoll, 2> parameter_polls_{};
  uint8_t next_parameter_read_{0};
  bool pending_{false};
  bool force_poll_{true};
  bool have_state_{false};
  bool inputs_enabled_{false};
  bool inputs_polled_{false};
  bool have_inputs_{false};
  bool prefer_inputs_{false};
  bool poll_after_motion_{false};
  uint8_t function_{0};
  uint16_t reg_{0};
  uint16_t value_{0};
  uint32_t sent_at_{0};
  uint32_t last_poll_{0};
  uint32_t last_state_{0};
  uint32_t last_inputs_poll_{0};
  uint32_t last_inputs_{0};
  uint32_t last_activity_{0};
  uint32_t errors_{0};
  uint32_t crc_errors_{0};
  std::array<uint8_t, 5 + 2 * PARAMETER_BLOCK_WORDS> rx_{};
  size_t rx_size_{0};
};

}  // namespace roger_edge1
}  // namespace esphome
