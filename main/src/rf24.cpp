#include "rf24.h"

#include "div_platform.h"
#include "shared.h"

#include <algorithm>
#include <cstring>

#ifndef NRF24_SCK
#define NRF24_SCK SD_SCLK
#endif
#ifndef NRF24_MISO
#define NRF24_MISO SD_MISO
#endif
#ifndef NRF24_MOSI
#define NRF24_MOSI SD_MOSI
#endif

RF24::RF24(uint8_t cePin, uint8_t csnPin, uint32_t spiSpeed)
    : cePin_(cePin), csnPin_(csnPin), spiSpeed_(spiSpeed) {}

uint8_t RF24::transfer(uint8_t value) {
  uint8_t out = 0;
  const uint32_t halfPeriodUs = std::max<uint32_t>(1, 500000UL / std::max<uint32_t>(spiSpeed_, 1));
  for (int bit = 7; bit >= 0; --bit) {
    digitalWrite(NRF24_MOSI, (value >> bit) & 1);
    delayMicroseconds(halfPeriodUs);
    digitalWrite(NRF24_SCK, HIGH);
    delayMicroseconds(halfPeriodUs);
    if (digitalRead(NRF24_MISO)) out |= (1u << bit);
    digitalWrite(NRF24_SCK, LOW);
  }
  return out;
}

void RF24::csn(bool high) { digitalWrite(csnPin_, high ? HIGH : LOW); }
void RF24::ce(bool high) { digitalWrite(cePin_, high ? HIGH : LOW); }

bool RF24::begin() {
  pinMode(NRF24_SCK, OUTPUT);
  pinMode(NRF24_MOSI, OUTPUT);
  pinMode(NRF24_MISO, INPUT);
  pinMode(cePin_, OUTPUT);
  pinMode(csnPin_, OUTPUT);
  ce(false);
  csn(true);
  digitalWrite(NRF24_SCK, LOW);
  delay(5);

  write_register(CONFIG, 0x0C);
  delay(5);
  setRetries(5, 15);
  setDataRate(RF24_1MBPS);
  setPALevel(RF24_PA_MAX);
  setCRCLength(RF24_CRC_16);
  setChannel(76);
  setPayloadSize(32);
  write_register(EN_AA, 0x3F);
  write_register(EN_RXADDR, 0x03);
  setAddressWidth(5);
  flush_rx();
  flush_tx();
  write_register(STATUS, 0x70);
  pVariant_ = true;
  return isChipConnected();
}

bool RF24::isChipConnected() {
  const uint8_t aw = read_register(SETUP_AW);
  return aw >= 1 && aw <= 3;
}

uint8_t RF24::read_register(uint8_t reg) {
  csn(false);
  transfer(R_REGISTER | (REGISTER_MASK & reg));
  const uint8_t result = transfer(0xFF);
  csn(true);
  return result;
}

void RF24::read_register(uint8_t reg, uint8_t* buf, uint8_t len) {
  csn(false);
  transfer(R_REGISTER | (REGISTER_MASK & reg));
  for (uint8_t i = 0; i < len; ++i) buf[i] = transfer(0xFF);
  csn(true);
}

uint8_t RF24::write_register(uint8_t reg, uint8_t value) {
  csn(false);
  const uint8_t status = transfer(W_REGISTER | (REGISTER_MASK & reg));
  transfer(value);
  csn(true);
  return status;
}

uint8_t RF24::write_register(uint8_t reg, const uint8_t* buf, uint8_t len) {
  csn(false);
  const uint8_t status = transfer(W_REGISTER | (REGISTER_MASK & reg));
  for (uint8_t i = 0; i < len; ++i) transfer(buf[i]);
  csn(true);
  return status;
}

uint8_t RF24::get_status() {
  csn(false);
  const uint8_t status = transfer(NOP);
  csn(true);
  return status;
}

void RF24::powerUp() {
  uint8_t cfg = read_register(CONFIG);
  if (!(cfg & 0x02)) {
    write_register(CONFIG, cfg | 0x02);
    delay(5);
  }
}

void RF24::powerDown() {
  ce(false);
  write_register(CONFIG, read_register(CONFIG) & ~0x02);
}

void RF24::startListening() {
  powerUp();
  write_register(CONFIG, read_register(CONFIG) | 0x01);
  write_register(STATUS, 0x70);
  ce(true);
  delayMicroseconds(150);
}

void RF24::stopListening() {
  ce(false);
  delayMicroseconds(150);
  if (read_register(FEATURE) & 0x02) delayMicroseconds(150);
  write_register(CONFIG, read_register(CONFIG) & ~0x01);
}

bool RF24::available() {
  uint8_t pipe = 0;
  return available(&pipe);
}

bool RF24::available(uint8_t* pipeNum) {
  const uint8_t status = get_status();
  const bool result = !(read_register(FIFO_STATUS) & 0x01);
  if (result && pipeNum) *pipeNum = (status >> 1) & 0x07;
  if (status & 0x40) write_register(STATUS, 0x40);
  return result;
}

uint8_t RF24::read_payload(void* buf, uint8_t len) {
  uint8_t* current = static_cast<uint8_t*>(buf);
  const uint8_t dataLen = std::min<uint8_t>(len, payloadSize_);
  csn(false);
  const uint8_t status = transfer(R_RX_PAYLOAD);
  for (uint8_t i = 0; i < dataLen; ++i) current[i] = transfer(0xFF);
  for (uint8_t i = dataLen; i < payloadSize_; ++i) transfer(0xFF);
  csn(true);
  return status;
}

void RF24::read(void* buf, uint8_t len) {
  read_payload(buf, dynamicPayloads_ ? std::min<uint8_t>(len, getDynamicPayloadSize()) : len);
  write_register(STATUS, 0x40);
}

uint8_t RF24::write_payload(const void* buf, uint8_t len, uint8_t command) {
  const uint8_t* current = static_cast<const uint8_t*>(buf);
  const uint8_t dataLen = std::min<uint8_t>(len, payloadSize_);
  csn(false);
  const uint8_t status = transfer(command);
  for (uint8_t i = 0; i < dataLen; ++i) transfer(current[i]);
  for (uint8_t i = dataLen; i < payloadSize_; ++i) transfer(0);
  csn(true);
  return status;
}

bool RF24::write(const void* buf, uint8_t len) {
  startWrite(buf, len, false);
  return txStandBy();
}

bool RF24::writeFast(const void* buf, uint8_t len) {
  write_payload(buf, len, W_TX_PAYLOAD);
  ce(true);
  return !(get_status() & 0x20);
}

bool RF24::writeBlocking(const void* buf, uint8_t len, uint32_t timeout) {
  const uint32_t start = millis();
  while ((read_register(FIFO_STATUS) & 0x20) && millis() - start < timeout) {
    if (get_status() & 0x20) {
      write_register(STATUS, 0x20);
      flush_tx();
    }
    delay(1);
  }
  if (millis() - start >= timeout) return false;
  return writeFast(buf, len);
}

void RF24::startWrite(const void* buf, uint8_t len, bool multicast) {
  write_register(CONFIG, (read_register(CONFIG) | 0x02) & ~0x01);
  write_payload(buf, len, multicast ? 0xB0 : W_TX_PAYLOAD);
  ce(true);
  delayMicroseconds(15);
  ce(false);
}

bool RF24::txStandBy() {
  const uint32_t start = millis();
  while (!(get_status() & (0x20 | 0x10))) {
    if (millis() - start > 95) {
      failureDetected_ = true;
      return false;
    }
    delay(1);
  }
  const uint8_t status = get_status();
  write_register(STATUS, 0x30);
  if (status & 0x10) {
    flush_tx();
    return false;
  }
  return true;
}

bool RF24::txStandBy(uint32_t timeout, bool startTx) {
  if (startTx) ce(true);
  const uint32_t start = millis();
  while (!(read_register(FIFO_STATUS) & 0x10)) {
    if (get_status() & 0x10) {
      write_register(STATUS, 0x10);
      ce(false);
      flush_tx();
      return false;
    }
    if (millis() - start >= timeout) {
      ce(false);
      return false;
    }
    delay(1);
  }
  ce(false);
  return true;
}

void RF24::whatHappened(bool& txOk, bool& txFail, bool& rxReady) {
  const uint8_t status = get_status();
  txOk = status & 0x20;
  txFail = status & 0x10;
  rxReady = status & 0x40;
  write_register(STATUS, 0x70);
}

void RF24::writeAddress(uint8_t reg, uint64_t address) {
  uint8_t buf[5];
  for (uint8_t i = 0; i < addrWidth_; ++i) {
    buf[i] = static_cast<uint8_t>(address >> (8 * i));
  }
  write_register(reg, buf, addrWidth_);
}

void RF24::openWritingPipe(uint64_t address) {
  writeAddress(RX_ADDR_P0, address);
  writeAddress(TX_ADDR, address);
  write_register(RX_PW_P0, payloadSize_);
}

void RF24::openReadingPipe(uint8_t child, uint64_t address) {
  if (child > 5) return;
  if (child < 2) {
    writeAddress(RX_ADDR_P0 + child, address);
  } else {
    write_register(RX_ADDR_P0 + child, static_cast<uint8_t>(address));
  }
  write_register(RX_PW_P0 + child, payloadSize_);
  write_register(EN_RXADDR, read_register(EN_RXADDR) | (1 << child));
}

void RF24::closeReadingPipe(uint8_t pipe) {
  if (pipe <= 5) write_register(EN_RXADDR, read_register(EN_RXADDR) & ~(1 << pipe));
}

void RF24::setAddressWidth(uint8_t width) {
  addrWidth_ = std::max<uint8_t>(3, std::min<uint8_t>(5, width));
  write_register(SETUP_AW, addrWidth_ - 2);
}

void RF24::setPayloadSize(uint8_t size) {
  payloadSize_ = std::max<uint8_t>(1, std::min<uint8_t>(32, size));
}

uint8_t RF24::getDynamicPayloadSize() {
  csn(false);
  transfer(R_RX_PL_WID);
  const uint8_t result = transfer(0xFF);
  csn(true);
  if (result > 32) {
    flush_rx();
    return 0;
  }
  return result;
}

void RF24::toggle_features() {
  csn(false);
  transfer(ACTIVATE);
  transfer(0x73);
  csn(true);
}

void RF24::enableDynamicPayloads() {
  uint8_t feature = read_register(FEATURE);
  if (!feature) {
    toggle_features();
    feature = read_register(FEATURE);
  }
  write_register(FEATURE, feature | 0x04);
  write_register(DYNPD, 0x3F);
  dynamicPayloads_ = true;
}

void RF24::disableDynamicPayloads() {
  write_register(FEATURE, read_register(FEATURE) & ~0x04);
  write_register(DYNPD, 0);
  dynamicPayloads_ = false;
}

void RF24::enableAckPayload() {
  uint8_t feature = read_register(FEATURE);
  if (!feature) {
    toggle_features();
    feature = read_register(FEATURE);
  }
  write_register(FEATURE, feature | 0x06);
  write_register(DYNPD, read_register(DYNPD) | 0x03);
}

void RF24::writeAckPayload(uint8_t pipe, const void* buf, uint8_t len) {
  if (pipe <= 5) write_payload(buf, std::min<uint8_t>(len, 32), W_ACK_PAYLOAD | pipe);
}

bool RF24::isAckPayloadAvailable() {
  const bool result = ackPayloadAvailable_;
  ackPayloadAvailable_ = false;
  return result;
}

void RF24::setAutoAck(bool enable) { write_register(EN_AA, enable ? 0x3F : 0); }

void RF24::setAutoAck(uint8_t pipe, bool enable) {
  if (pipe > 5) return;
  uint8_t en = read_register(EN_AA);
  en = enable ? (en | (1 << pipe)) : (en & ~(1 << pipe));
  write_register(EN_AA, en);
}

void RF24::setRetries(uint8_t delay, uint8_t count) {
  write_register(SETUP_RETR, ((delay & 0x0F) << 4) | (count & 0x0F));
}

void RF24::setChannel(uint8_t channel) { write_register(RF_CH, std::min<uint8_t>(channel, 125)); }
uint8_t RF24::getChannel() { return read_register(RF_CH); }

void RF24::setPALevel(uint8_t level, bool) {
  uint8_t setup = read_register(RF_SETUP) & ~0x06;
  setup |= (std::min<uint8_t>(level, 3) << 1);
  write_register(RF_SETUP, setup);
}

uint8_t RF24::getPALevel() { return (read_register(RF_SETUP) & 0x06) >> 1; }

bool RF24::setDataRate(uint8_t speed) {
  uint8_t setup = read_register(RF_SETUP) & ~0x28;
  if (speed == RF24_250KBPS) {
    setup |= 0x20;
  } else if (speed == RF24_2MBPS) {
    setup |= 0x08;
  }
  write_register(RF_SETUP, setup);
  return read_register(RF_SETUP) == setup;
}

uint8_t RF24::getDataRate() {
  const uint8_t setup = read_register(RF_SETUP);
  if (setup & 0x20) return RF24_250KBPS;
  if (setup & 0x08) return RF24_2MBPS;
  return RF24_1MBPS;
}

void RF24::setCRCLength(uint8_t length) {
  uint8_t cfg = read_register(CONFIG) & ~0x0C;
  if (length == RF24_CRC_DISABLED) {
    cfg &= ~0x08;
  } else if (length == RF24_CRC_8) {
    cfg |= 0x08;
  } else {
    cfg |= 0x0C;
  }
  write_register(CONFIG, cfg);
}

uint8_t RF24::getCRCLength() {
  const uint8_t cfg = read_register(CONFIG) & 0x0C;
  if (!(cfg & 0x08)) return RF24_CRC_DISABLED;
  return (cfg & 0x04) ? RF24_CRC_16 : RF24_CRC_8;
}

void RF24::disableCRC() { setCRCLength(RF24_CRC_DISABLED); }

void RF24::maskIRQ(bool txOk, bool txFail, bool rxReady) {
  uint8_t cfg = read_register(CONFIG) & ~0x70;
  if (rxReady) cfg |= 0x40;
  if (txOk) cfg |= 0x20;
  if (txFail) cfg |= 0x10;
  write_register(CONFIG, cfg);
}

bool RF24::testCarrier() { return read_register(0x09) & 0x01; }
bool RF24::testRPD() { return read_register(RPD) & 0x01; }

void RF24::flush_rx() {
  csn(false);
  transfer(FLUSH_RX);
  csn(true);
}

void RF24::flush_tx() {
  csn(false);
  transfer(FLUSH_TX);
  csn(true);
}

void RF24::startConstCarrier(uint8_t level, uint8_t channel) {
  stopListening();
  setAutoAck(false);
  setRetries(0, 0);
  setPALevel(level);
  setChannel(channel);
  write_register(RF_SETUP, read_register(RF_SETUP) | 0x90);
  uint8_t ff = 0xFF;
  write_payload(&ff, 1, W_TX_PAYLOAD);
  ce(true);
}

void RF24::stopConstCarrier() {
  ce(false);
  write_register(RF_SETUP, read_register(RF_SETUP) & ~0x90);
  flush_tx();
}

void RF24::printDetails() {}
void RF24::printPrettyDetails() {}
