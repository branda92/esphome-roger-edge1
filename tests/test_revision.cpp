#include "protocol.h"
#include "captured_fixtures.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <utility>
#include <vector>

using namespace esphome::roger_edge1;

struct Listener : ProtocolListener {
  std::vector<uint16_t> states;
  std::vector<std::pair<uint8_t, uint8_t>> parameters;
  std::vector<std::pair<uint16_t, uint16_t>> writes;
  std::vector<Failure> failures;
  void on_gate_status(uint16_t value) override { states.push_back(value); }
  void on_parameter_value(uint8_t parameter, uint8_t value) override { parameters.emplace_back(parameter, value); }
  void on_write_ack(uint16_t reg, uint16_t value) override { writes.emplace_back(reg, value); }
  void on_protocol_error(Failure reason, uint16_t, uint16_t, uint8_t) override { failures.push_back(reason); }
};

struct Rig {
  Protocol p;
  Listener l;
  Frame tx{};
  Rig() { p.set_listener(&l); }
  template<typename Bytes> void feed(const Bytes &bytes, uint32_t now) {
    for (uint8_t b : bytes) p.receive(b, now);
  }
  void reply(std::vector<uint8_t> bytes, uint32_t now) {
    const uint16_t crc = modbus_crc(bytes.data(), bytes.size());
    bytes.push_back(crc & 255);
    bytes.push_back(crc >> 8);
    feed(bytes, now);
  }
  void ready(uint32_t now = 10) {
    assert(p.next_request(now, tx));
    assert(tx == make_request(10, 3, REG_STATE, 1));
    reply({10, 3, 2, 0xFF, 0x66}, now + 1);
  }
  void request(uint32_t now, uint16_t reg, uint16_t count = 10) {
    assert(p.next_request(now, tx));
    assert(tx == make_request(10, 3, reg, count));
  }
};

const CapturedRead &reading(uint8_t parameter, uint8_t value) {
  for (const auto &r : CAPTURED_READS)
    if (r.parameter == parameter && r.value == value) return r;
  assert(false);
  return CAPTURED_READS[0];
}

void position_filter_and_captured_states() {
  PositionFilter filters[2];
  uint8_t file = 0;
  size_t changed = 0, samples = 0, pedestrian = 0;
  for (const auto &sample : CAPTURED_STATES) {
    if (file != sample.file) {
      file = sample.file;
      for (auto &filter : filters) filter.reset();
    }
    const auto state = GateStatus::decode(sample.word);
    const uint8_t raw[] = {state.position_1_raw, state.position_2_raw};
    const uint8_t code[] = {state.state_1, state.state_2};
    float positions[2];
    for (size_t i = 0; i < 2; i++) {
      positions[i] = filters[i].apply(raw[i], code[i], sample.ms);
      if (!GateStatus::position_known(code[i])) {
        assert(std::isnan(positions[i]));
      } else if (positions[i] != GateStatus::position(raw[i])) {
        changed++;
        assert((sample.file == 3 && sample.ms == 39471 && i == 0 && sample.word == 0xF063) ||
               (sample.file == 6 && sample.ms == 3354 && i == 1 && sample.word == 0x0F11) ||
               (sample.file == 8 && sample.ms == 3253 && i == 1 && sample.word == 0x0F11));
        assert(positions[i] == 0); // Hold closed only for these three anomalous samples.
      }
    }
    if (sample.word == 0xF965) {
      pedestrian++;
      assert(std::fabs(positions[0] - 0.4f) < 1e-6f && positions[1] == 0);
    }
    samples++;
  }
  assert(samples == 977 && changed == 3 && pedestrian > 0);

  PositionFilter f;
  assert(f.apply(15, 6, 0) == 0);
  assert(f.apply(0, 1, 500) == 0); // First full-scale jump held.
  assert(f.apply(0, 5, 1000) == 1); // Second consecutive sample confirms it.
  assert(f.apply(15, 3, 1500) == 1);
  assert(f.apply(14, 3, 2000) == GateStatus::position(14)); // Ordinary movement clears candidate.
  assert(f.apply(15, 6, 2500) == 0);
  assert(f.apply(0, 1, 3000) == 0);
  assert(f.apply(15, 1, 3500) == 0); // Return to accepted endpoint clears candidate too.
  assert(f.apply(0, 1, 4000) == 0);
  assert(std::isnan(f.apply(0, 9, 4500))); // Unknown position resets history.
  assert(f.apply(0, 5, 5000) == 1);
  assert(f.apply(15, 6, 8000) == 0); // Stale history is not used as an anchor.
  f.reset();
  assert(f.apply(0, 5, 8001) == 1);
  f.set_max_age(100);
  assert(f.apply(15, 6, 8101) == 0);
  f.reset();
  f.set_max_age(3000);
  assert(f.apply(15, 6, 0xFFFFFFF0) == 0);
  assert(f.apply(0, 1, 10) == 0);
  assert(f.apply(0, 1, 510) == 1); // millis rollover preserves recent history.
}

void captured_writes_and_parameter_readback() {
  size_t writes = 0;
  for (const auto &fixture : CAPTURED_WRITES) {
    Rig r;
    r.ready();
    const uint8_t parameter = fixture.reg == REG_PARAMETER_38 ? 38 : 80;
    if (fixture.reg == REG_COMMAND)
      assert(r.p.enqueue_command(fixture.value, 20));
    else
      assert(r.p.enqueue_parameter(parameter, fixture.value & 1, 20));
    assert(r.p.next_request(20, r.tx) && r.tx == fixture.request);
    r.feed(fixture.reply, 21);
    assert(r.l.writes.size() == 1 && r.l.writes[0].second == fixture.value);
    assert(r.l.parameters.empty()); // An ACK never publishes a parameter value.
    if (fixture.reg != REG_COMMAND) {
      r.request(26, REG_STATE, 1);
      r.reply({10, 3, 2, 0xFF, 0x66}, 27);
      r.request(32, parameter == 38 ? BLOCK_PARAMETER_38 : BLOCK_PARAMETER_80);
      const auto &read = reading(parameter, fixture.value & 1);
      assert(r.tx == read.request);
      r.feed(read.reply, 33);
      assert(r.l.parameters.size() == 1 && r.l.parameters[0] == std::make_pair(parameter, uint8_t(fixture.value & 1)));
    }
    writes++;
  }
  assert(writes == 26);
  for (const auto &fixture : CAPTURED_READS) {
    Rig r;
    r.p.enable_parameter_reads(fixture.parameter);
    r.ready();
    assert(r.l.parameters.empty());
    assert(r.p.next_request(20, r.tx) && r.tx == fixture.request);
    // Full captured 25-byte packet, split over several loop iterations.
    for (size_t i = 0; i < fixture.reply.size(); i++) {
      r.p.receive(fixture.reply[i], 21 + i);
      if (i + 1 < fixture.reply.size()) assert(r.l.parameters.empty());
    }
    assert(r.l.parameters.size() == 1 && r.l.parameters[0] == std::make_pair(fixture.parameter, fixture.value));
    r.feed(fixture.reply, 46); // Ignore unsolicited duplicate.
    assert(r.l.parameters.size() == 1 && r.l.states.size() == 1);
    assert(!r.p.next_request(51, r.tx)); // No read of the unconfigured select.
  }
  // Even an ACK for 1 followed by an actual read of 0 must publish 0.
  Rig mismatch;
  mismatch.ready();
  assert(mismatch.p.enqueue_parameter(80, 1, 20));
  assert(mismatch.p.next_request(20, mismatch.tx));
  mismatch.feed(mismatch.tx, 21);
  assert(mismatch.l.parameters.empty());
  mismatch.request(26, REG_STATE, 1);
  mismatch.reply({10, 3, 2, 0xFF, 0x66}, 27);
  mismatch.request(32, BLOCK_PARAMETER_80);
  mismatch.feed(reading(80, 0).reply, 33);
  assert(mismatch.l.parameters.back().second == 0);
}

void startup_periodic_and_reconnection() {
  Rig r;
  r.p.enable_parameter_reads(38);
  r.p.enable_parameter_reads(80);
  r.p.set_parameter_update_interval(1000);
  for (uint32_t now = 0; now <= 1200; now++) {
    if (!r.p.next_request(now, r.tx)) continue;
    assert(r.tx[1] == 3); // Startup and periodic synchronization never write anything.
    const uint16_t reg = (r.tx[2] << 8) | r.tx[3];
    if (reg == REG_STATE) r.reply({10, 3, 2, 0xFF, 0x66}, now);
    else r.feed(reading(reg == BLOCK_PARAMETER_38 ? 38 : 80, now < 1000 ? 1 : 0).reply, now);
  }
  assert(r.l.states.size() == 3 && r.l.parameters.size() == 4);
  assert(r.l.parameters[0] == std::make_pair(uint8_t(38), uint8_t(1)));
  assert(r.l.parameters[1] == std::make_pair(uint8_t(80), uint8_t(1)));
  assert(r.l.parameters[2] == std::make_pair(uint8_t(38), uint8_t(0)));
  assert(r.l.parameters[3] == std::make_pair(uint8_t(80), uint8_t(0)));

  Rig recovered;
  recovered.p.enable_parameter_reads(80);
  recovered.ready();
  recovered.request(20, BLOCK_PARAMETER_80);
  recovered.feed(reading(80, 1).reply, 21);
  assert(!recovered.p.online(3011));
  recovered.request(3011, REG_STATE, 1);
  recovered.reply({10, 3, 2, 0xFF, 0x66}, 3012);
  recovered.request(3017, BLOCK_PARAMETER_80); // Refresh immediately, not after 30s.
  recovered.feed(reading(80, 0).reply, 3018);
  assert(recovered.l.parameters.back().second == 0);

  Rig wrap;
  wrap.p.enable_parameter_reads(80);
  wrap.p.set_parameter_update_interval(1000);
  const uint32_t before = 0xFFFFFE00;
  wrap.ready(before);
  wrap.request(before + 10, BLOCK_PARAMETER_80);
  wrap.feed(reading(80, 0).reply, before + 11);
  wrap.request(500, REG_STATE, 1);
  wrap.reply({10, 3, 2, 0xFF, 0x66}, 501);
  wrap.request(506, BLOCK_PARAMETER_80);
  wrap.feed(reading(80, 1).reply, 507);
  assert(wrap.l.parameters.size() == 2);
}

void read_failures_and_lost_ack() {
  Rig r;
  r.p.enable_parameter_reads(80);
  r.ready();
  r.request(20, BLOCK_PARAMETER_80);
  r.reply({10, 3, 2, 0xFF, 0x66}, 21); // Wrong expected byte count, not a valid block.
  auto damaged = reading(80, 1).reply;
  damaged.back() ^= 1;
  r.feed(damaged, 22);
  assert(r.l.parameters.empty() && r.l.states.size() == 1 && r.p.crc_errors() >= 1);
  r.feed(std::vector<uint8_t>{10, 3, 20, 0}, 23);
  r.feed(reading(80, 1).reply, 54); // Partial frame abandoned after a 30ms gap.
  assert(r.l.parameters.size() == 1 && r.l.parameters.back().second == 1);

  Rig invalid;
  invalid.p.enable_parameter_reads(38);
  invalid.ready();
  invalid.request(20, BLOCK_PARAMETER_38);
  const auto &reference = reading(38, 1).reply;
  std::vector<uint8_t> unsupported(reference.begin(), reference.end() - 2);
  unsupported[14] = 2; // P38's actual word is now 0002, with a fresh valid CRC.
  invalid.reply(unsupported, 21);
  assert(invalid.l.parameters.empty() && invalid.l.failures.back() == Failure::INVALID_VALUE);
  assert(!invalid.p.next_request(26, invalid.tx)); // No tight retry loop on errors.

  Rig lost;
  lost.ready();
  assert(lost.p.enqueue_parameter(80, 1, 20));
  assert(lost.p.next_request(20, lost.tx) && lost.tx[1] == 6);
  const auto original = lost.tx;
  assert(!lost.p.next_request(220, lost.tx));
  assert(lost.l.failures.back() == Failure::TIMEOUT);
  lost.feed(original, 221); // A late ACK is not accepted.
  lost.request(226, BLOCK_PARAMETER_80); // Read back, never repeat the write.
  lost.feed(reading(80, 1).reply, 227);
  assert(lost.l.parameters.back().second == 1 && lost.l.writes.empty());

  Rig except;
  except.p.enable_parameter_reads(80);
  except.ready();
  except.request(20, BLOCK_PARAMETER_80);
  except.reply({10, 0x83, 2}, 21);
  assert(except.l.parameters.empty() && except.l.failures.back() == Failure::EXCEPTION);
  assert(!except.p.next_request(26, except.tx));
  except.request(510, REG_STATE, 1); // Gate polling keeps working after a failed block read.

  Rig timeout;
  timeout.p.enable_parameter_reads(80);
  timeout.ready();
  timeout.request(20, BLOCK_PARAMETER_80);
  timeout.feed(std::vector<uint8_t>{10, 3, 20, 0}, 21);
  assert(!timeout.p.next_request(220, timeout.tx));
  assert(timeout.l.failures.back() == Failure::TIMEOUT);
  assert(!timeout.p.next_request(225, timeout.tx));

  Rig stale;
  stale.p.enable_parameter_reads(80);
  stale.p.set_offline_timeout(15);
  stale.ready();
  stale.request(20, BLOCK_PARAMETER_80);
  stale.feed(reading(80, 1).reply, 30);
  assert(!stale.p.online(30)); // A parameter reply cannot refresh gate telemetry freshness.
}

void stop_during_block_read() {
  Rig r;
  r.p.enable_parameter_reads(80);
  r.ready();
  r.request(20, BLOCK_PARAMETER_80);
  assert(r.p.enqueue_command(CMD_OPEN, 21));
  assert(r.p.enqueue_command(CMD_STOP, 22));
  assert(!r.p.next_request(30, r.tx));
  assert(!r.p.next_request(220, r.tx)); // Pending read times out first.
  assert(r.p.next_request(225, r.tx));
  assert(r.tx == make_request(10, 6, REG_COMMAND, CMD_STOP));
  r.feed(r.tx, 226);
  r.request(231, REG_STATE, 1);
  r.reply({10, 3, 2, 0xFF, 0x66}, 232);
  assert(!r.p.next_request(237, r.tx)); // Cancelled OPEN never reaches the bus.
}

int main() {
  position_filter_and_captured_states();
  captured_writes_and_parameter_readback();
  startup_periodic_and_reconnection();
  read_failures_and_lost_ack();
  stop_during_block_read();
  std::puts("PASS: 977 captured states (only 3 filtered), 26 captured writes, 4 captured block reads, "
            "startup/periodic/readback/reconnect, error handling, STOP, rollover");
}
