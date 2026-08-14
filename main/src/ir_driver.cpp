#include "ir_recv.h"
#include "ir_send.h"

#include "div_platform.h"
#include "driver/gpio.h"
#include "esp_timer.h"

#include <algorithm>
#include <cstring>

namespace {
constexpr uint16_t kRawTickUs = 50;
constexpr uint16_t kMaxRaw = 512;
constexpr uint32_t kGapUs = 15000;

struct CaptureState {
  uint16_t pin = 0xFFFF;
  volatile bool enabled = false;
  volatile bool frameReady = false;
  volatile uint32_t lastEdgeUs = 0;
  volatile uint16_t pos = 0;
  volatile uint16_t work[kMaxRaw]{};
  volatile uint16_t readyLen = 0;
  volatile uint16_t ready[kMaxRaw]{};
};

CaptureState g_cap;
bool g_isrInstalled = false;

static uint16_t ticks(uint32_t us) {
  return static_cast<uint16_t>(std::min<uint32_t>(0xFFFF, std::max<uint32_t>(1, (us + kRawTickUs / 2) / kRawTickUs)));
}

static bool match(uint32_t measured, uint32_t desired, uint8_t tolerance = 30) {
  const uint32_t low = desired - (desired * tolerance / 100);
  const uint32_t high = desired + (desired * tolerance / 100);
  return measured >= low && measured <= high;
}

static uint32_t usecAt(const decode_results* r, uint16_t idx) {
  return static_cast<uint32_t>(r->rawbuf[idx]) * kRawTickUs;
}

static bool decodePulseDistance(decode_results* r, uint16_t offset, uint16_t nbits,
                                uint32_t oneMark, uint32_t oneSpace,
                                uint32_t zeroMark, uint32_t zeroSpace,
                                bool msbFirst, uint64_t& value) {
  value = 0;
  if (r->rawlen < offset + nbits * 2) return false;
  for (uint16_t i = 0; i < nbits; ++i) {
    const uint32_t mark = usecAt(r, offset + i * 2);
    const uint32_t space = usecAt(r, offset + i * 2 + 1);
    bool bit;
    if (match(mark, oneMark) && match(space, oneSpace)) bit = true;
    else if (match(mark, zeroMark) && match(space, zeroSpace)) bit = false;
    else return false;
    if (msbFirst) value = (value << 1) | (bit ? 1 : 0);
    else if (bit) value |= (1ULL << i);
  }
  return true;
}

static bool decodeKnown(decode_results* r) {
  uint64_t value = 0;
  if (r->rawlen >= 68 && match(usecAt(r, 1), 9000) && match(usecAt(r, 2), 4500) &&
      decodePulseDistance(r, 3, 32, 560, 1690, 560, 560, false, value)) {
    r->decode_type = NEC;
    r->value = value;
    r->bits = 32;
    return true;
  }
  if (r->rawlen >= 68 && match(usecAt(r, 1), 4500) && match(usecAt(r, 2), 4500) &&
      decodePulseDistance(r, 3, 32, 560, 1600, 560, 560, true, value)) {
    r->decode_type = SAMSUNG36;
    r->value = value;
    r->bits = 32;
    return true;
  }
  if (r->rawlen >= 26 && match(usecAt(r, 1), 2400) && match(usecAt(r, 2), 600) &&
      decodePulseDistance(r, 3, std::min<uint16_t>(20, (r->rawlen - 3) / 2), 1200, 600, 600, 600, false, value)) {
    r->decode_type = SONY;
    r->value = value;
    r->bits = std::min<uint16_t>(20, (r->rawlen - 3) / 2);
    return true;
  }
  if (r->rawlen >= 68 && match(usecAt(r, 1), 8000) && match(usecAt(r, 2), 4000) &&
      decodePulseDistance(r, 3, 32, 560, 1690, 560, 560, false, value)) {
    r->decode_type = LG;
    r->value = value;
    r->bits = 32;
    return true;
  }
  r->decode_type = UNKNOWN;
  r->value = 0;
  r->bits = 0;
  return false;
}

static void IRAM_ATTR irGpioIsr(void*) {
  if (!g_cap.enabled) return;
  const uint32_t now = static_cast<uint32_t>(esp_timer_get_time());
  const uint32_t delta = now - g_cap.lastEdgeUs;
  g_cap.lastEdgeUs = now;
  if (delta > kGapUs && g_cap.pos > 6) {
    const uint16_t pos = g_cap.pos;
    const uint16_t n = pos < kMaxRaw ? pos : kMaxRaw;
    for (uint16_t i = 0; i < n; ++i) g_cap.ready[i] = g_cap.work[i];
    g_cap.readyLen = n;
    g_cap.frameReady = true;
    g_cap.pos = 0;
    g_cap.work[g_cap.pos++] = ticks(delta);
    return;
  }
  if (g_cap.pos < kMaxRaw) {
    g_cap.work[g_cap.pos++] = ticks(delta);
  }
}
}  // namespace

IRrecv::IRrecv(uint16_t recvpin) : recvpin_(recvpin) {}

void IRrecv::enableIRIn() {
  pinMode(recvpin_, INPUT_PULLUP);
  g_cap.pin = recvpin_;
  g_cap.enabled = false;
  g_cap.frameReady = false;
  g_cap.pos = 0;
  g_cap.lastEdgeUs = static_cast<uint32_t>(esp_timer_get_time());
  if (!g_isrInstalled) {
    if (gpio_install_isr_service(ESP_INTR_FLAG_IRAM) == ESP_OK ||
        gpio_install_isr_service(0) == ESP_ERR_INVALID_STATE) {
      g_isrInstalled = true;
    }
  }
  gpio_set_intr_type(static_cast<gpio_num_t>(recvpin_), GPIO_INTR_ANYEDGE);
  gpio_isr_handler_remove(static_cast<gpio_num_t>(recvpin_));
  gpio_isr_handler_add(static_cast<gpio_num_t>(recvpin_), irGpioIsr, nullptr);
  g_cap.enabled = true;
  enabled_ = true;
}

void IRrecv::disableIRIn() {
  if (enabled_) {
    g_cap.enabled = false;
    gpio_isr_handler_remove(static_cast<gpio_num_t>(recvpin_));
  }
  enabled_ = false;
}

bool IRrecv::decode(decode_results* results) {
  if (!results || !g_cap.frameReady) return false;
  g_cap.enabled = false;
  const uint16_t readyLen = g_cap.readyLen;
  const uint16_t n = readyLen < kMaxRaw ? readyLen : kMaxRaw;
  results->rawlen = n;
  for (uint16_t i = 0; i < n; ++i) results->rawbuf[i] = g_cap.ready[i];
  decodeKnown(results);
  g_cap.frameReady = false;
  g_cap.enabled = enabled_;
  return n > 0;
}

void IRrecv::resume() {
  g_cap.frameReady = false;
  g_cap.pos = 0;
  g_cap.lastEdgeUs = static_cast<uint32_t>(esp_timer_get_time());
}

IRsend::IRsend(uint16_t sendpin) : sendpin_(sendpin) {}

void IRsend::begin() {
  pinMode(sendpin_, OUTPUT);
  digitalWrite(sendpin_, LOW);
}

void IRsend::mark(uint32_t usec) {
  const uint32_t period = 1000UL / std::max<uint16_t>(1, khz_);
  const uint32_t half = std::max<uint32_t>(1, period / 2);
  const uint32_t start = micros();
  while (micros() - start < usec) {
    digitalWrite(sendpin_, HIGH);
    delayMicroseconds(half);
    digitalWrite(sendpin_, LOW);
    delayMicroseconds(half);
  }
}

void IRsend::space(uint32_t usec) {
  digitalWrite(sendpin_, LOW);
  if (usec) delayMicroseconds(usec);
}

void IRsend::sendRaw(const uint16_t* buf, uint16_t len, uint16_t hz) {
  if (!buf || !len) return;
  khz_ = hz ? hz : 38;
  for (uint16_t i = 0; i < len; ++i) {
    if (i & 1) space(buf[i]);
    else mark(buf[i]);
  }
  space(0);
}

void IRsend::sendPulseDistance(uint64_t data, uint16_t nbits, uint32_t headerMark,
                               uint32_t headerSpace, uint32_t oneMark, uint32_t oneSpace,
                               uint32_t zeroMark, uint32_t zeroSpace, uint32_t footerMark,
                               bool msbFirst) {
  mark(headerMark);
  space(headerSpace);
  for (uint16_t i = 0; i < nbits; ++i) {
    const bool bit = msbFirst ? ((data >> (nbits - 1 - i)) & 1ULL) : ((data >> i) & 1ULL);
    mark(bit ? oneMark : zeroMark);
    space(bit ? oneSpace : zeroSpace);
  }
  if (footerMark) mark(footerMark);
  space(0);
}

void IRsend::sendManchester(uint64_t data, uint16_t nbits, uint32_t halfPeriod, bool startHigh) {
  bool level = startHigh;
  for (uint16_t i = 0; i < nbits; ++i) {
    const bool bit = (data >> (nbits - 1 - i)) & 1ULL;
    const bool first = bit ? !level : level;
    if (first) mark(halfPeriod); else space(halfPeriod);
    if (!first) mark(halfPeriod); else space(halfPeriod);
  }
  space(0);
}

bool IRsend::send(decode_type_t type, uint64_t data, uint16_t nbits, uint16_t repeat) {
  begin();
  uint16_t reps = repeat ? repeat : 1;
  switch (type) {
    case NEC:
    case NEC_LIKE:
      while (reps--) {
        sendPulseDistance(data, nbits ? nbits : 32, 9000, 4500, 560, 1690, 560, 560, 560, false);
        space(40000);
      }
      return true;
    case SONY:
    case SONY_38K:
      while (reps--) {
        sendPulseDistance(data, nbits ? nbits : 12, 2400, 600, 1200, 600, 600, 600, 0, false);
        space(45000);
      }
      return true;
    case SAMSUNG36:
      while (reps--) {
        sendPulseDistance(data, nbits ? nbits : 32, 4500, 4500, 560, 1600, 560, 560, 560, true);
        space(45000);
      }
      return true;
    case LG:
    case LG2:
      while (reps--) {
        sendPulseDistance(data, nbits ? nbits : 32, 8000, 4000, 560, 1690, 560, 560, 560, false);
        space(45000);
      }
      return true;
    case JVC:
      while (reps--) {
        sendPulseDistance(data, nbits ? nbits : 16, 8000, 4000, 526, 1574, 526, 526, 526, false);
        space(45000);
      }
      return true;
    case DENON:
      while (reps--) {
        sendPulseDistance(data, nbits ? nbits : 15, 260, 780, 260, 1820, 260, 780, 0, true);
        space(45000);
      }
      return true;
    case PANASONIC:
    case PIONEER:
      while (reps--) {
        sendPulseDistance(data, nbits ? nbits : 48, 3502, 1750, 502, 1244, 502, 400, 502, true);
        space(45000);
      }
      return true;
    case RC5:
      while (reps--) {
        sendManchester(data, nbits ? nbits : 14, 889, true);
        space(90000);
      }
      return true;
    case RC6:
      while (reps--) {
        mark(2666);
        space(889);
        sendManchester(data, nbits ? nbits : 20, 444, true);
        space(90000);
      }
      return true;
    case DISH:
      while (reps--) {
        sendPulseDistance(data, nbits ? nbits : 16, 400, 6100, 400, 1700, 400, 2800, 0, true);
        space(45000);
      }
      return true;
    case GICABLE:
    case EPSON:
      while (reps--) {
        sendPulseDistance(data, nbits ? nbits : 32, 9000, 4500, 560, 1680, 560, 560, 560, true);
        space(45000);
      }
      return true;
    default:
      return false;
  }
}
