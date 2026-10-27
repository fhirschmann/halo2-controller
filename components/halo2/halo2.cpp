#include "halo2.h"

#include <algorithm>
#include <cinttypes>
#include "esphome/core/application.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

namespace esphome::halo2 {

namespace protocol = halo2_protocol;

static const char *const TAG = "halo2";
static constexpr uint32_t MODE_PREFERENCE_KEY = 0x48414C32;
static constexpr uint32_t RECOVERY_MAX_DELAY_MS = 30000;
// A command batch the lamp did not acknowledge is resent with the complete
// requested state; afterwards fresh status polls reconcile the UI.
static constexpr uint8_t MAX_COMMAND_RETRIES = 3;

void Halo2::setup() {
  disable_loop();
  // LightState restores its preferences before this component is initialized.
  // Restore the bridge's last known state without transmitting on boot.
  mode_preference_ = global_preferences->make_preference<uint8_t>(MODE_PREFERENCE_KEY);
  if (!mode_preference_.load(&saved_mode_) || saved_mode_ < SavedMode::FRONT || saved_mode_ > SavedMode::BOTH)
    saved_mode_ = SavedMode::BOTH;
  LampState initial;
  initial.set_selection(saved_mode_ & SavedMode::FRONT, saved_mode_ & SavedMode::BACK);
  initial.front_brightness = initial_brightness_[0];
  initial.back_brightness = initial_brightness_[1];
  light_->restore_into(initial);
  lamp_state_ = initial;
  save_mode_();
  publish_light_();
  // Commands are accepted only after the first lamp reply, so restored UI
  // defaults never overwrite the lamp's presence sensor or timeout on boot.
  lifecycle_ = BridgeLifecycle();
  // Setup, reception, transmission and recovery all advance in loop().
  enable_loop();
  start_radio_();
}

void Halo2::start_radio_() {
  if (!lifecycle_.initializing() && !lifecycle_.retry()) return;
  publish_status_("Initializing radio");
  if (!radio_.setup(radio_address_, radio_channel_)) recover_radio_();
}

void Halo2::cancel_requests_() {
  // Clearing the request phases also makes a late batch completion harmless.
  commands_.clear();
  status_poll_.cancel();
}

void Halo2::recover_radio_() {
  if (!lifecycle_.on_radio_failure()) return;
  // Never replay an interrupted command batch after reconnecting to the lamp.
  cancel_requests_();
  status_set_warning();
  const char *error = radio_.last_error();
  if (error == nullptr) error = "BM5602 unavailable";
  char status[160];
  snprintf(status, sizeof(status), "%s; retrying in %" PRIu32 " s", error, recovery_delay_ / 1000);
  publish_status_(status);
  ESP_LOGW(TAG, "%s", status);
  set_timeout("radio_recovery", recovery_delay_, [this]() { start_radio_(); });
  recovery_delay_ = std::min(recovery_delay_ * 2, RECOVERY_MAX_DELAY_MS);
}

void Halo2::publish_address_() {
  char address[80];
  snprintf(address, sizeof(address), "%02X:%02X:%02X:%02X / %u MHz", radio_address_[0], radio_address_[1],
           radio_address_[2], radio_address_[3], protocol::RADIO_BASE_FREQUENCY_MHZ + radio_channel_);
  if (radio_address_sensor_ != nullptr) radio_address_sensor_->publish_state(address);
  ESP_LOGI(TAG, "Radio address %s (register order)", address);
}

void Halo2::radio_ready_() {
  lifecycle_.on_radio_ready();
  recovery_delay_ = RECOVERY_INITIAL_DELAY_MS;
  status_poll_.reset_failures();
  status_clear_warning();
  ESP_LOGI(TAG, "BM5602 ready, %u MHz, 125 kbit/s", protocol::RADIO_BASE_FREQUENCY_MHZ + radio_channel_);
  publish_address_();
  publish_status_("Listening");
  // Read the lamp after recovery instead of imposing the restored UI state.
  status_poll_.schedule(millis(), StatusPoll::SETTLE_MS);
}

void Halo2::loop() {
  if (lifecycle_.recovering()) return;
  if (radio_.last_error() != nullptr) {
    recover_radio_();
    return;
  }
  if (lifecycle_.initializing()) {
    if (!radio_.ready()) return;
    radio_ready_();
  }
  if (radio_.take_tx_done()) {
    const uint32_t now = millis();
    if (commands_.on_tx_done(now)) {
      status_poll_.schedule(now, StatusPoll::SETTLE_MS);
    } else {
      status_poll_.on_tx_done(now);
    }
  }
  // Fast command/RX processing is independent of periodic lamp polling.
  const uint32_t now = App.get_loop_component_start_time();
  if (now - last_process_at_ >= processing_interval_) {
    last_process_at_ = now;
    process_radio_();
  }
#ifdef USE_HALO2_SELF_TEST
  self_test_loop_();
#endif
}

void Halo2::dump_config() {
  ESP_LOGCONFIG(TAG, "ScreenBar HALO 2:");
  radio_.dump_config();
  ESP_LOGCONFIG(TAG, "  Processing interval: %" PRIu32 " ms", processing_interval_);
  ESP_LOGCONFIG(TAG, "  Frequency: %u MHz", protocol::RADIO_BASE_FREQUENCY_MHZ + radio_channel_);
  ESP_LOGCONFIG(TAG, "  Command debounce: %" PRIu32 " ms", commands_.debounce());
  LOG_UPDATE_INTERVAL(this);
  LOG_NUMBER("  ", "Front brightness", brightness_numbers_[0]);
  LOG_NUMBER("  ", "Back brightness", brightness_numbers_[1]);
  LOG_SELECT("  ", "Lighting mode", section_select_);
  LOG_SELECT("  ", "Ultrasonic sensor", ultrasonic_select_);
}

void Halo2::request_state_(const LampState &requested) {
  commands_.request(lamp_state_, requested);
  lamp_state_ = requested;
  save_mode_();
}

bool Halo2::send_batch_(std::span<const protocol::Frame> frames) {
  if (!radio_.send_batch(frames)) {
    recover_radio_();
    return false;
  }
  return true;
}

protocol::Frame Halo2::make_frame_(Command command, bool auto_brightness) {
  const auto &requested = lamp_state_;
  ESP_LOGD(TAG, "TX command 0x%02X, power %s, mode %u/%u, front %u%%, back %u%%, %u K", command, ONOFF(requested.power),
           requested.selected(Section::FRONT), requested.selected(Section::BACK), requested.front_brightness,
           requested.back_brightness, requested.color_temperature);
  auto payload = protocol::make_payload(command, requested, auto_brightness);
  // The lamp's radio drops a request whose PID and CRC equal the previous one
  // and answers with its stale reply (NickCao's PROTOCOL.md). The BM5602
  // assigns PIDs in hardware, so consecutive status queries also differ in the
  // rear temperature field, which command 04 does not apply.
  if (command == Command::STATUS) {
    const uint16_t offset = TEMPERATURE_STEP_K * (app_pid_ % 4U);
    const uint16_t rear = requested.color_temperature + offset <= MAX_TEMPERATURE_K
                              ? requested.color_temperature + offset
                              : requested.color_temperature - offset;
    payload.back_temperature_be = convert_big_endian(rear);
  }
  last_pcf_ = protocol::request_pcf(app_pid_++);
  return protocol::make_frame(last_pcf_, payload, radio_.address());
}

void Halo2::start_auto_brightness() {
  if (!accepts_commands()) return;
  commands_.auto_brightness();
}

void Halo2::control_ultrasonic(size_t index) {
  if (!accepts_commands() || index > static_cast<size_t>(UltrasonicTimeout::MINUTES_15) + 1U) return;
  auto requested = lamp_state_;
  requested.ultrasonic_enabled = index != 0;
  if (requested.ultrasonic_enabled) requested.ultrasonic_timeout = static_cast<UltrasonicTimeout>(index - 1);
  request_state_(requested);
  publish_ultrasonic_();
}

void Halo2::publish_ultrasonic_() {
  // Disabling presence detection retains the lamp's last timeout duration.
  const auto &requested = lamp_state_;
  ultrasonic_select_->publish_state(requested.ultrasonic_enabled ? static_cast<size_t>(requested.ultrasonic_timeout) + 1
                                                                 : size_t{0});
}

void Halo2::publish_status_(const char *status) {
  if (status == nullptr) status = "Radio error";
  if (radio_status_->state != status) radio_status_->publish_state(status);
}

void Halo2::save_mode_() {
  const auto &requested = lamp_state_;
  const uint8_t mode = (requested.selected(Section::FRONT) ? SavedMode::FRONT : SavedMode::NONE) |
                       (requested.selected(Section::BACK) ? SavedMode::BACK : SavedMode::NONE);
  if (mode == saved_mode_) return;
  saved_mode_ = mode;
  mode_preference_.save(&saved_mode_);
}

void Halo2::apply_received_(const protocol::ReceivedPacket &received) {
  const auto &state = received.state;
  const bool changed = !lifecycle_.active() || lamp_state_ != state;
  lamp_state_ = state;
  lifecycle_.on_received_state();
  if (changed) {
    save_mode_();
    publish_light_();
    publish_ultrasonic_();
  }
  status_clear_warning();
  publish_status_(received.is_reply() ? "Lamp status received" : "Controller update received");
}

void Halo2::update() {
  if (lifecycle_.linked() && commands_.idle() && status_poll_.idle()) status_poll_.schedule(millis(), 0);
}

void Halo2::process_radio_() {
  if (!lifecycle_.linked() || commands_.transmitting() || status_poll_.transmitting()) return;
  if (commands_.pending()) {
    send_commands_();
    return;
  }
  receive_packet_();
  poll_status_();
}

void Halo2::send_commands_() {
  if (!radio_.idle()) return;
  const auto batch = commands_.take(lamp_state_, millis());
  if (!batch) return;
  status_poll_.cancel();
  std::array<protocol::Frame, 2> frames;
  std::transform(batch->begin(), batch->end(), frames.begin(), [this, &batch](Command command) {
    return make_frame_(command, batch->auto_brightness && command == Command::SETTINGS);
  });
  const uint32_t failures = radio_.tx_failures();
  if (!send_batch_(std::span(frames).first(batch->count))) return;
  if (radio_.tx_failures() == failures) {
    command_retries_ = 0;
    status_clear_warning();
    publish_status_("Command acknowledged");
    return;
  }
  if (command_retries_ < MAX_COMMAND_RETRIES) {
    ++command_retries_;
    ESP_LOGW(TAG, "Lamp did not acknowledge a command; resending state (%u/%u)", command_retries_,
             MAX_COMMAND_RETRIES);
    commands_.resend();
    return;
  }
  command_retries_ = 0;
  status_set_warning();
  publish_status_("Lamp did not acknowledge command");
  ESP_LOGW(TAG, "Lamp did not acknowledge a command; giving up, status polling will reconcile");
}

void Halo2::receive_packet_() {
  protocol::ReceivedPacket received;
  const uint32_t previous_rx_count = radio_.rx_count();
  const bool received_state = radio_.poll(received);
  if (radio_.rx_count() != previous_rx_count) {
    // Log every passive capture, including rejected ones, for diagnostics.
    const auto &frame = radio_.rx_frame();
    ESP_LOGD(TAG, "RX passive frame: %s, decoded %s", format_hex_pretty(frame.data(), radio_.rx_length()).c_str(),
             YESNO(received_state));
  }
  if (!received_state) return;
  if (!received.is_reply()) {
    apply_received_(received);
    status_poll_.schedule(millis(), StatusPoll::SETTLE_MS);
    return;
  }
  const auto reply =
      status_poll_.on_reply(received.pcf & protocol::PCF_PID_MASK, received.command == Command::STATUS, millis());
  if (reply != StatusPoll::Reply::STATE || light_->transitioning()) return;
  apply_received_(received);
  const auto &state = received.state;
  last_status_ = state;
  last_status_at_ = millis();
  ++status_count_;
  ESP_LOGD(TAG, "Lamp status: power %s, mode %u/%u, front %u%%, back %u%%, %u K, ultrasonic %s, timeout %u min",
           ONOFF(state.power), state.selected(Section::FRONT), state.selected(Section::BACK), state.front_brightness,
           state.back_brightness, state.color_temperature, ONOFF(state.ultrasonic_enabled),
           ULTRASONIC_TIMEOUT_MINUTES[static_cast<size_t>(state.ultrasonic_timeout)]);
}

void Halo2::poll_status_() {
  const uint32_t now = millis();
  if (status_poll_.expire(now)) {
    ++status_timeouts_;
    const auto &reply = radio_.last_reply();
    ESP_LOGD(TAG, "Lamp status query timed out (%u consecutive, IRQ %02" PRIX32 ", STATUS %02X, last ACK payload %s)",
             status_poll_.failures(), radio_.last_irq(), radio_.last_status(),
             format_hex_pretty(reply.data(), reply.size()).c_str());
    if (status_poll_.failures() >= StatusPoll::FAILURE_THRESHOLD) {
      status_set_warning();
      publish_status_("Lamp status unavailable; retaining last known state");
    }
  }
  if (!status_poll_.due(now) || light_->transitioning() || !radio_.idle()) return;
  ESP_LOGV(TAG, "%s lamp status", status_poll_.reading() ? "Reading" : "Refreshing");
  const std::array<protocol::Frame, 1> frames{make_frame_(Command::STATUS)};
  if (send_batch_(frames)) {
    status_poll_.begin_request(last_pcf_ & protocol::PCF_PID_MASK);
  }
}

#ifdef USE_HALO2_SELF_TEST
// Hardware self-test: each step requests the lamp's original state with one
// change through the normal command path, then waits for a fresh status
// reply that matches. The last step restores the original state.
void Halo2::self_test_loop_() {
  static const char *const STAG = "selftest";
  static constexpr uint32_t BOOT_SETTLE_MS = 8000, STEP_TIMEOUT_MS = 15000, STEP_PAUSE_MS = 1000,
                            LONG_OFF_MS = 60000;
  static constexpr uint8_t ROUNDS = 3;
  const uint32_t now = millis();
  switch (self_test_phase_) {
    case SelfTestPhase::WAIT_ORIGIN: {
      if (!lifecycle_.active() || last_status_at_ == 0 || now < BOOT_SETTLE_MS) return;
      const LampState o = last_status_;
      self_test_origin_ = o;
      auto bump = [](uint8_t value) {
        return static_cast<uint8_t>(value >= MAX_BRIGHTNESS_PERCENT ? value - 1 : value + 1);
      };
      self_test_steps_count_ = 0;
      auto add = [this](const char *name, const LampState &state, uint32_t hold = 0) {
        self_test_names_[self_test_steps_count_] = name;
        self_test_holds_[self_test_steps_count_] = hold;
        self_test_steps_[self_test_steps_count_++] = state;
      };
      LampState s = o;
      if (o.selected(Section::BACK)) {
        s.back_brightness = bump(o.back_brightness);
        add("back brightness", s);
      }
      s = o;
      if (o.selected(Section::FRONT)) {
        s.front_brightness = bump(o.front_brightness);
        add("front brightness", s);
      }
      s = o;
      s.color_temperature = o.color_temperature <= MAX_TEMPERATURE_K - 100 ? o.color_temperature + 100
                                                                            : o.color_temperature - 100;
      add("color temperature", s);
      s = o;
      s.ultrasonic_timeout = static_cast<UltrasonicTimeout>((static_cast<uint8_t>(o.ultrasonic_timeout) + 1) % 4);
      add("presence timeout", s);
      s = o;
      s.ultrasonic_enabled = !o.ultrasonic_enabled;
      add("presence enable", s);
      s = o;
      s.selection = o.selection == LampSelection::BOTH ? LampSelection::FRONT_ONLY : LampSelection::BOTH;
      add("lamp selection", s);
      s = o;
      s.power = !o.power;
      // The last round leaves the lamp off for a minute before turning it on.
      add("power toggle", s, self_test_round_ + 1 == ROUNDS && o.power ? LONG_OFF_MS : 0);
      add("restore original", o);
      ESP_LOGI(STAG, "Round %u/%u origin: power %s, mode %u, front %u%%, back %u%%, %u K, presence %s, timeout %u min; %u steps",
               self_test_round_ + 1, ROUNDS, ONOFF(o.power), static_cast<unsigned>(o.selection), o.front_brightness,
               o.back_brightness, o.color_temperature, ONOFF(o.ultrasonic_enabled),
               ULTRASONIC_TIMEOUT_MINUTES[static_cast<size_t>(o.ultrasonic_timeout)], self_test_steps_count_);
      if (self_test_round_ == 0) {
        self_test_tx_base_ = radio_.tx_count();
        self_test_fail_base_ = radio_.tx_failures();
        self_test_retry_base_ = radio_.tx_retries();
        self_test_status_base_ = status_count_;
        self_test_timeout_base_ = status_timeouts_;
      }
      self_test_step_ = 0;
      self_test_phase_ = SelfTestPhase::START_STEP;
      self_test_pause_ = STEP_PAUSE_MS;
      self_test_at_ = now;
      return;
    }
    case SelfTestPhase::START_STEP: {
      if (now - self_test_at_ < self_test_pause_ || !commands_.idle()) return;
      if (self_test_step_ >= self_test_steps_count_) {
        self_test_total_ += self_test_steps_count_;
        if (++self_test_round_ < ROUNDS) {
          self_test_phase_ = SelfTestPhase::WAIT_ORIGIN;
          return;
        }
        ESP_LOGI(STAG,
                 "RESULT: %u/%u steps passed; radio TX %" PRIu32 ", unacknowledged %" PRIu32 ", retries %" PRIu32
                 "; status replies %" PRIu32 ", status timeouts %" PRIu32 ", chip resets %" PRIu32,
                 self_test_passed_, self_test_total_, radio_.tx_count() - self_test_tx_base_,
                 radio_.tx_failures() - self_test_fail_base_, radio_.tx_retries() - self_test_retry_base_,
                 status_count_ - self_test_status_base_, status_timeouts_ - self_test_timeout_base_,
                 radio_.chip_resets());
        self_test_phase_ = SelfTestPhase::DONE;
        return;
      }
      request_state_(self_test_steps_[self_test_step_]);
      publish_light_();
      publish_ultrasonic_();
      self_test_at_ = now;
      self_test_phase_ = SelfTestPhase::WAIT_RESULT;
      return;
    }
    case SelfTestPhase::WAIT_RESULT: {
      const auto &want = self_test_steps_[self_test_step_];
      const bool matched = last_status_at_ > self_test_at_ && commands_.idle() && last_status_ == want;
      if (!matched && now - self_test_at_ <= STEP_TIMEOUT_MS) return;
      self_test_passed_ += matched;
      const auto &got = last_status_;
      ESP_LOGI(STAG,
               "Step %u %s: %s after %" PRIu32 " ms (lamp: power %s, mode %u, front %u%%, back %u%%, %u K, "
               "presence %s, timeout %u min)",
               self_test_step_ + 1, self_test_names_[self_test_step_], matched ? "PASS" : "FAIL", now - self_test_at_,
               ONOFF(got.power), static_cast<unsigned>(got.selection), got.front_brightness, got.back_brightness,
               got.color_temperature, ONOFF(got.ultrasonic_enabled),
               ULTRASONIC_TIMEOUT_MINUTES[static_cast<size_t>(got.ultrasonic_timeout)]);
      self_test_pause_ = STEP_PAUSE_MS + (matched ? self_test_holds_[self_test_step_] : 0);
      if (self_test_pause_ > STEP_PAUSE_MS) ESP_LOGI(STAG, "Holding for %" PRIu32 " s", self_test_pause_ / 1000);
      ++self_test_step_;
      self_test_at_ = now;
      self_test_phase_ = SelfTestPhase::START_STEP;
      return;
    }
    case SelfTestPhase::DONE:
      return;
  }
}
#endif

}  // namespace esphome::halo2
