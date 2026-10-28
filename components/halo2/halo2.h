#pragma once

#include <span>
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/core/preferences.h"
#include "esphome/components/button/button.h"
#include "esphome/components/light/light_output.h"
#include "esphome/components/number/number.h"
#include "esphome/components/select/select.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "bridge_state.h"
#include "command_queue.h"
#include "halo2_protocol.h"
#include "bm5602_radio.h"

namespace esphome::halo2 {

class Halo2Light;

class Halo2 : public PollingComponent {
 public:
  void setup() override;
  void update() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::LATE; }

  BM5602Radio &get_radio() { return radio_; }
  void set_light(Halo2Light *value) { light_ = value; }
  void set_brightness_number(number::Number *value, Section section) {
    brightness_numbers_[static_cast<size_t>(section)] = value;
  }
  void set_initial_brightness(Section section, uint8_t value) {
    initial_brightness_[static_cast<size_t>(section)] = value;
  }
  void set_ultrasonic_select(select::Select *value) { ultrasonic_select_ = value; }
  void set_section_select(select::Select *value) { section_select_ = value; }
  void set_radio_status(text_sensor::TextSensor *value) { radio_status_ = value; }
  void set_radio_address_sensor(text_sensor::TextSensor *value) { radio_address_sensor_ = value; }
  void set_radio_address(const halo2_protocol::Address &value) { radio_address_ = value; }
  void set_radio_channel(uint8_t value) { radio_channel_ = value; }
  void set_processing_interval(uint32_t value) { processing_interval_ = value; }
  void set_command_debounce(uint32_t value) { commands_.set_debounce(value); }
  void start_auto_brightness();

  bool accepts_commands() const { return lifecycle_.active(); }
  void control_light();
  void control_brightness(Section section, float value);
  void control_selection(size_t index);
  void control_ultrasonic(size_t index);
  void resend() {
    if (accepts_commands()) commands_.resend();
  }

 protected:
  // Persisted selection is a bitmask, distinct from LampSelection's wire values.
  enum SavedMode : uint8_t { NONE = 0, FRONT = 1, BACK = 2, BOTH = FRONT | BACK };
  static constexpr uint32_t RECOVERY_INITIAL_DELAY_MS = 1000;
  // After a command or an overheard remote frame the lamp keeps reporting its old
  // power state for more than a second (it still answered "on" 1.2 s after an off).
  // Status replies inside this window are stale and are not applied.
  static constexpr uint32_t STATE_SETTLE_MS = 3000;

  void request_state_(const LampState &requested);
  void publish_light_();
  void publish_brightness_();
  void publish_ultrasonic_();
  void publish_status_(const char *status);
  void save_mode_();
  void apply_received_(const halo2_protocol::ReceivedPacket &received);
  void publish_address_();
  void process_radio_();
  void send_commands_();
  void receive_packet_();
  void poll_status_();
  void settle_(uint32_t now);
  bool settling_(uint32_t now) const { return static_cast<int32_t>(settle_until_ - now) > 0; }
  void start_radio_();
  void radio_ready_();
  void recover_radio_();
  void cancel_requests_();
  bool send_batch_(std::span<const halo2_protocol::Frame> frames);
  halo2_protocol::Frame make_frame_(Command command, bool auto_brightness = false);

  Halo2Light *light_{nullptr};
  std::array<number::Number *, 2> brightness_numbers_{};
  std::array<uint8_t, 2> initial_brightness_{12, 91};
  select::Select *section_select_{nullptr};
  select::Select *ultrasonic_select_{nullptr};
  text_sensor::TextSensor *radio_status_{nullptr};
  text_sensor::TextSensor *radio_address_sensor_{nullptr};
  ESPPreferenceObject mode_preference_;
  halo2_protocol::Address radio_address_{};
  uint8_t radio_channel_{halo2_protocol::RADIO_CHANNEL};
  BridgeLifecycle lifecycle_;
  BM5602Radio radio_;
  uint8_t app_pid_{0};
  uint8_t last_pcf_{0};
  uint32_t recovery_delay_{RECOVERY_INITIAL_DELAY_MS};
  // Requested settings drive outgoing commands and the optimistic UI.
  // The lifecycle owns whether a received baseline has been established.
  LampState lamp_state_;
  CommandQueue commands_;
  uint32_t processing_interval_{50};
  uint32_t last_process_at_{0};
  uint32_t settle_until_{0};
  StatusPoll status_poll_;
  uint8_t saved_mode_{SavedMode::BOTH};
  uint8_t command_retries_{0};
  // Last fresh status reply (command 04 after refresh), for diagnostics.
  LampState last_status_;
  uint32_t last_status_at_{0}, status_count_{0}, status_timeouts_{0};

#ifdef USE_HALO2_SELF_TEST
  enum class SelfTestPhase : uint8_t { WAIT_ORIGIN, START_STEP, WAIT_RESULT, DONE };
  void self_test_loop_();
  SelfTestPhase self_test_phase_{SelfTestPhase::WAIT_ORIGIN};
  LampState self_test_origin_;
  std::array<LampState, 8> self_test_steps_{};
  std::array<const char *, 8> self_test_names_{};
  std::array<uint32_t, 8> self_test_holds_{};
  uint8_t self_test_steps_count_{0}, self_test_step_{0}, self_test_passed_{0}, self_test_round_{0},
      self_test_total_{0};
  uint32_t self_test_pause_{0};
  uint32_t self_test_at_{0}, self_test_tx_base_{0}, self_test_fail_base_{0}, self_test_retry_base_{0},
      self_test_status_base_{0}, self_test_timeout_base_{0};
#endif
};

class Halo2Light : public light::LightOutput, public Parented<Halo2> {
 public:
  using Parented<Halo2>::Parented;
  light::LightTraits get_traits() override;
  void setup_state(light::LightState *state) override {
    state_ = state;
    parent_->set_light(this);
  }
  void update_state(light::LightState *) override {
    // Capture local commands immediately, before polling can receive a packet.
    // The guard also prevents received state from becoming a new transmission.
    local_write_ = !reflecting_state_ && parent_->accepts_commands();
    if (local_write_) parent_->control_light();
  }
  void write_state(light::LightState *) override {
    // ESPHome installs the final transition values after update_state().
    // Reconcile those here, retaining the origin of the deferred write.
    if (local_write_) parent_->control_light();
    local_write_ = false;
  }

 protected:
  friend class Halo2;
  void restore_into(LampState &state) const;
  void read_into(LampState &state, bool use_target = false) const;
  void publish(const LampState &state);
  bool transitioning() const;

  // Freeze the original ratio across a fade; integer samples must not
  // become the basis of the following sample's proportional dimming.
  LampState dimming_basis_;
  light::LightState *state_{nullptr};
  bool local_write_{false};
  bool reflecting_state_{false};
};

class Halo2BrightnessNumber : public number::Number, public Parented<Halo2> {
 public:
  Halo2BrightnessNumber(Halo2 *parent, Section section) : Parented<Halo2>(parent), section_(section) {}

 protected:
  void control(float value) override { parent_->control_brightness(section_, value); }
  Section section_;
};

class Halo2UltrasonicSelect : public select::Select, public Parented<Halo2> {
 public:
  using Parented<Halo2>::Parented;

 protected:
  void control(size_t index) override { parent_->control_ultrasonic(index); }
};

class Halo2SectionSelect : public select::Select, public Parented<Halo2> {
 public:
  using Parented<Halo2>::Parented;

 protected:
  void control(size_t index) override { parent_->control_selection(index); }
};

class Halo2AutoButton : public button::Button, public Parented<Halo2> {
 public:
  using Parented<Halo2>::Parented;

 protected:
  void press_action() override { parent_->start_auto_brightness(); }
};

}  // namespace esphome::halo2
