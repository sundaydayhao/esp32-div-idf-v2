#include "div_cc1101.h"

#include <algorithm>
#include <cstring>

namespace {
constexpr uint8_t WRITE_BURST = 0x40;
constexpr uint8_t READ_SINGLE = 0x80;
constexpr uint8_t READ_BURST = 0xC0;
constexpr uint8_t BYTES_IN_RXFIFO = 0x7F;

uint8_t paTable[8] = {0x00, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
constexpr uint8_t paTable315[8] = {0x12, 0x0D, 0x1C, 0x34, 0x51, 0x85, 0xCB, 0xC2};
constexpr uint8_t paTable433[8] = {0x12, 0x0E, 0x1D, 0x34, 0x60, 0x84, 0xC8, 0xC0};
constexpr uint8_t paTable868[10] = {0x03, 0x17, 0x1D, 0x26, 0x37, 0x50, 0x86, 0xCD, 0xC5, 0xC0};
constexpr uint8_t paTable915[10] = {0x03, 0x0E, 0x1E, 0x27, 0x38, 0x8E, 0x84, 0xCC, 0xC3, 0xC0};

static uint8_t choosePa(const uint8_t* table, size_t n, int p, bool hasMinus6) {
  if (p <= -30) return table[0];
  if (p <= -20) return table[1];
  if (p <= -15) return table[2];
  if (p <= -10) return table[3];
  if (hasMinus6 && p <= -6) return table[4];
  if (p <= 0) return table[hasMinus6 ? 5 : 4];
  if (p <= 5) return table[hasMinus6 ? 6 : 5];
  if (p <= 7) return table[hasMinus6 ? 7 : 6];
  if (n > 8 && p <= 10) return table[8];
  return table[n - 1];
}

static uint8_t clampByte(int v) {
  return static_cast<uint8_t>(std::max(0, std::min(255, v)));
}
}  // namespace

DivCc1101 DivCc1101Radio;

void DivCc1101::spiStart() {
  pinMode(sck_, OUTPUT);
  pinMode(mosi_, OUTPUT);
  pinMode(miso_, INPUT);
  pinMode(ss_, OUTPUT);
  digitalWrite(ss_, HIGH);
  digitalWrite(sck_, LOW);
  digitalWrite(mosi_, LOW);
}

void DivCc1101::spiEnd() {}

uint8_t DivCc1101::transfer(uint8_t value) {
  uint8_t out = 0;
  for (int bit = 7; bit >= 0; --bit) {
    digitalWrite(mosi_, (value >> bit) & 1);
    delayMicroseconds(1);
    digitalWrite(sck_, HIGH);
    delayMicroseconds(1);
    if (digitalRead(miso_)) out |= (1u << bit);
    digitalWrite(sck_, LOW);
    delayMicroseconds(1);
  }
  return out;
}

void DivCc1101::select() {
  digitalWrite(ss_, LOW);
  waitMisoLow();
}

void DivCc1101::deselect() {
  digitalWrite(ss_, HIGH);
}

void DivCc1101::waitMisoLow(uint32_t timeoutUs) {
  const uint32_t start = micros();
  while (digitalRead(miso_) && (micros() - start) < timeoutUs) {
    delayMicroseconds(1);
  }
}

void DivCc1101::gdoSet() {
  if (gdo0_ != 0xFF) pinMode(gdo0_, OUTPUT);
  if (gdo2_ != 0xFF) pinMode(gdo2_, INPUT);
}

void DivCc1101::gdo0Set() {
  if (gdo0_ != 0xFF) pinMode(gdo0_, INPUT);
}

void DivCc1101::reset() {
  spiStart();
  digitalWrite(ss_, LOW);
  delayMicroseconds(10);
  digitalWrite(ss_, HIGH);
  delayMicroseconds(40);
  select();
  transfer(CC1101_SRES);
  waitMisoLow();
  deselect();
  delay(1);
}

void DivCc1101::setSpiDefaults() {
  if (!customSpi_) {
    sck_ = 12;
    miso_ = 13;
    mosi_ = 11;
    ss_ = 5;
  }
}

void DivCc1101::Init() {
  setSpiDefaults();
  reset();
  regConfigSettings();
}

void DivCc1101::SpiWriteReg(uint8_t addr, uint8_t value) {
  spiStart();
  select();
  transfer(addr);
  transfer(value);
  deselect();
}

void DivCc1101::SpiWriteBurstReg(uint8_t addr, uint8_t* buffer, uint8_t num) {
  spiStart();
  select();
  transfer(addr | WRITE_BURST);
  for (uint8_t i = 0; i < num; ++i) transfer(buffer[i]);
  deselect();
}

void DivCc1101::SpiStrobe(uint8_t strobe) {
  spiStart();
  select();
  transfer(strobe);
  deselect();
}

uint8_t DivCc1101::SpiReadReg(uint8_t addr) {
  spiStart();
  select();
  transfer(addr | READ_SINGLE);
  const uint8_t value = transfer(0);
  deselect();
  return value;
}

void DivCc1101::SpiReadBurstReg(uint8_t addr, uint8_t* buffer, uint8_t num) {
  spiStart();
  select();
  transfer(addr | READ_BURST);
  for (uint8_t i = 0; i < num; ++i) buffer[i] = transfer(0);
  deselect();
}

uint8_t DivCc1101::SpiReadStatus(uint8_t addr) {
  spiStart();
  select();
  transfer(addr | READ_BURST);
  const uint8_t value = transfer(0);
  deselect();
  return value;
}

void DivCc1101::setSpiPin(uint8_t sck, uint8_t miso, uint8_t mosi, uint8_t ss) {
  customSpi_ = true;
  sck_ = sck;
  miso_ = miso;
  mosi_ = mosi;
  ss_ = ss;
}

void DivCc1101::addSpiPin(uint8_t sck, uint8_t miso, uint8_t mosi, uint8_t ss, uint8_t modul) {
  if (modul >= kMaxModules) return;
  customSpi_ = true;
  sckM_[modul] = sck;
  misoM_[modul] = miso;
  mosiM_[modul] = mosi;
  ssM_[modul] = ss;
}

void DivCc1101::setGDO(uint8_t gdo0, uint8_t gdo2) {
  gdo0_ = gdo0;
  gdo2_ = gdo2;
  gdoSet();
}

void DivCc1101::setGDO0(uint8_t gdo0) {
  gdo0_ = gdo0;
  gdo0Set();
}

void DivCc1101::addGDO(uint8_t gdo0, uint8_t gdo2, uint8_t modul) {
  if (modul >= kMaxModules) return;
  gdo0M_[modul] = gdo0;
  gdo2M_[modul] = gdo2;
  gdoSetMode_ = 2;
}

void DivCc1101::addGDO0(uint8_t gdo0, uint8_t modul) {
  if (modul >= kMaxModules) return;
  gdo0M_[modul] = gdo0;
  gdoSetMode_ = 1;
}

void DivCc1101::setModul(uint8_t modul) {
  if (modul >= kMaxModules) return;
  if (customSpi_) {
    sck_ = sckM_[modul];
    miso_ = misoM_[modul];
    mosi_ = mosiM_[modul];
    ss_ = ssM_[modul];
  }
  if (gdoSetMode_ >= 1) gdo0_ = gdo0M_[modul];
  if (gdoSetMode_ >= 2) gdo2_ = gdo2M_[modul];
  if (gdoSetMode_ == 1) gdo0Set();
  if (gdoSetMode_ == 2) gdoSet();
}

void DivCc1101::setCCMode(bool s) {
  ccMode_ = s;
  if (s) {
    SpiWriteReg(CC1101_IOCFG2, 0x0B);
    SpiWriteReg(CC1101_IOCFG0, 0x06);
    SpiWriteReg(CC1101_PKTCTRL0, 0x05);
    SpiWriteReg(CC1101_MDMCFG3, 0xF8);
    SpiWriteReg(CC1101_MDMCFG2, 0x93);
    SpiWriteReg(CC1101_MDMCFG1, 0x22);
    SpiWriteReg(CC1101_MDMCFG0, 0xF8);
    SpiWriteReg(CC1101_DEVIATN, 0x47);
  } else {
    SpiWriteReg(CC1101_IOCFG2, 0x0D);
    SpiWriteReg(CC1101_IOCFG0, 0x0D);
    SpiWriteReg(CC1101_PKTCTRL0, 0x32);
    SpiWriteReg(CC1101_MDMCFG3, 0x93);
    SpiWriteReg(CC1101_MDMCFG2, 0x30);
    SpiWriteReg(CC1101_MDMCFG1, 0x22);
    SpiWriteReg(CC1101_MDMCFG0, 0xF8);
    SpiWriteReg(CC1101_DEVIATN, 0x00);
  }
}

void DivCc1101::setModulation(uint8_t m) {
  if (m > 4) m = 4;
  modulation_ = m;
  splitMdmcfg2();
  static const uint8_t modBits[] = {0x00, 0x10, 0x30, 0x40, 0x70};
  m2MODFM_ = modBits[m];
  SpiWriteReg(CC1101_MDMCFG2, m2DCOFF_ + m2MODFM_ + m2MANCH_ + m2SYNCM_);
  setPA(pa_);
}

void DivCc1101::setPA(int p) {
  pa_ = p;
  uint8_t a = 0xC0;
  if (mhz_ >= 300 && mhz_ <= 348) {
    a = choosePa(paTable315, 8, p, false);
    lastPaBand_ = 1;
  } else if (mhz_ >= 378 && mhz_ <= 464) {
    a = choosePa(paTable433, 8, p, false);
    lastPaBand_ = 2;
  } else if (mhz_ >= 779 && mhz_ <= 899.99f) {
    a = choosePa(paTable868, 10, p, true);
    lastPaBand_ = 3;
  } else if (mhz_ >= 900 && mhz_ <= 928) {
    a = choosePa(paTable915, 10, p, true);
    lastPaBand_ = 4;
  }
  if (modulation_ == 2) {
    paTable[0] = 0;
    paTable[1] = a;
  } else {
    paTable[0] = a;
    paTable[1] = 0;
  }
  SpiWriteBurstReg(CC1101_PATABLE, paTable, sizeof(paTable));
}

void DivCc1101::setMHZ(float mhz) {
  mhz_ = mhz;
  const uint32_t freq = static_cast<uint32_t>((mhz * 65536.0f) / 26.0f);
  SpiWriteReg(CC1101_FREQ2, (freq >> 16) & 0xFF);
  SpiWriteReg(CC1101_FREQ1, (freq >> 8) & 0xFF);
  SpiWriteReg(CC1101_FREQ0, freq & 0xFF);
  calibrate();
}

void DivCc1101::calibrate() {
  if (mhz_ >= 300 && mhz_ <= 348) {
    SpiWriteReg(CC1101_FSCTRL0, clampByte(map(static_cast<long>(mhz_), 300, 348, clb1_[0], clb1_[1])));
    SpiWriteReg(CC1101_TEST0, mhz_ < 322.88f ? 0x0B : 0x09);
    if (lastPaBand_ != 1) setPA(pa_);
  } else if (mhz_ >= 378 && mhz_ <= 464) {
    SpiWriteReg(CC1101_FSCTRL0, clampByte(map(static_cast<long>(mhz_), 378, 464, clb2_[0], clb2_[1])));
    SpiWriteReg(CC1101_TEST0, mhz_ < 430.5f ? 0x0B : 0x09);
    if (lastPaBand_ != 2) setPA(pa_);
  } else if (mhz_ >= 779 && mhz_ <= 899.99f) {
    SpiWriteReg(CC1101_FSCTRL0, clampByte(map(static_cast<long>(mhz_), 779, 899, clb3_[0], clb3_[1])));
    SpiWriteReg(CC1101_TEST0, mhz_ < 861 ? 0x0B : 0x09);
    if (lastPaBand_ != 3) setPA(pa_);
  } else if (mhz_ >= 900 && mhz_ <= 928) {
    SpiWriteReg(CC1101_FSCTRL0, clampByte(map(static_cast<long>(mhz_), 900, 928, clb4_[0], clb4_[1])));
    SpiWriteReg(CC1101_TEST0, 0x09);
    if (lastPaBand_ != 4) setPA(pa_);
  }
  int s = SpiReadStatus(CC1101_FSCAL2);
  if (s < 32) SpiWriteReg(CC1101_FSCAL2, static_cast<uint8_t>(s + 32));
}

void DivCc1101::setChannel(uint8_t chnl) {
  channel_ = chnl;
  SpiWriteReg(CC1101_CHANNR, chnl);
}

void DivCc1101::setChsp(float f) {
  splitMdmcfg1();
  uint8_t mdmcfg0 = 0;
  int e = 0;
  float c = f;
  if (c > 405.456543f) c = 405.456543f;
  if (c < 25.390625f) c = 25.390625f;
  for (int i = 0; i < 3; ++i) {
    if (c <= 50.682068f) {
      c = (c - 25.390625f) / 0.0991825f;
      mdmcfg0 = static_cast<uint8_t>(c + 0.5f);
      break;
    }
    e++;
    c /= 2;
  }
  m1CHSP_ = static_cast<uint8_t>(e);
  SpiWriteReg(CC1101_MDMCFG1, m1FEC_ + m1PRE_ + m1CHSP_);
  SpiWriteReg(CC1101_MDMCFG0, mdmcfg0);
}

void DivCc1101::setRxBW(float f) {
  splitMdmcfg4();
  int s1 = 3;
  int s2 = 3;
  for (int i = 0; i < 3 && f > 101.5625f; ++i) {
    f /= 2;
    s1--;
  }
  for (int i = 0; i < 3 && f > 58.1f; ++i) {
    f /= 1.25f;
    s2--;
  }
  m4RxBw_ = static_cast<uint8_t>(s1 * 64 + s2 * 16);
  SpiWriteReg(CC1101_MDMCFG4, m4RxBw_ + m4DaRa_);
}

void DivCc1101::setDRate(float d) {
  splitMdmcfg4();
  d = std::max(0.0247955f, std::min(1621.83f, d));
  m4DaRa_ = 0;
  uint8_t mdmcfg3 = 0;
  for (int i = 0; i < 20; ++i) {
    if (d <= 0.0494942f) {
      float c = (d - 0.0247955f) / 0.00009685f;
      mdmcfg3 = static_cast<uint8_t>(c + 0.5f);
      break;
    }
    m4DaRa_++;
    d /= 2;
  }
  SpiWriteReg(CC1101_MDMCFG4, m4RxBw_ + m4DaRa_);
  SpiWriteReg(CC1101_MDMCFG3, mdmcfg3);
}

void DivCc1101::setDeviation(float d) {
  d = std::max(1.586914f, std::min(380.859375f, d));
  float f = 1.586914f;
  float v = 0.19836425f;
  int c = 0;
  for (int i = 0; i < 255; ++i) {
    f += v;
    if (c == 7) {
      v *= 2;
      c = -1;
      i += 8;
    }
    if (f >= d) {
      c = i;
      break;
    }
    c++;
  }
  SpiWriteReg(CC1101_DEVIATN, clampByte(c));
}

void DivCc1101::regConfigSettings() {
  SpiWriteReg(CC1101_FSCTRL1, 0x06);
  setCCMode(ccMode_);
  setMHZ(mhz_);
  SpiWriteReg(CC1101_MDMCFG1, 0x02);
  SpiWriteReg(CC1101_MDMCFG0, 0xF8);
  SpiWriteReg(CC1101_CHANNR, channel_);
  SpiWriteReg(CC1101_DEVIATN, 0x47);
  SpiWriteReg(CC1101_FREND1, 0x56);
  SpiWriteReg(CC1101_MCSM0, 0x18);
  SpiWriteReg(CC1101_FOCCFG, 0x16);
  SpiWriteReg(CC1101_BSCFG, 0x1C);
  SpiWriteReg(CC1101_AGCCTRL2, 0xC7);
  SpiWriteReg(CC1101_AGCCTRL1, 0x00);
  SpiWriteReg(CC1101_AGCCTRL0, 0xB2);
  SpiWriteReg(CC1101_FSCAL3, 0xE9);
  SpiWriteReg(CC1101_FSCAL2, 0x2A);
  SpiWriteReg(CC1101_FSCAL1, 0x00);
  SpiWriteReg(CC1101_FSCAL0, 0x1F);
  SpiWriteReg(CC1101_FSTEST, 0x59);
  SpiWriteReg(CC1101_TEST2, 0x81);
  SpiWriteReg(CC1101_TEST1, 0x35);
  SpiWriteReg(CC1101_TEST0, 0x09);
  SpiWriteReg(CC1101_PKTCTRL1, 0x04);
  SpiWriteReg(CC1101_ADDR, 0x00);
  SpiWriteReg(CC1101_PKTLEN, 0x00);
}

void DivCc1101::SetTx() {
  SpiStrobe(CC1101_SIDLE);
  SpiStrobe(CC1101_STX);
  trxstate_ = 1;
}

void DivCc1101::SetRx() {
  SpiStrobe(CC1101_SIDLE);
  SpiStrobe(CC1101_SRX);
  trxstate_ = 2;
}

void DivCc1101::SetTx(float mhz) {
  SpiStrobe(CC1101_SIDLE);
  setMHZ(mhz);
  SpiStrobe(CC1101_STX);
  trxstate_ = 1;
}

void DivCc1101::SetRx(float mhz) {
  SpiStrobe(CC1101_SIDLE);
  setMHZ(mhz);
  SpiStrobe(CC1101_SRX);
  trxstate_ = 2;
}

int DivCc1101::getRssi() {
  int rssi = SpiReadStatus(CC1101_RSSI);
  return (rssi >= 128) ? ((rssi - 256) / 2 - 74) : (rssi / 2 - 74);
}

uint8_t DivCc1101::getLqi() {
  return SpiReadStatus(CC1101_LQI);
}

void DivCc1101::setSres() {
  SpiStrobe(CC1101_SRES);
  trxstate_ = 0;
}

void DivCc1101::setSidle() {
  SpiStrobe(CC1101_SIDLE);
  trxstate_ = 0;
}

void DivCc1101::goSleep() {
  trxstate_ = 0;
  SpiStrobe(CC1101_SIDLE);
  SpiStrobe(CC1101_SPWD);
}

void DivCc1101::SendData(char* txchar) {
  SendData(reinterpret_cast<uint8_t*>(txchar), static_cast<uint8_t>(std::min<size_t>(std::strlen(txchar), 61)));
}

void DivCc1101::SendData(uint8_t* txBuffer, uint8_t size) {
  size = std::min<uint8_t>(size, 61);
  SpiWriteReg(CC1101_TXFIFO, size);
  SpiWriteBurstReg(CC1101_TXFIFO, txBuffer, size);
  SpiStrobe(CC1101_SIDLE);
  SpiStrobe(CC1101_STX);
  if (gdo0_ != 0xFF) {
    uint32_t start = millis();
    while (!digitalRead(gdo0_) && millis() - start < 1000) delay(1);
    start = millis();
    while (digitalRead(gdo0_) && millis() - start < 1000) delay(1);
  }
  SpiStrobe(CC1101_SFTX);
  trxstate_ = 1;
}

void DivCc1101::SendData(char* txchar, int t) {
  SendData(reinterpret_cast<uint8_t*>(txchar), static_cast<uint8_t>(std::min<size_t>(std::strlen(txchar), 61)), t);
}

void DivCc1101::SendData(uint8_t* txBuffer, uint8_t size, int t) {
  size = std::min<uint8_t>(size, 61);
  SpiWriteReg(CC1101_TXFIFO, size);
  SpiWriteBurstReg(CC1101_TXFIFO, txBuffer, size);
  SpiStrobe(CC1101_SIDLE);
  SpiStrobe(CC1101_STX);
  delay(t);
  SpiStrobe(CC1101_SFTX);
  trxstate_ = 1;
}

bool DivCc1101::CheckCRC() {
  const uint8_t lqi = SpiReadStatus(CC1101_LQI);
  if (lqi & 0x80) return true;
  SpiStrobe(CC1101_SFRX);
  SpiStrobe(CC1101_SRX);
  return false;
}

bool DivCc1101::CheckRxFifo(int t) {
  if (trxstate_ != 2) SetRx();
  if (SpiReadStatus(CC1101_RXBYTES) & BYTES_IN_RXFIFO) {
    delay(t);
    return true;
  }
  return false;
}

uint8_t DivCc1101::CheckReceiveFlag() {
  if (trxstate_ != 2) SetRx();
  if (gdo0_ != 0xFF && digitalRead(gdo0_)) {
    while (digitalRead(gdo0_)) delay(1);
    return 1;
  }
  return (SpiReadStatus(CC1101_RXBYTES) & BYTES_IN_RXFIFO) ? 1 : 0;
}

uint8_t DivCc1101::ReceiveData(uint8_t* rxBuffer) {
  if (!(SpiReadStatus(CC1101_RXBYTES) & BYTES_IN_RXFIFO)) {
    SpiStrobe(CC1101_SFRX);
    SpiStrobe(CC1101_SRX);
    return 0;
  }
  uint8_t size = SpiReadReg(CC1101_RXFIFO);
  if (size > 64) size = 64;
  SpiReadBurstReg(CC1101_RXFIFO, rxBuffer, size);
  uint8_t status[2];
  SpiReadBurstReg(CC1101_RXFIFO, status, 2);
  SpiStrobe(CC1101_SFRX);
  SpiStrobe(CC1101_SRX);
  return size;
}

void DivCc1101::setClb(uint8_t b, uint8_t s, uint8_t e) {
  uint8_t* clb = nullptr;
  if (b == 1) clb = clb1_;
  else if (b == 2) clb = clb2_;
  else if (b == 3) clb = clb3_;
  else if (b == 4) clb = clb4_;
  if (clb) {
    clb[0] = s;
    clb[1] = e;
  }
}

bool DivCc1101::getCC1101() {
  setSpiDefaults();
  uint8_t version = SpiReadStatus(CC1101_VERSION);
  return version != 0x00 && version != 0xFF;
}

uint8_t DivCc1101::getMode() {
  return trxstate_;
}

void DivCc1101::setSyncWord(uint8_t sh, uint8_t sl) {
  SpiWriteReg(CC1101_SYNC1, sh);
  SpiWriteReg(CC1101_SYNC0, sl);
}

void DivCc1101::setAddr(uint8_t v) { SpiWriteReg(CC1101_ADDR, v); }

void DivCc1101::splitPktctrl1() {
  int calc = SpiReadStatus(CC1101_PKTCTRL1);
  pc1PQT_ = calc & 0xE0;
  pc1CRC_AF_ = calc & 0x08;
  pc1APP_ST_ = calc & 0x04;
  pc1ADRCHK_ = calc & 0x03;
}

void DivCc1101::splitPktctrl0() {
  int calc = SpiReadStatus(CC1101_PKTCTRL0);
  pc0WDATA_ = calc & 0x40;
  pc0PktForm_ = calc & 0x30;
  pc0CRC_EN_ = calc & 0x04;
  pc0LenConf_ = calc & 0x03;
}

void DivCc1101::splitMdmcfg1() {
  int calc = SpiReadStatus(CC1101_MDMCFG1);
  m1FEC_ = calc & 0x80;
  m1PRE_ = calc & 0x70;
  m1CHSP_ = calc & 0x03;
}

void DivCc1101::splitMdmcfg2() {
  int calc = SpiReadStatus(CC1101_MDMCFG2);
  m2DCOFF_ = calc & 0x80;
  m2MODFM_ = calc & 0x70;
  m2MANCH_ = calc & 0x08;
  m2SYNCM_ = calc & 0x07;
}

void DivCc1101::splitMdmcfg4() {
  int calc = SpiReadStatus(CC1101_MDMCFG4);
  m4RxBw_ = calc & 0xF0;
  m4DaRa_ = calc & 0x0F;
}

void DivCc1101::setWhiteData(bool v) {
  splitPktctrl0();
  pc0WDATA_ = v ? 0x40 : 0x00;
  SpiWriteReg(CC1101_PKTCTRL0, pc0WDATA_ + pc0PktForm_ + pc0CRC_EN_ + pc0LenConf_);
}

void DivCc1101::setPktFormat(uint8_t v) {
  splitPktctrl0();
  pc0PktForm_ = (v & 0x03) << 4;
  SpiWriteReg(CC1101_PKTCTRL0, pc0WDATA_ + pc0PktForm_ + pc0CRC_EN_ + pc0LenConf_);
}

void DivCc1101::setCrc(bool v) {
  splitPktctrl0();
  pc0CRC_EN_ = v ? 0x04 : 0x00;
  SpiWriteReg(CC1101_PKTCTRL0, pc0WDATA_ + pc0PktForm_ + pc0CRC_EN_ + pc0LenConf_);
}

void DivCc1101::setLengthConfig(uint8_t v) {
  splitPktctrl0();
  pc0LenConf_ = v & 0x03;
  SpiWriteReg(CC1101_PKTCTRL0, pc0WDATA_ + pc0PktForm_ + pc0CRC_EN_ + pc0LenConf_);
}

void DivCc1101::setPacketLength(uint8_t v) { SpiWriteReg(CC1101_PKTLEN, v); }

void DivCc1101::setDcFilterOff(bool v) {
  splitMdmcfg2();
  m2DCOFF_ = v ? 0x80 : 0;
  SpiWriteReg(CC1101_MDMCFG2, m2DCOFF_ + m2MODFM_ + m2MANCH_ + m2SYNCM_);
}

void DivCc1101::setManchester(bool v) {
  splitMdmcfg2();
  m2MANCH_ = v ? 0x08 : 0;
  SpiWriteReg(CC1101_MDMCFG2, m2DCOFF_ + m2MODFM_ + m2MANCH_ + m2SYNCM_);
}

void DivCc1101::setSyncMode(uint8_t v) {
  splitMdmcfg2();
  m2SYNCM_ = v & 0x07;
  SpiWriteReg(CC1101_MDMCFG2, m2DCOFF_ + m2MODFM_ + m2MANCH_ + m2SYNCM_);
}

void DivCc1101::setFEC(bool v) {
  splitMdmcfg1();
  m1FEC_ = v ? 0x80 : 0;
  SpiWriteReg(CC1101_MDMCFG1, m1FEC_ + m1PRE_ + m1CHSP_);
}

void DivCc1101::setPRE(uint8_t v) {
  splitMdmcfg1();
  m1PRE_ = (v & 0x07) << 4;
  SpiWriteReg(CC1101_MDMCFG1, m1FEC_ + m1PRE_ + m1CHSP_);
}

void DivCc1101::setPQT(uint8_t v) {
  splitPktctrl1();
  pc1PQT_ = (v & 0x07) << 5;
  SpiWriteReg(CC1101_PKTCTRL1, pc1PQT_ + pc1CRC_AF_ + pc1APP_ST_ + pc1ADRCHK_);
}

void DivCc1101::setCRC_AF(bool v) {
  splitPktctrl1();
  pc1CRC_AF_ = v ? 0x08 : 0;
  SpiWriteReg(CC1101_PKTCTRL1, pc1PQT_ + pc1CRC_AF_ + pc1APP_ST_ + pc1ADRCHK_);
}

void DivCc1101::setAppendStatus(bool v) {
  splitPktctrl1();
  pc1APP_ST_ = v ? 0x04 : 0;
  SpiWriteReg(CC1101_PKTCTRL1, pc1PQT_ + pc1CRC_AF_ + pc1APP_ST_ + pc1ADRCHK_);
}

void DivCc1101::setAdrChk(uint8_t v) {
  splitPktctrl1();
  pc1ADRCHK_ = v & 0x03;
  SpiWriteReg(CC1101_PKTCTRL1, pc1PQT_ + pc1CRC_AF_ + pc1APP_ST_ + pc1ADRCHK_);
}
