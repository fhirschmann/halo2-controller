#pragma once

#include <array>
#include <cstdio>
#include <cstring>
#include <span>
#include "esphome/components/spi/spi.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "halo2_protocol.h"

namespace esphome::halo2 {

// Holtek BC5602 on a Best Modules BM5602-60-1, driven through its packet
// engine (auto-ACK with payload, hardware PID/CRC), as in kuzmin-no's
// MicroPython bridge. On the tested lamp this transport applied 30/30
// settings, while clock-synchronous direct TX (pin 8/TBCLK) applied none.
//
// Every request is a blocking exchange of a few milliseconds: TX with up to
// 3x(1+2) transmissions, then the lamp's ACK payload is read from the RX
// FIFO. Between exchanges the chip listens passively (no ACKs, no CRC check
// in hardware) for the original controller; those frames are CRC-checked in
// software.
class BM5602Radio : public spi::SPIDevice<spi::BIT_ORDER_MSB_FIRST, spi::CLOCK_POLARITY_LOW, spi::CLOCK_PHASE_LEADING,
                                          spi::DATA_RATE_1MHZ> {
 public:
  static constexpr size_t MAX_RAW_SIZE = 16;
  using RawFrame = std::array<uint8_t, MAX_RAW_SIZE>;

  void dump_config() {
    static const char *const TAG = "halo2";
    LOG_SPI_DEVICE(this);
    ESP_LOGCONFIG(TAG, "  BM5602 version: %02X %02X %02X", version_[0], version_[1], version_[2]);
  }

  bool ready() const { return ready_; }
  bool idle() const { return ready_; }
  const char *last_error() const { return error_[0] ? error_ : nullptr; }
  const halo2_protocol::Address &address() const { return address_; }
  uint8_t channel() const { return channel_; }
  uint32_t rx_count() const { return rx_count_; }
  uint32_t last_irq() const { return last_irq_; }
  const RawFrame &rx_frame() const { return rx_frame_; }
  size_t rx_length() const { return rx_length_; }
  uint32_t tx_count() const { return tx_count_; }
  uint32_t tx_failures() const { return tx_failures_; }
  uint32_t tx_retries() const { return tx_retries_; }
  uint32_t chip_resets() const { return chip_resets_; }
  // Last ACK payload and STATUS register seen after TX_DS, for diagnostics.
  const std::array<uint8_t, halo2_protocol::PAYLOAD_SIZE> &last_reply() const { return last_reply_; }
  uint8_t last_status() const { return last_status_; }

  bool take_tx_done() {
    const bool completed = tx_completed_;
    tx_completed_ = false;
    return completed;
  }

  bool setup(const halo2_protocol::Address &address, uint8_t channel) {
    ready_ = false;
    error_[0] = 0;
    address_ = address;
    channel_ = channel;
    replies_count_ = 0;
    // Register the SPI device once; a retry after a failed probe only repeats the chip init.
    if (!spi_registered_) {
      spi_setup();
      spi_registered_ = true;
    }
    // An ESP32 reset does not power-cycle the module: start from a chip reset.
    if (!init_chip_()) {
      std::snprintf(error_, sizeof(error_), "BM5602 not found (version %02X %02X %02X)", version_[0], version_[1],
                    version_[2]);
      return false;
    }
    listen_();
    ready_ = true;
    return true;
  }

  // Sends each frame's payload with auto-ACK. A frame counts as delivered when
  // the lamp acknowledged it; its ACK payload becomes a reply for poll().
  bool send_batch(std::span<const halo2_protocol::Frame> frames) {
    if (!ready_) return false;
    configure_ptx_();
    for (const auto &frame : frames) {
      const auto payload = bit_cast<std::array<uint8_t, halo2_protocol::PAYLOAD_SIZE>>(frame.payload);
      std::array<uint8_t, halo2_protocol::PAYLOAD_SIZE> reply{};
      ++tx_count_;
      if (!exchange_(payload, reply)) {
        ++tx_failures_;
        continue;
      }
      halo2_protocol::ReceivedPacket packet;
      if (halo2_protocol::decode_reply(reply.data(), frame.pcf, packet) && replies_count_ < replies_.size())
        replies_[replies_count_++] = packet;
    }
    listen_();
    tx_completed_ = true;
    return true;
  }

  // Returns queued ACK replies first, then passively received controller frames.
  bool poll(halo2_protocol::ReceivedPacket &packet) {
    if (replies_count_ > 0) {
      packet = replies_[0];
      std::move(replies_.begin() + 1, replies_.begin() + replies_count_, replies_.begin());
      --replies_count_;
      return true;
    }
    if (!ready_ || (read_reg_(REG_STATUS) & STATUS_RX_EMPTY)) return false;
    const uint8_t length = read_reg_(REG_PKT4);
    if (length == 0 || length > MAX_RAW_SIZE) {
      listen_();
      return false;
    }
    rx_frame_.fill(0);
    read_bytes_(CMD_READ_RX_FIFO, rx_frame_.data(), length);
    rx_length_ = length;
    ++rx_count_;
    command_(CMD_LIGHT_SLEEP);
    write_reg_(REG_IRQ1, IRQ1_RX_DR);
    command_(CMD_FLUSH_RX);
    command_(CMD_RX_MODE);
    // Without the packet engine the 9-bit PCF arrives as payload bits. Accept
    // the bit-shifted air layout and, as a fallback, a byte-aligned frame.
    if (length >= halo2_protocol::AIR_FRAME_SIZE &&
        halo2_protocol::decode_air_frame(rx_frame_.data(), halo2_protocol::AIR_FRAME_SIZE, packet, address_))
      return true;
    return length >= halo2_protocol::FRAME_SIZE &&
           halo2_protocol::decode_frame(rx_frame_.data(), halo2_protocol::FRAME_SIZE, packet, address_);
  }

 protected:
  static constexpr std::array<uint8_t, 3> EXPECTED_VERSION{0x56, 0x02, 0x01};
  static constexpr uint8_t REG_CFG1 = 0x00, REG_RC1 = 0x01, REG_MASK = 0x03, REG_IRQ1 = 0x04, REG_STATUS = 0x05,
                           REG_IO1 = 0x06, REG_PKT1 = 0x09, REG_PKT4 = 0x0C, REG_RFCH = 0x10, REG_DM1 = 0x11,
                           REG_RT1 = 0x13, REG_DPL1 = 0x2A, REG_DPL2 = 0x2B, REG_RXPW0 = 0x2C, REG_ENAA = 0x32;
  static constexpr uint8_t CMD_WRITE_REG = 0x40, CMD_READ_REG = 0xC0, CMD_READ_VERSION = 0x9F,
                           CMD_WRITE_ADDRESS = 0x10, CMD_WRITE_TX_ACK = 0x11, CMD_FLUSH_TX = 0x09,
                           CMD_FLUSH_RX = 0x89, CMD_READ_RX_FIFO = 0xBF, CMD_LIGHT_SLEEP = 0x0C, CMD_TX_MODE = 0x0E,
                           CMD_RX_MODE = 0x8E;
  static constexpr uint8_t IO1_GIO2_SDO = 0x48, DM1_125K_ADDR4 = 0x82, STATUS_RX_EMPTY = 0x01,
                           STATUS_TX_EMPTY = 0x10, STATUS_TX_FULL = 0x20, MASK_PRX = 0x01, CMD_SOFTWARE_RESET = 0x08;
  // IRQ1 flags are write-one-to-clear and only while the chip sleeps (tested on
  // hardware). kuzmin-no writes zero in TX mode, which leaves flags set.
  static constexpr uint8_t IRQ1_RX_DR = 0x40, IRQ1_TX_DS = 0x20, IRQ1_MAX_RT = 0x10,
                           IRQ1_CLEAR = IRQ1_RX_DR | IRQ1_TX_DS | IRQ1_MAX_RT;
  // 2 ms auto-retransmit delay with 2 hardware retransmissions per attempt.
  static constexpr uint8_t RT1_2MS_2X = 0x72, TX_ATTEMPTS = 3;
  static constexpr uint32_t TX_TIMEOUT_US = 15000;

  void command_(uint8_t cmd) {
    enable();
    write_byte(cmd);
    disable();
  }
  void write_reg_(uint8_t reg, uint8_t value) {
    enable();
    write_byte(CMD_WRITE_REG | reg);
    write_byte(value);
    disable();
  }
  uint8_t read_reg_(uint8_t reg) {
    enable();
    write_byte(CMD_READ_REG | reg);
    const uint8_t value = read_byte();
    disable();
    return value;
  }
  void write_bytes_(uint8_t cmd, const uint8_t *data, size_t size) {
    enable();
    write_byte(cmd);
    write_array(data, size);
    disable();
  }
  void read_bytes_(uint8_t cmd, uint8_t *data, size_t size) {
    enable();
    write_byte(cmd);
    read_array(data, size);
    disable();
  }
  void set_bank_(uint8_t bank) {
    write_reg_(REG_CFG1, static_cast<uint8_t>((read_reg_(REG_CFG1) & 0xFCU) | (bank & 0x03U)));
  }
  void common_config_() {
    command_(CMD_LIGHT_SLEEP);
    write_reg_(REG_IO1, IO1_GIO2_SDO);
    set_bank_(0);
    write_reg_(REG_RFCH, channel_);
    write_reg_(REG_DM1, DM1_125K_ADDR4);
    write_bytes_(CMD_WRITE_ADDRESS, address_.data(), address_.size());
  }

  // Passive listening for the original controller: PRX without auto-ACK and
  // without hardware CRC, fixed 14-byte payload (9-bit PCF + 10 + CRC).
  void listen_() {
    common_config_();
    write_reg_(REG_MASK, static_cast<uint8_t>(read_reg_(REG_MASK) | MASK_PRX));
    write_reg_(REG_DPL1, 0x00);
    write_reg_(REG_DPL2, 0x00);
    write_reg_(REG_RXPW0, halo2_protocol::AIR_FRAME_SIZE);
    write_reg_(REG_PKT1, 0x00);
    write_reg_(REG_ENAA, 0x00);
    write_reg_(REG_IRQ1, IRQ1_CLEAR);
    command_(CMD_FLUSH_RX);
    command_(CMD_RX_MODE);
  }

  // PTX with dynamic payload, hardware CRC and auto-ACK, as in kuzmin-no.
  void configure_ptx_() {
    common_config_();
    // PRX off; unmask TX_DS and MAX_RT.
    write_reg_(REG_MASK, static_cast<uint8_t>(read_reg_(REG_MASK) & 0xCEU));
    write_reg_(REG_IRQ1, IRQ1_CLEAR);
    write_reg_(REG_DPL1, 0x01);
    write_reg_(REG_DPL2, 0x04);
    write_reg_(REG_PKT1, 0x20);
    write_reg_(REG_ENAA, 0x3F);
    write_reg_(REG_RT1, RT1_2MS_2X);
  }

  // Software reset plus the reference initialization. Registers return to
  // their defaults, so callers configure PTX or passive RX afterwards.
  bool init_chip_() {
    // The first transaction is write-only: GIO2 becomes SDO only afterwards.
    write_reg_(REG_IO1, IO1_GIO2_SDO);
    command_(CMD_SOFTWARE_RESET);
    delay(20);
    write_reg_(REG_IO1, IO1_GIO2_SDO);
    command_(CMD_LIGHT_SLEEP);
    delay(1);
    read_bytes_(CMD_READ_VERSION, version_.data(), version_.size());
    if (version_ != EXPECTED_VERSION) return false;
    set_bank_(0);
    // Clear the power-on flag the same way as the reference initialization.
    const uint8_t rc1 = read_reg_(REG_RC1);
    if (rc1 & 0x80U) write_reg_(REG_RC1, static_cast<uint8_t>(rc1 & 0x7FU));
    return true;
  }

  // After MAX_RT the chip keeps the packet and the flag; neither FLUSH_TX nor
  // clearing IRQ1 removes them reliably. Only a software reset recovers.
  void reset_chip_() {
    ++chip_resets_;
    if (!init_chip_())
      std::snprintf(error_, sizeof(error_), "BM5602 lost after reset (version %02X %02X %02X)", version_[0],
                    version_[1], version_[2]);
    configure_ptx_();
  }

  // IRQ1 can only be cleared (and the FIFOs flushed) while the chip sleeps;
  // in TX mode those writes have no effect and a stale TX_DS would make an
  // unsent request look acknowledged.
  bool exchange_(const std::array<uint8_t, halo2_protocol::PAYLOAD_SIZE> &payload,
                 std::array<uint8_t, halo2_protocol::PAYLOAD_SIZE> &reply) {
    for (uint8_t attempt = 0; attempt < TX_ATTEMPTS; ++attempt) {
      if (attempt > 0) ++tx_retries_;
      command_(CMD_LIGHT_SLEEP);
      write_reg_(REG_IRQ1, IRQ1_CLEAR);
      command_(CMD_FLUSH_TX);
      command_(CMD_FLUSH_RX);
      if ((read_reg_(REG_IRQ1) & IRQ1_CLEAR) || !(read_reg_(REG_STATUS) & STATUS_TX_EMPTY)) reset_chip_();
      write_bytes_(CMD_WRITE_TX_ACK, payload.data(), payload.size());
      command_(CMD_TX_MODE);
      const uint32_t started = micros();
      uint8_t irq = 0;
      do {
        irq = read_reg_(REG_IRQ1);
      } while (!(irq & (IRQ1_TX_DS | IRQ1_MAX_RT)) && micros() - started < TX_TIMEOUT_US);
      last_irq_ = irq;
      command_(CMD_LIGHT_SLEEP);
      if (irq & IRQ1_TX_DS) {
        // The ACK payload does not show up in STATUS; read it unconditionally
        // like the reference bridge. An empty ACK yields bytes the decoder rejects.
        last_status_ = read_reg_(REG_STATUS);
        read_bytes_(CMD_READ_RX_FIFO, reply.data(), reply.size());
        last_reply_ = reply;
        write_reg_(REG_IRQ1, IRQ1_CLEAR);
        command_(CMD_FLUSH_RX);
        command_(CMD_FLUSH_TX);
        return true;
      }
      reset_chip_();
    }
    return false;
  }

  halo2_protocol::Address address_{};
  uint8_t channel_{halo2_protocol::RADIO_CHANNEL};
  std::array<uint8_t, 3> version_{};
  std::array<halo2_protocol::ReceivedPacket, 4> replies_{};
  size_t replies_count_{0};
  RawFrame rx_frame_{};
  size_t rx_length_{0};
  uint32_t rx_count_{0}, tx_count_{0}, tx_failures_{0}, tx_retries_{0}, chip_resets_{0};
  uint32_t last_irq_{0};
  std::array<uint8_t, halo2_protocol::PAYLOAD_SIZE> last_reply_{};
  uint8_t last_status_{0};
  bool ready_{false}, tx_completed_{false}, spi_registered_{false};
  char error_[64]{};
};

}  // namespace esphome::halo2
