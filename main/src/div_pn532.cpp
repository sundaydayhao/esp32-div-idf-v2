#include "div_pn532.h"

#include "div_platform.h"

#include <algorithm>
#include <cstring>

namespace {
constexpr uint8_t PN532_PREAMBLE = 0x00;
constexpr uint8_t PN532_STARTCODE1 = 0x00;
constexpr uint8_t PN532_STARTCODE2 = 0xFF;
constexpr uint8_t PN532_POSTAMBLE = 0x00;
constexpr uint8_t PN532_HOSTTOPN532 = 0xD4;
constexpr uint8_t PN532_PN532TOHOST = 0xD5;

constexpr uint8_t PN532_SPI_DATAWRITE = 0x01;
constexpr uint8_t PN532_SPI_STATUSREAD = 0x02;
constexpr uint8_t PN532_SPI_DATAREAD = 0x03;
constexpr uint8_t PN532_SPI_READY = 0x01;

constexpr uint8_t PN532_COMMAND_DIAGNOSE = 0x00;
constexpr uint8_t PN532_COMMAND_GETFIRMWAREVERSION = 0x02;
constexpr uint8_t PN532_COMMAND_SAMCONFIGURATION = 0x14;
constexpr uint8_t PN532_COMMAND_RFCONFIGURATION = 0x32;
constexpr uint8_t PN532_COMMAND_INLISTPASSIVETARGET = 0x4A;
constexpr uint8_t PN532_COMMAND_INDATAEXCHANGE = 0x40;
constexpr uint8_t PN532_COMMAND_TGINITASTARGET = 0x8C;
constexpr uint8_t PN532_COMMAND_TGGETDATA = 0x86;
constexpr uint8_t PN532_COMMAND_TGSETDATA = 0x8E;

constexpr uint8_t PN532_MIFARE_READ = 0x30;
constexpr uint8_t PN532_MIFARE_WRITE = 0xA0;
constexpr uint8_t PN532_MIFARE_UL_WRITE = 0xA2;

constexpr uint8_t ackFrame[] = {0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00};
}

DivPn532::DivPn532(int8_t sck, int8_t miso, int8_t mosi, int8_t ss)
    : sck_(sck), miso_(miso), mosi_(mosi), ss_(ss) {}

void DivPn532::begin() {
  pinMode(sck_, OUTPUT);
  pinMode(mosi_, OUTPUT);
  pinMode(miso_, INPUT);
  pinMode(ss_, OUTPUT);
  cs(true);
  digitalWrite(sck_, LOW);
  digitalWrite(mosi_, LOW);
  delay(10);
}

void DivPn532::cs(bool high) { digitalWrite(ss_, high ? HIGH : LOW); }

uint8_t DivPn532::spiTransfer(uint8_t value) {
  uint8_t out = 0;
  for (int bit = 7; bit >= 0; --bit) {
    digitalWrite(mosi_, (value >> bit) & 1);
    delayMicroseconds(2);
    digitalWrite(sck_, HIGH);
    delayMicroseconds(2);
    if (digitalRead(miso_)) out |= (1u << bit);
    digitalWrite(sck_, LOW);
  }
  return out;
}

void DivPn532::spiWrite(uint8_t value) { spiTransfer(value); }
uint8_t DivPn532::spiRead() { return spiTransfer(0x00); }

bool DivPn532::isReady() {
  cs(false);
  spiWrite(PN532_SPI_STATUSREAD);
  const bool ready = spiRead() == PN532_SPI_READY;
  cs(true);
  return ready;
}

bool DivPn532::waitReady(uint16_t timeout) {
  const uint32_t start = millis();
  while (!isReady()) {
    if (timeout && (millis() - start) > timeout) return false;
    delay(5);
  }
  return true;
}

void DivPn532::writeFrame(const uint8_t* data, uint8_t len) {
  uint8_t checksum = PN532_HOSTTOPN532;
  cs(false);
  spiWrite(PN532_SPI_DATAWRITE);
  spiWrite(PN532_PREAMBLE);
  spiWrite(PN532_STARTCODE1);
  spiWrite(PN532_STARTCODE2);
  spiWrite(len + 1);
  spiWrite(static_cast<uint8_t>(~(len + 1) + 1));
  spiWrite(PN532_HOSTTOPN532);
  for (uint8_t i = 0; i < len; ++i) {
    spiWrite(data[i]);
    checksum += data[i];
  }
  spiWrite(static_cast<uint8_t>(~checksum + 1));
  spiWrite(PN532_POSTAMBLE);
  cs(true);
}

bool DivPn532::sendCommandCheckAck(const uint8_t* cmd, uint8_t cmdlen, uint16_t timeout) {
  writeFrame(cmd, cmdlen);
  if (!waitReady(timeout)) return false;
  uint8_t ack[sizeof(ackFrame)]{};
  cs(false);
  spiWrite(PN532_SPI_DATAREAD);
  for (uint8_t& b : ack) b = spiRead();
  cs(true);
  return std::memcmp(ack, ackFrame, sizeof(ackFrame)) == 0;
}

bool DivPn532::readResponse(uint8_t* response, uint8_t* responseLength, uint16_t timeout) {
  if (!waitReady(timeout)) return false;
  cs(false);
  spiWrite(PN532_SPI_DATAREAD);
  if (spiRead() != PN532_PREAMBLE || spiRead() != PN532_STARTCODE1 || spiRead() != PN532_STARTCODE2) {
    cs(true);
    return false;
  }
  uint8_t len = spiRead();
  uint8_t lcs = spiRead();
  if (static_cast<uint8_t>(len + lcs) != 0) {
    cs(true);
    return false;
  }
  if (len < 2 || spiRead() != PN532_PN532TOHOST) {
    cs(true);
    return false;
  }
  uint8_t checksum = PN532_PN532TOHOST;
  len--;
  const uint8_t copyLen = std::min<uint8_t>(len, responseLength ? *responseLength : len);
  for (uint8_t i = 0; i < len; ++i) {
    const uint8_t b = spiRead();
    checksum += b;
    if (i < copyLen && response) response[i] = b;
  }
  const uint8_t dcs = spiRead();
  (void)spiRead();
  cs(true);
  if (static_cast<uint8_t>(checksum + dcs) != 0) return false;
  if (responseLength) *responseLength = copyLen;
  return true;
}

bool DivPn532::commandResponse(uint8_t command, const uint8_t* cmd, uint8_t cmdlen,
                                     uint8_t* response, uint8_t* responseLength, uint16_t timeout) {
  packetBuffer_[0] = command;
  if (cmdlen && cmd) std::memcpy(packetBuffer_ + 1, cmd, cmdlen);
  if (!sendCommandCheckAck(packetBuffer_, cmdlen + 1, timeout)) return false;
  uint8_t maxLen = responseLength ? *responseLength : 0;
  if (!readResponse(packetBuffer_, &maxLen, timeout)) return false;
  if (maxLen < 1 || packetBuffer_[0] != static_cast<uint8_t>(command + 1)) return false;
  const uint8_t payloadLen = maxLen - 1;
  const uint8_t copyLen = responseLength ? std::min<uint8_t>(*responseLength, payloadLen) : 0;
  if (response && copyLen) std::memcpy(response, packetBuffer_ + 1, copyLen);
  if (responseLength) *responseLength = copyLen;
  return true;
}

uint32_t DivPn532::getFirmwareVersion() {
  uint8_t response[4]{};
  uint8_t len = sizeof(response);
  if (!commandResponse(PN532_COMMAND_GETFIRMWAREVERSION, nullptr, 0, response, &len, 1000) || len < 4) {
    return 0;
  }
  return (static_cast<uint32_t>(response[0]) << 24) |
         (static_cast<uint32_t>(response[1]) << 16) |
         (static_cast<uint32_t>(response[2]) << 8) |
         response[3];
}

void DivPn532::samConfig() {
  const uint8_t cmd[] = {0x01, 0x14, 0x01};
  uint8_t response[2]{};
  uint8_t len = sizeof(response);
  commandResponse(PN532_COMMAND_SAMCONFIGURATION, cmd, sizeof(cmd), response, &len, 1000);
}

void DivPn532::setPassiveActivationRetries(uint8_t maxRetries) {
  const uint8_t cmd[] = {0x05, 0xFF, 0x01, maxRetries};
  uint8_t response[2]{};
  uint8_t len = sizeof(response);
  commandResponse(PN532_COMMAND_RFCONFIGURATION, cmd, sizeof(cmd), response, &len, 1000);
}

bool DivPn532::readPassiveTargetID(uint8_t cardbaudrate, uint8_t* uid, uint8_t* uidLength, uint16_t timeout) {
  const uint8_t cmd[] = {0x01, cardbaudrate};
  uint8_t response[24]{};
  uint8_t len = sizeof(response);
  if (!commandResponse(PN532_COMMAND_INLISTPASSIVETARGET, cmd, sizeof(cmd), response, &len, timeout ? timeout : 1000)) {
    return false;
  }
  if (len < 6 || response[0] < 1) return false;
  inListedTag_ = response[1];
  const uint8_t nfcidLen = response[5];
  if (len < static_cast<uint8_t>(6 + nfcidLen)) return false;
  if (uid && uidLength) {
    *uidLength = std::min<uint8_t>(nfcidLen, *uidLength ? *uidLength : nfcidLen);
    std::memcpy(uid, response + 6, *uidLength);
  }
  return true;
}

bool DivPn532::inDataExchange(const uint8_t* send, uint8_t sendLength, uint8_t* response, uint8_t* responseLength) {
  if (sendLength > sizeof(packetBuffer_) - 2) return false;
  packetBuffer_[0] = inListedTag_;
  std::memcpy(packetBuffer_ + 1, send, sendLength);
  uint8_t len = responseLength ? *responseLength : 0;
  if (!commandResponse(PN532_COMMAND_INDATAEXCHANGE, packetBuffer_, sendLength + 1, packetBuffer_, &len, 1000)) {
    return false;
  }
  if (len < 1 || packetBuffer_[0] != 0x00) return false;
  const uint8_t payloadLen = len - 1;
  const uint8_t copyLen = responseLength ? std::min<uint8_t>(*responseLength, payloadLen) : 0;
  if (response && copyLen) std::memcpy(response, packetBuffer_ + 1, copyLen);
  if (responseLength) *responseLength = copyLen;
  return true;
}

bool DivPn532::mifareClassicAuthenticateBlock(uint8_t* uid, uint8_t uidLen, uint8_t blockNumber,
                                                     uint8_t keyNumber, uint8_t* keyData) {
  uint8_t cmd[13]{};
  cmd[0] = keyNumber ? PN532_AUTH_WITH_KEYB : PN532_AUTH_WITH_KEYA;
  cmd[1] = blockNumber;
  std::memcpy(cmd + 2, keyData, 6);
  std::memcpy(cmd + 8, uid, std::min<uint8_t>(uidLen, 4));
  uint8_t response[2]{};
  uint8_t len = sizeof(response);
  return inDataExchange(cmd, 12, response, &len);
}

bool DivPn532::mifareClassicReadDataBlock(uint8_t blockNumber, uint8_t* data) {
  const uint8_t cmd[] = {PN532_MIFARE_READ, blockNumber};
  uint8_t len = 16;
  return inDataExchange(cmd, sizeof(cmd), data, &len) && len == 16;
}

bool DivPn532::mifareClassicWriteDataBlock(uint8_t blockNumber, const uint8_t* data) {
  uint8_t cmd[18]{};
  cmd[0] = PN532_MIFARE_WRITE;
  cmd[1] = blockNumber;
  std::memcpy(cmd + 2, data, 16);
  uint8_t response[2]{};
  uint8_t len = sizeof(response);
  return inDataExchange(cmd, sizeof(cmd), response, &len);
}

bool DivPn532::mifareUltralightReadPage(uint8_t page, uint8_t* buffer) {
  const uint8_t cmd[] = {PN532_MIFARE_READ, page};
  uint8_t len = 16;
  return inDataExchange(cmd, sizeof(cmd), buffer, &len) && len >= 4;
}

bool DivPn532::mifareUltralightWritePage(uint8_t page, const uint8_t* data) {
  uint8_t cmd[6]{};
  cmd[0] = PN532_MIFARE_UL_WRITE;
  cmd[1] = page;
  std::memcpy(cmd + 2, data, 4);
  uint8_t response[2]{};
  uint8_t len = sizeof(response);
  return inDataExchange(cmd, sizeof(cmd), response, &len);
}

bool DivPn532::asTarget() {
  const uint8_t cmd[] = {
      0x00, 0x04, 0x00, 0x12, 0x34, 0x56, 0x20,
      0x01, 0xFE, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7,
      0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7,
      0xFF, 0xFF, 0xAA, 0x99, 0x88, 0x77, 0x66, 0x55,
      0x44, 0x33, 0x22, 0x11, 0x00, 0x00};
  uint8_t response[4]{};
  uint8_t len = sizeof(response);
  return commandResponse(PN532_COMMAND_TGINITASTARGET, cmd, sizeof(cmd), response, &len, 1000) && len >= 1;
}

bool DivPn532::getDataTarget(uint8_t* cmd, uint8_t* cmdlen) {
  uint8_t len = cmdlen ? *cmdlen : 0;
  if (!commandResponse(PN532_COMMAND_TGGETDATA, nullptr, 0, packetBuffer_, &len, 1000)) return false;
  if (len < 1 || packetBuffer_[0] != 0x00) return false;
  const uint8_t payloadLen = len - 1;
  const uint8_t copyLen = cmdlen ? std::min<uint8_t>(*cmdlen, payloadLen) : 0;
  if (cmd && copyLen) std::memcpy(cmd, packetBuffer_ + 1, copyLen);
  if (cmdlen) *cmdlen = copyLen;
  return true;
}

bool DivPn532::setDataTarget(const uint8_t* cmd, uint8_t cmdlen) {
  uint8_t response[2]{};
  uint8_t len = sizeof(response);
  return commandResponse(PN532_COMMAND_TGSETDATA, cmd, cmdlen, response, &len, 1000) &&
         len >= 1 && response[0] == 0x00;
}
