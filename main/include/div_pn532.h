#pragma once

#include <cstdint>
#include <cstring>

#define PN532_BRTY_ISO14443A_106KBPS 0x00
#define PN532_MIFARE_ISO14443A 0x00
#define PN532_AUTH_WITH_KEYA 0x60
#define PN532_AUTH_WITH_KEYB 0x61

class DivPn532 {
 public:
  DivPn532(int8_t sck, int8_t miso, int8_t mosi, int8_t ss);
  void begin();
  void samConfig();
  void setPassiveActivationRetries(uint8_t maxRetries);
  uint32_t getFirmwareVersion();
  bool readPassiveTargetID(uint8_t cardbaudrate, uint8_t* uid, uint8_t* uidLength, uint16_t timeout = 0);
  bool inDataExchange(const uint8_t* send, uint8_t sendLength, uint8_t* response, uint8_t* responseLength);
  bool mifareClassicAuthenticateBlock(uint8_t* uid, uint8_t uidLen, uint8_t blockNumber, uint8_t keyNumber, uint8_t* keyData);
  bool mifareClassicReadDataBlock(uint8_t blockNumber, uint8_t* data);
  bool mifareClassicWriteDataBlock(uint8_t blockNumber, const uint8_t* data);
  bool mifareUltralightReadPage(uint8_t page, uint8_t* buffer);
  bool mifareUltralightWritePage(uint8_t page, const uint8_t* data);
  bool asTarget();
  bool getDataTarget(uint8_t* cmd, uint8_t* cmdlen);
  bool setDataTarget(const uint8_t* cmd, uint8_t cmdlen);

 private:
  bool sendCommandCheckAck(const uint8_t* cmd, uint8_t cmdlen, uint16_t timeout = 1000);
  bool readResponse(uint8_t* response, uint8_t* responseLength, uint16_t timeout = 1000);
  bool isReady();
  void writeFrame(const uint8_t* data, uint8_t len);
  uint8_t spiRead();
  void spiWrite(uint8_t value);
  uint8_t spiTransfer(uint8_t value);
  void cs(bool high);
  bool waitReady(uint16_t timeout);
  bool commandResponse(uint8_t command, const uint8_t* cmd, uint8_t cmdlen,
                       uint8_t* response, uint8_t* responseLength, uint16_t timeout = 1000);

  int8_t sck_;
  int8_t miso_;
  int8_t mosi_;
  int8_t ss_;
  uint8_t packetBuffer_[265]{};
  uint8_t inListedTag_ = 1;
};
