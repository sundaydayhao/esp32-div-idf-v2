#pragma once

#include <cstdint>

#include "div_platform.h"

#define CC1101_IOCFG2       0x00
#define CC1101_IOCFG1       0x01
#define CC1101_IOCFG0       0x02
#define CC1101_FIFOTHR      0x03
#define CC1101_SYNC1        0x04
#define CC1101_SYNC0        0x05
#define CC1101_PKTLEN       0x06
#define CC1101_PKTCTRL1     0x07
#define CC1101_PKTCTRL0     0x08
#define CC1101_ADDR         0x09
#define CC1101_CHANNR       0x0A
#define CC1101_FSCTRL1      0x0B
#define CC1101_FSCTRL0      0x0C
#define CC1101_FREQ2        0x0D
#define CC1101_FREQ1        0x0E
#define CC1101_FREQ0        0x0F
#define CC1101_MDMCFG4      0x10
#define CC1101_MDMCFG3      0x11
#define CC1101_MDMCFG2      0x12
#define CC1101_MDMCFG1      0x13
#define CC1101_MDMCFG0      0x14
#define CC1101_DEVIATN      0x15
#define CC1101_MCSM2        0x16
#define CC1101_MCSM1        0x17
#define CC1101_MCSM0        0x18
#define CC1101_FOCCFG       0x19
#define CC1101_BSCFG        0x1A
#define CC1101_AGCCTRL2     0x1B
#define CC1101_AGCCTRL1     0x1C
#define CC1101_AGCCTRL0     0x1D
#define CC1101_WOREVT1      0x1E
#define CC1101_WOREVT0      0x1F
#define CC1101_WORCTRL      0x20
#define CC1101_FREND1       0x21
#define CC1101_FREND0       0x22
#define CC1101_FSCAL3       0x23
#define CC1101_FSCAL2       0x24
#define CC1101_FSCAL1       0x25
#define CC1101_FSCAL0       0x26
#define CC1101_RCCTRL1      0x27
#define CC1101_RCCTRL0      0x28
#define CC1101_FSTEST       0x29
#define CC1101_PTEST        0x2A
#define CC1101_AGCTEST      0x2B
#define CC1101_TEST2        0x2C
#define CC1101_TEST1        0x2D
#define CC1101_TEST0        0x2E

#define CC1101_SRES         0x30
#define CC1101_SFSTXON      0x31
#define CC1101_SXOFF        0x32
#define CC1101_SCAL         0x33
#define CC1101_SRX          0x34
#define CC1101_TXFIFO 0x3F
#define CC1101_STX 0x35
#define CC1101_SIDLE        0x36
#define CC1101_SAFC         0x37
#define CC1101_SWOR         0x38
#define CC1101_SPWD         0x39
#define CC1101_SFRX         0x3A
#define CC1101_SFTX         0x3B
#define CC1101_SWORRST      0x3C
#define CC1101_SNOP         0x3D

#define CC1101_PARTNUM      0x30
#define CC1101_VERSION      0x31
#define CC1101_FREQEST      0x32
#define CC1101_LQI          0x33
#define CC1101_RSSI         0x34
#define CC1101_MARCSTATE    0x35
#define CC1101_WORTIME1     0x36
#define CC1101_WORTIME0     0x37
#define CC1101_PKTSTATUS    0x38
#define CC1101_VCO_VC_DAC   0x39
#define CC1101_TXBYTES      0x3A
#define CC1101_RXBYTES      0x3B

#define CC1101_PATABLE      0x3E
#define CC1101_RXFIFO       0x3F

class DivCc1101 {
 public:
  void Init();
  uint8_t SpiReadStatus(uint8_t addr);
  void setSpiPin(uint8_t sck, uint8_t miso, uint8_t mosi, uint8_t ss);
  void addSpiPin(uint8_t sck, uint8_t miso, uint8_t mosi, uint8_t ss, uint8_t modul);
  void setGDO(uint8_t gdo0, uint8_t gdo2);
  void setGDO0(uint8_t gdo0);
  void addGDO(uint8_t gdo0, uint8_t gdo2, uint8_t modul);
  void addGDO0(uint8_t gdo0, uint8_t modul);
  void setModul(uint8_t modul);
  void setCCMode(bool s);
  void setModulation(uint8_t m);
  void setPA(int p);
  void setMHZ(float mhz);
  void setChannel(uint8_t chnl);
  void setChsp(float f);
  void setRxBW(float f);
  void setDRate(float d);
  void setDeviation(float d);
  void SetTx();
  void SetRx();
  void SetTx(float mhz);
  void SetRx(float mhz);
  int getRssi();
  uint8_t getLqi();
  void setSres();
  void setSidle();
  void goSleep();
  void SendData(uint8_t* txBuffer, uint8_t size);
  void SendData(char* txchar);
  void SendData(uint8_t* txBuffer, uint8_t size, int t);
  void SendData(char* txchar, int t);
  uint8_t CheckReceiveFlag();
  uint8_t ReceiveData(uint8_t* rxBuffer);
  bool CheckCRC();
  void SpiStrobe(uint8_t strobe);
  void SpiWriteReg(uint8_t addr, uint8_t value);
  void SpiWriteBurstReg(uint8_t addr, uint8_t* buffer, uint8_t num);
  uint8_t SpiReadReg(uint8_t addr);
  void SpiReadBurstReg(uint8_t addr, uint8_t* buffer, uint8_t num);
  void setClb(uint8_t b, uint8_t s, uint8_t e);
  bool getCC1101();
  uint8_t getMode();
  void setSyncWord(uint8_t sh, uint8_t sl);
  void setAddr(uint8_t v);
  void setWhiteData(bool v);
  void setPktFormat(uint8_t v);
  void setCrc(bool v);
  void setLengthConfig(uint8_t v);
  void setPacketLength(uint8_t v);
  void setDcFilterOff(bool v);
  void setManchester(bool v);
  void setSyncMode(uint8_t v);
  void setFEC(bool v);
  void setPRE(uint8_t v);
  void setPQT(uint8_t v);
  void setCRC_AF(bool v);
  void setAppendStatus(bool v);
  void setAdrChk(uint8_t v);
  bool CheckRxFifo(int t);

 private:
  static constexpr uint8_t kMaxModules = 6;
  void spiStart();
  void spiEnd();
  void gdoSet();
  void gdo0Set();
  void reset();
  void setSpiDefaults();
  void regConfigSettings();
  void calibrate();
  void splitPktctrl0();
  void splitPktctrl1();
  void splitMdmcfg1();
  void splitMdmcfg2();
  void splitMdmcfg4();
  uint8_t transfer(uint8_t value);
  void select();
  void deselect();
  void waitMisoLow(uint32_t timeoutUs = 10000);

  uint8_t modulation_ = 2;
  uint8_t frend0_ = 0;
  uint8_t channel_ = 0;
  int pa_ = 12;
  uint8_t lastPaBand_ = 0;
  uint8_t sck_ = 12;
  uint8_t miso_ = 13;
  uint8_t mosi_ = 11;
  uint8_t ss_ = 5;
  uint8_t gdo0_ = 0xFF;
  uint8_t gdo2_ = 0xFF;
  uint8_t sckM_[kMaxModules]{};
  uint8_t misoM_[kMaxModules]{};
  uint8_t mosiM_[kMaxModules]{};
  uint8_t ssM_[kMaxModules]{};
  uint8_t gdo0M_[kMaxModules]{};
  uint8_t gdo2M_[kMaxModules]{};
  uint8_t gdoSetMode_ = 0;
  bool customSpi_ = false;
  bool ccMode_ = false;
  float mhz_ = 433.92f;
  uint8_t m4RxBw_ = 0;
  uint8_t m4DaRa_ = 0;
  uint8_t m2DCOFF_ = 0;
  uint8_t m2MODFM_ = 0;
  uint8_t m2MANCH_ = 0;
  uint8_t m2SYNCM_ = 0;
  uint8_t m1FEC_ = 0;
  uint8_t m1PRE_ = 0;
  uint8_t m1CHSP_ = 0;
  uint8_t pc1PQT_ = 0;
  uint8_t pc1CRC_AF_ = 0;
  uint8_t pc1APP_ST_ = 0;
  uint8_t pc1ADRCHK_ = 0;
  uint8_t pc0WDATA_ = 0;
  uint8_t pc0PktForm_ = 0;
  uint8_t pc0CRC_EN_ = 0;
  uint8_t pc0LenConf_ = 0;
  uint8_t trxstate_ = 0;
  uint8_t clb1_[2] = {24, 28};
  uint8_t clb2_[2] = {31, 38};
  uint8_t clb3_[2] = {65, 76};
  uint8_t clb4_[2] = {77, 79};
};

extern DivCc1101 DivCc1101Radio;
