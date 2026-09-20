#include "protocol.h"
#include "captured_inputs.h"
#include "captured_fixtures.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace esphome::roger_edge1;

struct Listener : ProtocolListener {
  std::vector<InputStatus> inputs;
  unsigned states{0}, writes{0}, parameters{0}, invalidations{0}, errors{0};
  void on_gate_status(uint16_t) override { states++; }
  void on_write_ack(uint16_t, uint16_t) override { writes++; }
  void on_parameter_value(uint8_t, uint8_t) override { parameters++; }
  void on_input_status(const InputStatus &value) override { inputs.push_back(value); }
  void on_inputs_invalidated() override { invalidations++; }
  void on_protocol_error(Failure, uint16_t, uint16_t, uint8_t) override { errors++; }
};

struct Rig {
  Protocol p;
  Listener l;
  Frame tx{};
  Rig() { p.set_listener(&l); }
  uint16_t reg() const { return (uint16_t(tx[2]) << 8) | tx[3]; }
  template<typename Bytes> void feed(const Bytes &bytes, uint32_t now) {
    for (uint8_t b : bytes) p.receive(b, now);
  }
  void reply(std::vector<uint8_t> bytes, uint32_t now) {
    const uint16_t crc = modbus_crc(bytes.data(), bytes.size());
    bytes.push_back(crc & 255);
    bytes.push_back(crc >> 8);
    feed(bytes, now);
  }
  void state(uint32_t now) { reply({10, 3, 2, 0xFF, 0x66}, now); }
  void inputs(uint32_t now, uint16_t value, uint16_t auxiliary = 0x0036) {
    reply({10, 3, 4, uint8_t(value >> 8), uint8_t(value), uint8_t(auxiliary >> 8), uint8_t(auxiliary)}, now);
  }
  void ready(uint32_t now = 10) {
    p.enable_input_reads();
    assert(p.next_request(now, tx) && reg() == REG_STATE);
    state(now + 1);
    assert(p.next_request(now + 6, tx) && tx == make_request(10, 3, REG_INPUTS, 2));
  }
};

void captured_replies_and_independent_masks() {
  unsigned count = 0;
  for (const auto &fixture : CAPTURED_INPUTS) {
    Rig r;
    assert(!r.p.inputs_online(0) && r.l.inputs.empty());
    r.ready();
    assert(r.tx == fixture.request);
    for (size_t i = 0; i < fixture.reply.size(); i++) {
      r.p.receive(fixture.reply[i], 17 + i);
      if (i + 1 < fixture.reply.size()) assert(r.l.inputs.empty());
    }
    assert(r.p.inputs_online(25) && r.l.inputs.size() == 1);
    const auto &status = r.l.inputs.back();
    assert(status.raw == fixture.raw && status.auxiliary_raw == fixture.auxiliary);
    assert(status.ft1_obstructed() == (fixture.raw == 0x10));
    assert(status.ft2_obstructed() == (fixture.raw == 0x20));
    assert(r.l.parameters == 0 && r.l.states == 1 && r.l.writes == 0);
    r.feed(fixture.reply, 26); // Unsolicited duplicate never refreshes telemetry.
    assert(r.l.inputs.size() == 1);
    count++;
  }
  assert(count == 33);
  // Synthetic cases absent from the captures: both beams and unrelated active bits.
  struct Case { uint16_t raw, auxiliary; bool ft1, ft2; };
  for (const auto &test : {Case{0x30, 0, true, true}, Case{0, 0xFFFF, false, false},
                          Case{0xFFCF, 0x36, false, false}, Case{0xFFDF, 0x36, true, false},
                          Case{0xFFEF, 0x36, false, true}, Case{0xFFFF, 0, true, true}}) {
    Rig r;
    r.ready();
    r.inputs(17, test.raw, test.auxiliary);
    assert(r.l.inputs.back().ft1_obstructed() == test.ft1);
    assert(r.l.inputs.back().ft2_obstructed() == test.ft2);
  }
}

void persistent_capture_sequence() {
  std::vector<const CapturedInput *> sequence;
  for (const auto &fixture : CAPTURED_INPUTS)
    if (std::strcmp(fixture.file, "uart_check_app_web_2.sr") == 0) sequence.push_back(&fixture);
  assert(sequence.size() == 21);
  Rig r;
  r.p.enable_input_reads();
  size_t index = 0;
  for (uint32_t now = 0; now < 11000 && index < sequence.size(); now++) {
    if (!r.p.next_request(now, r.tx)) continue;
    assert(r.tx[1] == 3);
    if (r.reg() == REG_STATE) r.state(now);
    else {
      assert(r.reg() == REG_INPUTS && r.tx == sequence[index]->request);
      r.feed(sequence[index]->reply, now);
      assert(r.l.inputs.back().raw == sequence[index]->raw);
      index++;
    }
  }
  assert(index == 21 && r.l.invalidations == 0);
  // The four consecutive FT1 and five FT2 samples remain active until an explicit clear read.
  for (size_t i = 4; i <= 7; i++) assert(r.l.inputs[i].ft1_obstructed());
  assert(!r.l.inputs[8].ft1_obstructed());
  for (size_t i = 12; i <= 16; i++) assert(r.l.inputs[i].ft2_obstructed());
  assert(!r.l.inputs[17].ft2_obstructed());
}

void independent_freshness_and_recovery() {
  Rig r;
  r.p.enable_input_reads();
  for (uint32_t now = 0; now <= 4000; now++) {
    if (r.p.next_request(now, r.tx)) {
      if (r.reg() == REG_STATE) r.state(now);
      else if (now < 500 || now >= 3500) r.inputs(now, now < 500 ? 0x10 : 0);
      // Drop other input replies: gate status still arrives throughout the outage.
    }
    if (now == 3009) assert(r.p.inputs_online(now) && r.l.invalidations == 0);
    if (now == 3010) assert(!r.p.inputs_online(now) && r.l.invalidations == 1 && r.p.online(now));
    if (now == 3400) {
      assert(!r.p.inputs_online(now) && r.l.inputs.size() == 1 && r.l.invalidations == 1);
      assert(r.l.inputs.back().ft1_obstructed()); // Staleness did not inject a false clear callback.
    }
  }
  assert(r.p.inputs_online(4000) && !r.l.inputs.back().ft1_obstructed());
  assert(r.l.invalidations == 1 && r.l.errors > 0);

  Rig gate_failed;
  gate_failed.p.enable_input_reads();
  for (uint32_t now = 0; now <= 4500; now++) {
    if (!gate_failed.p.next_request(now, gate_failed.tx)) continue;
    if (gate_failed.reg() == REG_INPUTS) gate_failed.inputs(now, 0x20);
    else if (now < 500) gate_failed.state(now);
  }
  assert(!gate_failed.p.online(4500) && gate_failed.p.inputs_online(4500));
  assert(gate_failed.l.invalidations == 0); // Input reads do not depend on successful gate polls.
  assert(!gate_failed.p.enqueue_command(CMD_OPEN, 4500));
  assert(gate_failed.p.enqueue_command(CMD_STOP, 4500));

  Rig wrap;
  const uint32_t start = 0xFFFFF800;
  wrap.ready(start);
  wrap.inputs(start + 7, 0x30);
  assert(wrap.p.inputs_online(start + 3006));
  // receive() expires inputs too, even if noise keeps the UART input buffer busy.
  wrap.p.receive(0xFF, start + 3007);
  assert(!wrap.p.inputs_online(start + 3007) && wrap.l.invalidations == 1);
  wrap.p.receive(0xFF, start + 3008);
  assert(wrap.l.invalidations == 1);

  Rig timeout;
  timeout.p.set_inputs_timeout(1000);
  timeout.ready();
  timeout.inputs(17, 0x20);
  assert(timeout.p.inputs_online(1016));
  timeout.p.next_request(1017, timeout.tx);
  assert(!timeout.p.inputs_online(1017) && timeout.l.invalidations == 1);
}

void bad_frames_never_mean_clear() {
  Rig r;
  r.ready();
  r.reply({10, 3, 2, 0, 0}, 17); // 0x1582-like event reply cannot complete a two-register read.
  auto bad = CAPTURED_INPUTS[0].reply;
  bad.back() ^= 1;
  r.feed(bad, 18);
  assert(r.l.inputs.empty() && !r.p.inputs_online(18) && r.p.crc_errors() >= 1);
  r.feed(std::vector<uint8_t>{10, 3, 4, 0}, 19);
  r.inputs(50, 0x10); // Abandon partial bytes after the 30ms gap, then resynchronize.
  assert(r.l.inputs.size() == 1 && r.l.inputs.back().ft1_obstructed());

  assert(r.p.next_request(510, r.tx) && r.reg() == REG_STATE);
  r.state(511);
  assert(r.p.next_request(516, r.tx) && r.reg() == REG_INPUTS);
  r.reply({10, 0x83, 4}, 517);
  assert(r.l.errors == 1 && r.l.inputs.size() == 1 && r.p.inputs_online(517));
  assert(!r.p.next_request(522, r.tx)); // Wait for the normal interval after an exception.
  r.p.receive(0, 3050);
  assert(r.l.invalidations == 1 && !r.p.inputs_online(3050));

  Rig late;
  late.ready();
  late.inputs(216, 0); // Reply at the 200ms timeout is rejected even before next_request().
  assert(late.l.inputs.empty() && late.l.errors == 1 && !late.p.inputs_online(216));

  Rig never;
  never.p.enable_input_reads();
  for (uint32_t now = 0; now < 4000; now++) never.p.next_request(now, never.tx);
  assert(never.l.inputs.empty() && never.l.invalidations == 0 && !never.p.inputs_online(4000));
}

void scheduler_commands_and_parameters() {
  Rig r;
  r.p.enable_input_reads();
  r.p.enable_parameter_reads(38);
  r.p.enable_parameter_reads(80);
  r.p.set_parameter_update_interval(1000);
  std::vector<uint32_t> input_times, state_times;
  for (uint32_t now = 0; now <= 2500; now++) {
    if (now == 50) assert(r.p.enqueue_parameter(80, 1, now));
    if (!r.p.next_request(now, r.tx)) continue;
    if (r.tx[1] == 6) r.feed(r.tx, now);
    else if (r.reg() == REG_STATE) { state_times.push_back(now); r.state(now); }
    else if (r.reg() == REG_INPUTS) { input_times.push_back(now); r.inputs(now, 0); }
    else {
      const uint8_t parameter = r.reg() == BLOCK_PARAMETER_38 ? 38 : 80;
      for (const auto &fixture : CAPTURED_READS)
        if (fixture.parameter == parameter && fixture.value == 1) r.feed(fixture.reply, now);
    }
  }
  assert(input_times.size() == 5 && input_times[0] == 10);
  for (size_t i = 1; i < input_times.size(); i++) assert(input_times[i] - input_times[i-1] == 500);
  assert(state_times.size() >= 5 && r.l.parameters >= 6 && r.l.writes == 1);

  Rig stop;
  stop.ready(); // Input request pending.
  assert(stop.p.enqueue_command(CMD_OPEN, 17));
  assert(stop.p.enqueue_command(CMD_STOP, 18));
  assert(!stop.p.next_request(19, stop.tx));
  assert(!stop.p.next_request(216, stop.tx));
  assert(stop.p.next_request(221, stop.tx));
  assert(stop.tx == make_request(10, 6, REG_COMMAND, CMD_STOP));
  stop.feed(stop.tx, 222);
  assert(stop.p.next_request(227, stop.tx) && stop.reg() == REG_STATE);
  stop.state(228);
  assert(!stop.p.next_request(233, stop.tx)); // Queued OPEN was cancelled.

  Rig flood;
  flood.p.enable_input_reads();
  unsigned sent_motion = 0;
  for (uint32_t now = 0; now <= 3000; now++) {
    if (flood.p.online(now)) assert(flood.p.enqueue_command(CMD_OPEN, now));
    if (!flood.p.next_request(now, flood.tx)) continue;
    if (flood.tx[1] == 6) { flood.feed(flood.tx, now); sent_motion++; }
    else if (flood.reg() == REG_STATE) flood.state(now);
    else { assert(flood.reg() == REG_INPUTS); flood.inputs(now, 0); }
  }
  assert(sent_motion > 10 && flood.l.states > 10 && flood.l.inputs.size() >= 5);
  assert(flood.p.online(3000) && flood.p.inputs_online(3000));

  Rig slow;
  slow.p.enable_input_reads();
  slow.p.set_update_interval(100);
  slow.p.set_inputs_update_interval(100);
  slow.p.set_response_timeout(1000);
  std::vector<uint16_t> requests;
  for (uint32_t now = 0; now <= 4200; now++)
    if (slow.p.next_request(now, slow.tx)) requests.push_back(slow.reg());
  assert(requests.size() == 5);
  assert((requests == std::vector<uint16_t>{REG_STATE, REG_INPUTS, REG_STATE, REG_INPUTS, REG_STATE}));

  Rig disabled;
  for (uint32_t now = 0; now <= 1500; now++) {
    if (!disabled.p.next_request(now, disabled.tx)) continue;
    assert(disabled.reg() == REG_STATE);
    disabled.state(now);
  }
  assert(disabled.l.inputs.empty());
}

int main() {
  captured_replies_and_independent_masks();
  persistent_capture_sequence();
  independent_freshness_and_recovery();
  bad_frames_never_mean_clear();
  scheduler_commands_and_parameters();
  std::puts("PASS: 33 captured input replies, persistent sequence, independent masks, freshness/recovery, "
            "invalid/partial/late frames, STOP, polling fairness, parameters, rollover");
}
