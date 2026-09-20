#include "protocol.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace esphome::roger_edge1;

struct Listener : ProtocolListener {
  std::vector<uint16_t> states;
  std::vector<std::pair<uint16_t, uint16_t>> writes;
  std::vector<Failure> failures;
  uint8_t last_exception{0};
  void on_gate_status(uint16_t value) override { states.push_back(value); }
  void on_write_ack(uint16_t reg, uint16_t value) override { writes.emplace_back(reg, value); }
  void on_protocol_error(Failure reason, uint16_t, uint16_t, uint8_t exception) override {
    failures.push_back(reason);
    last_exception = exception;
  }
};

struct Rig {
  Protocol p;
  Listener l;
  Frame tx{};
  Rig() { p.set_listener(&l); }
  void feed(std::vector<uint8_t> bytes, uint32_t now) {
    for (auto b : bytes) p.receive(b, now);
  }
  void reply(std::vector<uint8_t> bytes, uint32_t now) {
    auto crc = modbus_crc(bytes.data(), bytes.size());
    bytes.push_back(crc & 255);
    bytes.push_back(crc >> 8);
    feed(bytes, now);
  }
  void ready(uint32_t now = 10) {
    assert(p.next_request(now, tx));
    assert(tx[1] == 3 && tx[2] == 0x15 && tx[3] == 0x80 && tx[5] == 1);
    reply({10, 3, 2, 0xFF, 0x66}, now + 1);
    assert(p.online(now + 1));
  }
  void ack(uint32_t now) { feed(std::vector<uint8_t>(tx.begin(), tx.end()), now); }
};

void crc_and_decode() {
  // Standard Modbus RTU example: address 1, FC03, first 10 holding registers.
  const uint8_t reference[] = {1, 3, 0, 0, 0, 10};
  assert(modbus_crc(reference, sizeof(reference)) == 0xCDC5);
  auto s = GateStatus::decode(0xA321);
  assert(s.position_1_raw == 3 && s.position_2_raw == 10 && s.state_1 == 1 && s.state_2 == 2);
  assert(GateStatus::position(15) == 0 && GateStatus::position(0) == 1);
  assert(std::fabs(GateStatus::position(3) - 0.8f) < 1e-6);
  for (uint32_t word = 0; word <= 0xFFFF; word++) {
    s = GateStatus::decode(word);
    assert(uint32_t((s.position_2_raw << 12) | (s.position_1_raw << 8) | (s.state_2 << 4) | s.state_1) == word);
  }
  for (uint8_t i = 0; i < 16; i++) {
    assert(GateStatus::position_known(i) == (i >= 1 && i <= 6));
    assert(GateStatus::state_name(i)[0] != '\0');
  }
  assert(GateStatus::opening(9) && GateStatus::closing(11));
  assert(!GateStatus::position_known(12));  // Ambiguous state must never imply 100% open.
}

void startup_and_commands() {
  Rig r;
  assert(!r.p.online(0));
  assert(!r.p.enqueue_command(CMD_OPEN, 0));
  assert(!r.p.enqueue_parameter(80, 1, 0));
  r.ready();
  assert(r.l.writes.empty());
  const uint16_t commands[] = {CMD_OPEN, CMD_STOP, CMD_CLOSE, CMD_PEDESTRIAN};
  uint32_t now = 20;
  for (auto command : commands) {
    assert(r.p.enqueue_command(command, now));
    assert(r.p.next_request(now, r.tx));
    assert(r.tx[0] == 10 && r.tx[1] == 6 && r.tx[2] == 0x19 && r.tx[3] == 0x65);
    assert(r.tx[4] == 0x68 && r.tx[5] == (command & 255));
    r.ack(now + 1);
    assert(r.l.writes.back().second == command);
    assert(r.p.next_request(now + 6, r.tx));  // One status refresh after the ACK.
    assert(r.tx[1] == 3);
    r.reply({10, 3, 2, 0xFF, 0x66}, now + 7);
    now += 20;
  }
  assert(!r.p.enqueue_command(0x1234, now));
}

void parameters() {
  for (auto parameter : {38, 80}) {
    for (auto value : {0, 1}) {
      Rig r;
      r.ready();
      assert(r.p.enqueue_parameter(parameter, value, 20));
      assert(r.p.next_request(20, r.tx));
      const auto expected = make_request(10, 6, parameter == 38 ? 0x1603 : 0x1622,
                                        (parameter == 38 ? 0x2600 : 0x5000) | value);
      assert(r.tx == expected);
      assert(r.l.writes.empty());  // No optimistic publication.
      r.ack(21);
      assert(r.l.writes.size() == 1);
    }
  }
  Rig r;
  r.ready();
  assert(!r.p.enqueue_parameter(79, 1, 20));
  assert(!r.p.enqueue_parameter(80, 2, 20));
}

void fragmentation_corruption_and_noise() {
  Rig r;
  assert(r.p.next_request(10, r.tx));
  r.feed({0, 255, 10, 4, 10, 3, 4, 0, 0, 0, 0}, 11); // Noise, wrong function/length.
  r.reply({11, 3, 2, 0, 0x55}, 12);                    // Different slave.
  r.feed({10, 3, 2, 0xFF, 0x66, 0, 0}, 13);           // Invalid CRC.
  assert(r.l.states.empty());
  assert(r.p.crc_errors() >= 1);
  std::vector<uint8_t> good{10, 3, 2, 0xA3, 0x21};
  const auto crc = modbus_crc(good.data(), good.size());
  good.push_back(crc & 255);
  good.push_back(crc >> 8);
  for (size_t i = 0; i < good.size(); i++) {
    r.p.receive(good[i], 20 + i);
    if (i + 1 < good.size()) assert(r.l.states.empty());
  }
  assert(r.l.states.size() == 1 && r.l.states[0] == 0xA321);
  r.feed(good, 30); // Unsolicited/duplicate reply is discarded.
  assert(r.l.states.size() == 1);

  Rig partial;
  assert(partial.p.next_request(10, partial.tx));
  partial.feed({10, 3, 2, 0xFF}, 11);
  partial.reply({10, 3, 2, 0, 0x55}, 60); // Incomplete old frame times out.
  assert(partial.l.states.size() == 1 && partial.l.states[0] == 0x55);
}

void matching_ack_and_exceptions() {
  Rig r;
  r.ready();
  assert(r.p.enqueue_command(CMD_OPEN, 20));
  assert(r.p.next_request(20, r.tx));
  r.reply({10, 6, 0x19, 0x65, 0x68, 4}, 21); // Valid CRC, wrong value.
  r.reply({10, 6, 0x16, 0x22, 0x68, 2}, 22); // Wrong register.
  assert(r.l.writes.empty());
  assert(!r.p.next_request(25, r.tx));
  r.reply({10, 0x86, 2}, 26);
  assert(r.l.writes.empty() && r.l.failures.back() == Failure::EXCEPTION && r.l.last_exception == 2);
  assert(r.p.errors() == 1);
  assert(!r.p.next_request(31, r.tx)); // No retry.

  Rig poll;
  assert(poll.p.next_request(10, poll.tx));
  poll.reply({10, 0x83, 3}, 11);
  assert(!poll.p.online(11) && poll.l.last_exception == 3);
}

void stop_priority_and_coalescing() {
  Rig r;
  r.ready();
  assert(r.p.next_request(510, r.tx)); // A poll is already in flight.
  assert(r.p.enqueue_command(CMD_OPEN, 511));
  assert(r.p.enqueue_command(CMD_CLOSE, 512));
  assert(r.p.enqueue_command(CMD_STOP, 513));
  assert(r.p.enqueue_parameter(80, 1, 514));
  assert(!r.p.next_request(520, r.tx)); // Never overlap wire transactions.
  r.reply({10, 3, 2, 0xFF, 0x66}, 521);
  assert(r.p.next_request(526, r.tx));
  assert(r.tx[5] == 1 && r.tx[3] == 0x65);
  r.ack(527);
  assert(r.p.next_request(532, r.tx) && r.tx[1] == 3);
  r.reply({10, 3, 2, 0xFF, 0x66}, 533);
  assert(r.p.next_request(538, r.tx) && r.tx[3] == 0x22); // No old OPEN/CLOSE after STOP.

  Rig latest;
  latest.ready();
  for (int i = 0; i < 10000; i++) assert(latest.p.enqueue_command(CMD_OPEN, 20));
  assert(latest.p.enqueue_command(CMD_CLOSE, 20));
  assert(latest.p.next_request(20, latest.tx) && latest.tx[5] == 4);
  latest.ack(21);
  assert(latest.p.next_request(26, latest.tx) && latest.tx[1] == 3);
  latest.reply({10, 3, 2, 0xFF, 0x66}, 27);
  assert(!latest.p.next_request(32, latest.tx));
}

void timeouts_staleness_and_rollover() {
  Rig r;
  r.ready();
  assert(r.p.enqueue_command(CMD_OPEN, 20));
  assert(r.p.next_request(20, r.tx));
  auto old = r.tx;
  assert(!r.p.next_request(220, r.tx));
  assert(r.l.failures.back() == Failure::TIMEOUT);
  r.feed(std::vector<uint8_t>(old.begin(), old.end()), 221);
  assert(r.l.writes.empty());
  assert(!r.p.next_request(226, r.tx));
  assert(r.p.next_request(510, r.tx) && r.tx[1] == 3); // Recover by reading, never replay OPEN.
  assert(!r.p.online(3011));
  assert(!r.p.enqueue_command(CMD_CLOSE, 3011));
  assert(r.p.enqueue_command(CMD_STOP, 3011)); // STOP remains available without telemetry.
  assert(!r.p.next_request(3011, r.tx)); // Expire the pending poll and observe bus gap.
  assert(r.p.next_request(3016, r.tx) && r.tx[1] == 6 && r.tx[5] == 1);

  Rig expired;
  expired.ready();
  assert(expired.p.enqueue_command(CMD_OPEN, 20));
  assert(expired.p.next_request(2020, expired.tx) && expired.tx[1] == 3);
  assert(expired.l.failures.back() == Failure::EXPIRED);

  Rig late;
  assert(late.p.next_request(10, late.tx));
  late.reply({10, 3, 2, 0xFF, 0x66}, 210); // Watchdog runs on receive too.
  assert(late.l.states.empty() && late.l.failures.back() == Failure::TIMEOUT);

  Rig wrap;
  const uint32_t before_wrap = 0xFFFFFFF0;
  wrap.ready(before_wrap);
  assert(wrap.p.online(10));
  assert(wrap.p.enqueue_command(CMD_OPEN, 10));
  assert(wrap.p.next_request(10, wrap.tx) && wrap.tx[1] == 6);
  wrap.ack(11);
  assert(wrap.l.writes.size() == 1);
}

int main() {
  crc_and_decode();
  startup_and_commands();
  parameters();
  fragmentation_corruption_and_noise();
  matching_ack_and_exceptions();
  stop_priority_and_coalescing();
  timeouts_staleness_and_rollover();
  std::puts("PASS: CRC, 65536 packed states, commands, parameters, framing, ACK matching, exceptions, STOP, queues, timeout, offline, millis rollover");
}
