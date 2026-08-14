#pragma once

#include <cstdint>
#include <cstddef>

#define RF24_PA_MAX 3
#define RF24_PA_HIGH 2
#define RF24_PA_LOW 1
#define RF24_PA_MIN 0
#define RF24_2MBPS 2
#define RF24_1MBPS 1
#define RF24_250KBPS 0
#define RF24_CRC_DISABLED 0
#define RF24_CRC_8 1
#define RF24_CRC_16 2

enum rf24_crclength_e : uint8_t {
  RF24_CRC_DISABLED_E = RF24_CRC_DISABLED,
  RF24_CRC_8_E = RF24_CRC_8,
  RF24_CRC_16_E = RF24_CRC_16,
};

enum rf24_datarate_e : uint8_t {
  RF24_250KBPS_E = RF24_250KBPS,
  RF24_1MBPS_E = RF24_1MBPS,
  RF24_2MBPS_E = RF24_2MBPS,
};

enum rf24_pa_dbm_e : uint8_t {
  RF24_PA_MIN_E = RF24_PA_MIN,
  RF24_PA_LOW_E = RF24_PA_LOW,
  RF24_PA_HIGH_E = RF24_PA_HIGH,
  RF24_PA_MAX_E = RF24_PA_MAX,
};

class RF24 {
 public:
  RF24(uint8_t cePin, uint8_t csnPin, uint32_t spiSpeed = 16000000UL);

  bool begin();
  bool isChipConnected();
  void powerUp();
  void powerDown();
  void startListening();
  void stopListening();
  bool available();
  bool available(uint8_t* pipeNum);
  void read(void* buf, uint8_t len);
  bool write(const void* buf, uint8_t len);
  bool writeFast(const void* buf, uint8_t len);
  bool writeBlocking(const void* buf, uint8_t len, uint32_t timeout);
  void startWrite(const void* buf, uint8_t len, bool multicast = false);
  bool txStandBy();
  bool txStandBy(uint32_t timeout, bool startTx = false);
  void whatHappened(bool& txOk, bool& txFail, bool& rxReady);

  void openWritingPipe(uint64_t address);
  void openReadingPipe(uint8_t child, uint64_t address);
  void closeReadingPipe(uint8_t pipe);
  void setAddressWidth(uint8_t width);
  void setPayloadSize(uint8_t size);
  uint8_t getPayloadSize() const { return payloadSize_; }
  uint8_t getDynamicPayloadSize();
  void enableDynamicPayloads();
  void disableDynamicPayloads();
  void enableAckPayload();
  void writeAckPayload(uint8_t pipe, const void* buf, uint8_t len);
  bool isAckPayloadAvailable();
  void setAutoAck(bool enable);
  void setAutoAck(uint8_t pipe, bool enable);
  void setRetries(uint8_t delay, uint8_t count);
  void setChannel(uint8_t channel);
  uint8_t getChannel();
  void setPALevel(uint8_t level, bool lnaEnable = true);
  uint8_t getPALevel();
  bool setDataRate(uint8_t speed);
  uint8_t getDataRate();
  void setCRCLength(uint8_t length);
  uint8_t getCRCLength();
  void disableCRC();
  void maskIRQ(bool txOk, bool txFail, bool rxReady);
  bool testCarrier();
  bool testRPD();
  void flush_rx();
  void flush_tx();
  uint8_t get_status();
  uint8_t failureDetected() const { return failureDetected_; }
  void startConstCarrier(uint8_t level, uint8_t channel);
  void stopConstCarrier();
  void printDetails();
  void printPrettyDetails();

 private:
  static constexpr uint8_t CONFIG = 0x00;
  static constexpr uint8_t EN_AA = 0x01;
  static constexpr uint8_t EN_RXADDR = 0x02;
  static constexpr uint8_t SETUP_AW = 0x03;
  static constexpr uint8_t SETUP_RETR = 0x04;
  static constexpr uint8_t RF_CH = 0x05;
  static constexpr uint8_t RF_SETUP = 0x06;
  static constexpr uint8_t STATUS = 0x07;
  static constexpr uint8_t OBSERVE_TX = 0x08;
  static constexpr uint8_t RPD = 0x09;
  static constexpr uint8_t RX_ADDR_P0 = 0x0A;
  static constexpr uint8_t RX_ADDR_P1 = 0x0B;
  static constexpr uint8_t RX_ADDR_P2 = 0x0C;
  static constexpr uint8_t TX_ADDR = 0x10;
  static constexpr uint8_t RX_PW_P0 = 0x11;
  static constexpr uint8_t FIFO_STATUS = 0x17;
  static constexpr uint8_t DYNPD = 0x1C;
  static constexpr uint8_t FEATURE = 0x1D;

  static constexpr uint8_t R_REGISTER = 0x00;
  static constexpr uint8_t W_REGISTER = 0x20;
  static constexpr uint8_t REGISTER_MASK = 0x1F;
  static constexpr uint8_t ACTIVATE = 0x50;
  static constexpr uint8_t R_RX_PL_WID = 0x60;
  static constexpr uint8_t R_RX_PAYLOAD = 0x61;
  static constexpr uint8_t W_TX_PAYLOAD = 0xA0;
  static constexpr uint8_t W_ACK_PAYLOAD = 0xA8;
  static constexpr uint8_t FLUSH_TX = 0xE1;
  static constexpr uint8_t FLUSH_RX = 0xE2;
  static constexpr uint8_t REUSE_TX_PL = 0xE3;
  static constexpr uint8_t NOP = 0xFF;

  uint8_t transfer(uint8_t value);
  void csn(bool high);
  void ce(bool high);
  uint8_t read_register(uint8_t reg);
  void read_register(uint8_t reg, uint8_t* buf, uint8_t len);
  uint8_t write_register(uint8_t reg, uint8_t value);
  uint8_t write_register(uint8_t reg, const uint8_t* buf, uint8_t len);
  uint8_t write_payload(const void* buf, uint8_t len, uint8_t command);
  uint8_t read_payload(void* buf, uint8_t len);
  void toggle_features();
  void writeAddress(uint8_t reg, uint64_t address);

  uint8_t cePin_;
  uint8_t csnPin_;
  uint32_t spiSpeed_;
  uint8_t payloadSize_ = 32;
  uint8_t addrWidth_ = 5;
  bool dynamicPayloads_ = false;
  bool ackPayloadAvailable_ = false;
  bool pVariant_ = false;
  bool failureDetected_ = false;
};
