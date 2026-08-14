#include "div_display.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <vector>

#include "board_pins.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_rom_sys.h"

namespace {
constexpr const char* kTag = "DivDisplay";
spi_device_handle_t g_tft = nullptr;
bool g_busReady = false;

uint16_t beColor(uint16_t color) {
  return static_cast<uint16_t>((color << 8) | (color >> 8));
}

void setDc(int level) {
  gpio_set_level(static_cast<gpio_num_t>(TFT_DC), level);
}

void sendBytes(const uint8_t* data, size_t len) {
  if (!g_tft || !data || len == 0) {
    return;
  }
  spi_transaction_t t = {};
  t.length = len * 8;
  t.tx_buffer = data;
  spi_device_polling_transmit(g_tft, &t);
}

void sendByte(uint8_t b) {
  sendBytes(&b, 1);
}

uint8_t glyphRow(char c, int row) {
  if (c < 32 || c > 126) {
    c = '?';
  }
  if (c == ' ') {
    return 0;
  }
  if (c >= '0' && c <= '9') {
    static const uint8_t digits[10][7] = {
        {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E},
        {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E},
        {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F},
        {0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E},
        {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02},
        {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E},
        {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E},
        {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},
        {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E},
        {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C},
    };
    return digits[c - '0'][row];
  }
  if (c >= 'a' && c <= 'z') {
    c = static_cast<char>(c - 'a' + 'A');
  }
  switch (c) {
    case 'A': { static const uint8_t r[7] = {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}; return r[row]; }
    case 'B': { static const uint8_t r[7] = {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}; return r[row]; }
    case 'C': { static const uint8_t r[7] = {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}; return r[row]; }
    case 'D': { static const uint8_t r[7] = {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E}; return r[row]; }
    case 'E': { static const uint8_t r[7] = {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}; return r[row]; }
    case 'F': { static const uint8_t r[7] = {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}; return r[row]; }
    case 'G': { static const uint8_t r[7] = {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F}; return r[row]; }
    case 'H': { static const uint8_t r[7] = {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}; return r[row]; }
    case 'I': { static const uint8_t r[7] = {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}; return r[row]; }
    case 'J': { static const uint8_t r[7] = {0x07,0x02,0x02,0x02,0x12,0x12,0x0C}; return r[row]; }
    case 'K': { static const uint8_t r[7] = {0x11,0x12,0x14,0x18,0x14,0x12,0x11}; return r[row]; }
    case 'L': { static const uint8_t r[7] = {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}; return r[row]; }
    case 'M': { static const uint8_t r[7] = {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}; return r[row]; }
    case 'N': { static const uint8_t r[7] = {0x11,0x19,0x15,0x13,0x11,0x11,0x11}; return r[row]; }
    case 'O': { static const uint8_t r[7] = {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}; return r[row]; }
    case 'P': { static const uint8_t r[7] = {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}; return r[row]; }
    case 'Q': { static const uint8_t r[7] = {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}; return r[row]; }
    case 'R': { static const uint8_t r[7] = {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}; return r[row]; }
    case 'S': { static const uint8_t r[7] = {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}; return r[row]; }
    case 'T': { static const uint8_t r[7] = {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}; return r[row]; }
    case 'U': { static const uint8_t r[7] = {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}; return r[row]; }
    case 'V': { static const uint8_t r[7] = {0x11,0x11,0x11,0x11,0x11,0x0A,0x04}; return r[row]; }
    case 'W': { static const uint8_t r[7] = {0x11,0x11,0x11,0x15,0x15,0x15,0x0A}; return r[row]; }
    case 'X': { static const uint8_t r[7] = {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}; return r[row]; }
    case 'Y': { static const uint8_t r[7] = {0x11,0x11,0x0A,0x04,0x04,0x04,0x04}; return r[row]; }
    case 'Z': { static const uint8_t r[7] = {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}; return r[row]; }
    case '.': { static const uint8_t r[7] = {0,0,0,0,0,0x0C,0x0C}; return r[row]; }
    case ',': { static const uint8_t r[7] = {0,0,0,0,0,0x0C,0x08}; return r[row]; }
    case ':': { static const uint8_t r[7] = {0,0x0C,0x0C,0,0x0C,0x0C,0}; return r[row]; }
    case ';': { static const uint8_t r[7] = {0,0x0C,0x0C,0,0x0C,0x08,0}; return r[row]; }
    case '-': { static const uint8_t r[7] = {0,0,0,0x1F,0,0,0}; return r[row]; }
    case '_': { static const uint8_t r[7] = {0,0,0,0,0,0,0x1F}; return r[row]; }
    case '/': { static const uint8_t r[7] = {0x01,0x02,0x02,0x04,0x08,0x08,0x10}; return r[row]; }
    case '\\':{ static const uint8_t r[7] = {0x10,0x08,0x08,0x04,0x02,0x02,0x01}; return r[row]; }
    case '+': { static const uint8_t r[7] = {0,0x04,0x04,0x1F,0x04,0x04,0}; return r[row]; }
    case '*': { static const uint8_t r[7] = {0,0x15,0x0E,0x1F,0x0E,0x15,0}; return r[row]; }
    case '!': { static const uint8_t r[7] = {0x04,0x04,0x04,0x04,0x04,0,0x04}; return r[row]; }
    case '?': { static const uint8_t r[7] = {0x0E,0x11,0x01,0x02,0x04,0,0x04}; return r[row]; }
    case '%': { static const uint8_t r[7] = {0x19,0x1A,0x02,0x04,0x08,0x0B,0x13}; return r[row]; }
    case '#': { static const uint8_t r[7] = {0x0A,0x0A,0x1F,0x0A,0x1F,0x0A,0x0A}; return r[row]; }
    case '[': { static const uint8_t r[7] = {0x0E,0x08,0x08,0x08,0x08,0x08,0x0E}; return r[row]; }
    case ']': { static const uint8_t r[7] = {0x0E,0x02,0x02,0x02,0x02,0x02,0x0E}; return r[row]; }
    case '(': { static const uint8_t r[7] = {0x02,0x04,0x08,0x08,0x08,0x04,0x02}; return r[row]; }
    case ')': { static const uint8_t r[7] = {0x08,0x04,0x02,0x02,0x02,0x04,0x08}; return r[row]; }
    case '<': { static const uint8_t r[7] = {0x02,0x04,0x08,0x10,0x08,0x04,0x02}; return r[row]; }
    case '>': { static const uint8_t r[7] = {0x08,0x04,0x02,0x01,0x02,0x04,0x08}; return r[row]; }
    default:  { static const uint8_t r[7] = {0x1F,0x11,0x15,0x11,0x15,0x11,0x1F}; return r[row]; }
  }
}
}  // namespace

DivDisplay::DivDisplay()
    : width_(TFT_WIDTH),
      height_(TFT_HEIGHT),
      rotation_(0),
      textFg_(TFT_WHITE),
      textBg_(TFT_BLACK),
      textSize_(1),
      textFont_(1),
      textDatum_(TL_DATUM),
      textWrap_(true),
      cursorX_(0),
      cursorY_(0) {}

void DivDisplay::init() {
  gpio_config_t out = {};
  out.mode = GPIO_MODE_OUTPUT;
  out.pin_bit_mask = (1ULL << TFT_DC) | (1ULL << TFT_CS);
#if TFT_RST >= 0
  out.pin_bit_mask |= (1ULL << TFT_RST);
#endif
#if TFT_BL >= 0
  out.pin_bit_mask |= (1ULL << TFT_BL);
#endif
  gpio_config(&out);
  gpio_set_level(static_cast<gpio_num_t>(TFT_CS), 1);
#if TFT_BL >= 0
  gpio_set_level(static_cast<gpio_num_t>(TFT_BL), TFT_BACKLIGHT_ON ? 1 : 0);
#endif

  spi_bus_config_t bus = {};
  bus.mosi_io_num = TFT_MOSI;
  bus.miso_io_num = TFT_MISO;
  bus.sclk_io_num = TFT_SCLK;
  bus.quadwp_io_num = -1;
  bus.quadhd_io_num = -1;
  bus.max_transfer_sz = TFT_WIDTH * 80 * 2;

  if (!g_busReady) {
    esp_err_t err = spi_bus_initialize(DIV_TFT_HOST, &bus, SPI_DMA_CH_AUTO);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
      ESP_LOGW(kTag, "TFT bus init failed: %s", esp_err_to_name(err));
      return;
    }
    g_busReady = true;
  }
  if (!g_tft) {
    spi_device_interface_config_t dev = {};
    dev.clock_speed_hz = SPI_FREQUENCY;
    dev.mode = 0;
    dev.spics_io_num = TFT_CS;
    dev.queue_size = 7;
    dev.flags = SPI_DEVICE_NO_DUMMY;
    ESP_ERROR_CHECK(spi_bus_add_device(DIV_TFT_HOST, &dev, &g_tft));
  }
  initPanel();
  setRotation(TFT_ROTATION);
  fillScreen(TFT_BLACK);
}

void DivDisplay::initPanel() {
#if TFT_RST >= 0
  gpio_set_level(static_cast<gpio_num_t>(TFT_RST), 0);
  esp_rom_delay_us(20000);
  gpio_set_level(static_cast<gpio_num_t>(TFT_RST), 1);
  esp_rom_delay_us(150000);
#endif
  sendCommand(0x01);
  delay(120);
  sendCommand(0x11);
  delay(120);
  const uint8_t pixFmt = 0x55;
  sendCommand(0x3A, &pixFmt, 1);
  const uint8_t mem = 0x40 | TFT_RGB_ORDER;
  sendCommand(0x36, &mem, 1);
#ifdef TFT_INVERSION_OFF
  sendCommand(0x20);
#else
  sendCommand(0x21);
#endif
  sendCommand(0x29);
  delay(20);
}

void DivDisplay::sendCommand(uint8_t cmd, const uint8_t* data, size_t len) {
  setDc(0);
  sendByte(cmd);
  if (data && len) {
    setDc(1);
    sendBytes(data, len);
  }
}

void DivDisplay::setRotation(uint8_t rotation) {
  rotation_ = rotation & 3;
  width_ = (rotation_ & 1) ? TFT_HEIGHT : TFT_WIDTH;
  height_ = (rotation_ & 1) ? TFT_WIDTH : TFT_HEIGHT;
  uint8_t madctl = 0x40 | TFT_RGB_ORDER;
  switch (rotation_) {
    case 1: madctl = 0x20 | TFT_RGB_ORDER; break;
    case 2: madctl = 0x80 | TFT_RGB_ORDER; break;
    case 3: madctl = 0xE0 | TFT_RGB_ORDER; break;
    default: madctl = 0x40 | TFT_RGB_ORDER; break;
  }
  sendCommand(0x36, &madctl, 1);
}

void DivDisplay::setAddrWindow(int32_t x, int32_t y, int32_t w, int32_t h) {
  const uint16_t x0 = x + TFT_COL_OFFSET;
  const uint16_t y0 = y + TFT_ROW_OFFSET;
  const uint16_t x1 = x + w - 1 + TFT_COL_OFFSET;
  const uint16_t y1 = y + h - 1 + TFT_ROW_OFFSET;
  uint8_t col[4] = {static_cast<uint8_t>(x0 >> 8), static_cast<uint8_t>(x0),
                    static_cast<uint8_t>(x1 >> 8), static_cast<uint8_t>(x1)};
  uint8_t row[4] = {static_cast<uint8_t>(y0 >> 8), static_cast<uint8_t>(y0),
                    static_cast<uint8_t>(y1 >> 8), static_cast<uint8_t>(y1)};
  sendCommand(0x2A, col, sizeof(col));
  sendCommand(0x2B, row, sizeof(row));
  sendCommand(0x2C);
}

void DivDisplay::fillScreen(uint16_t color) {
  fillRect(0, 0, width_, height_, color);
}

void DivDisplay::writeFillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color) {
  fillRect(x, y, w, h, color);
}

void DivDisplay::fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color) {
  if (w <= 0 || h <= 0 || x >= width_ || y >= height_) return;
  if (x == 0 && w == 240 && width_ > 240) {
    w = width_;
  }
  if (y >= 0 && h == 320 && height_ > 320) {
    h = height_ - y;
  }
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > width_) w = width_ - x;
  if (y + h > height_) h = height_ - y;
  if (w <= 0 || h <= 0) return;
  setAddrWindow(x, y, w, h);
  const uint16_t c = beColor(color);
  uint16_t line[128];
  std::fill(std::begin(line), std::end(line), c);
  setDc(1);
  int32_t remaining = w * h;
  while (remaining > 0) {
    const int32_t n = std::min<int32_t>(remaining, static_cast<int32_t>(std::size(line)));
    sendBytes(reinterpret_cast<const uint8_t*>(line), n * 2);
    remaining -= n;
  }
}

void DivDisplay::drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color) {
  drawFastHLine(x, y, w, color);
  drawFastHLine(x, y + h - 1, w, color);
  drawFastVLine(x, y, h, color);
  drawFastVLine(x + w - 1, y, h, color);
}

void DivDisplay::drawRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t, uint16_t color) {
  drawRect(x, y, w, h, color);
}

void DivDisplay::fillRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t, uint16_t color) {
  fillRect(x, y, w, h, color);
}

void DivDisplay::drawFastHLine(int32_t x, int32_t y, int32_t w, uint16_t color) {
  fillRect(x, y, w, 1, color);
}

void DivDisplay::drawFastVLine(int32_t x, int32_t y, int32_t h, uint16_t color) {
  fillRect(x, y, 1, h, color);
}

void DivDisplay::writePixel(int32_t x, int32_t y, uint16_t color) {
  drawPixel(x, y, color);
}

void DivDisplay::drawPixel(int32_t x, int32_t y, uint16_t color) {
  if (x < 0 || y < 0 || x >= width_ || y >= height_) return;
  const uint16_t c = beColor(color);
  setAddrWindow(x, y, 1, 1);
  setDc(1);
  sendBytes(reinterpret_cast<const uint8_t*>(&c), 2);
}

void DivDisplay::drawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t color) {
  if (y0 == y1 && x0 == 0 && x1 == 240 && width_ > 240) {
    x1 = width_ - 1;
  }
  if (x0 == x1 && y0 == 0 && y1 == 320 && height_ > 320) {
    y1 = height_ - 1;
  }
  const int32_t dx = std::abs(x1 - x0);
  const int32_t sx = x0 < x1 ? 1 : -1;
  const int32_t dy = -std::abs(y1 - y0);
  const int32_t sy = y0 < y1 ? 1 : -1;
  int32_t err = dx + dy;
  while (true) {
    drawPixel(x0, y0, color);
    if (x0 == x1 && y0 == y1) break;
    const int32_t e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}

void DivDisplay::drawCircle(int32_t x0, int32_t y0, int32_t r, uint16_t color) {
  int32_t f = 1 - r, ddF_x = 1, ddF_y = -2 * r, x = 0, y = r;
  drawPixel(x0, y0 + r, color); drawPixel(x0, y0 - r, color);
  drawPixel(x0 + r, y0, color); drawPixel(x0 - r, y0, color);
  while (x < y) {
    if (f >= 0) { y--; ddF_y += 2; f += ddF_y; }
    x++; ddF_x += 2; f += ddF_x;
    drawPixel(x0 + x, y0 + y, color); drawPixel(x0 - x, y0 + y, color);
    drawPixel(x0 + x, y0 - y, color); drawPixel(x0 - x, y0 - y, color);
    drawPixel(x0 + y, y0 + x, color); drawPixel(x0 - y, y0 + x, color);
    drawPixel(x0 + y, y0 - x, color); drawPixel(x0 - y, y0 - x, color);
  }
}

void DivDisplay::fillCircle(int32_t x0, int32_t y0, int32_t r, uint16_t color) {
  for (int32_t y = -r; y <= r; ++y) {
    const int32_t dx = static_cast<int32_t>(std::sqrt(static_cast<float>(r * r - y * y)));
    drawFastHLine(x0 - dx, y0 + y, dx * 2 + 1, color);
  }
}

void DivDisplay::fillTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                              int32_t x2, int32_t y2, uint16_t color) {
  if (y0 > y1) { std::swap(y0, y1); std::swap(x0, x1); }
  if (y1 > y2) { std::swap(y2, y1); std::swap(x2, x1); }
  if (y0 > y1) { std::swap(y0, y1); std::swap(x0, x1); }
  auto interp = [](int32_t xA, int32_t yA, int32_t xB, int32_t yB, int32_t y) {
    return yB == yA ? xA : xA + (xB - xA) * (y - yA) / (yB - yA);
  };
  for (int32_t y = y0; y <= y2; ++y) {
    const bool lower = y > y1;
    const int32_t xa = interp(x0, y0, x2, y2, y);
    const int32_t xb = lower ? interp(x1, y1, x2, y2, y) : interp(x0, y0, x1, y1, y);
    drawFastHLine(std::min(xa, xb), y, std::abs(xa - xb) + 1, color);
  }
}

void DivDisplay::drawBitmap(int32_t x, int32_t y, const uint8_t* bitmap, int32_t w, int32_t h, uint16_t color) {
  if (!bitmap) return;
  const int32_t byteWidth = (w + 7) / 8;
  for (int32_t yy = 0; yy < h; ++yy) {
    for (int32_t xx = 0; xx < w; ++xx) {
      if (bitmap[yy * byteWidth + xx / 8] & (0x80 >> (xx & 7))) {
        drawPixel(x + xx, y + yy, color);
      }
    }
  }
}

void DivDisplay::setCursor(int32_t x, int32_t y) { cursorX_ = x; cursorY_ = y; }
void DivDisplay::setTextColor(uint16_t fg) { textFg_ = fg; textBg_ = fg; }
void DivDisplay::setTextColor(uint16_t fg, uint16_t bg) { textFg_ = fg; textBg_ = bg; }
void DivDisplay::setTextSize(uint8_t size) { textSize_ = std::max<uint8_t>(1, size); }
void DivDisplay::setTextFont(uint8_t font) { textFont_ = font; }
void DivDisplay::setTextDatum(uint8_t datum) { textDatum_ = datum; }
void DivDisplay::setTextWrap(bool wrap) { textWrap_ = wrap; }

int16_t DivDisplay::charWidth(uint8_t font) const {
  const uint8_t scale = std::max<uint8_t>(textSize_, font > 1 ? 2 : 1);
  return 6 * scale;
}

int16_t DivDisplay::charHeight(uint8_t font) const {
  const uint8_t scale = std::max<uint8_t>(textSize_, font > 1 ? 2 : 1);
  return 8 * scale;
}

int16_t DivDisplay::textWidth(const char* text, uint8_t font) const {
  return text ? static_cast<int16_t>(std::strlen(text) * charWidth(font ? font : textFont_)) : 0;
}

void DivDisplay::drawGlyph(char c, int32_t x, int32_t y, uint8_t scale, uint16_t fg, uint16_t bg) {
  for (int row = 0; row < 7; ++row) {
    const uint8_t bits = glyphRow(c, row);
    for (int col = 0; col < 5; ++col) {
      const bool on = bits & (1 << (4 - col));
      const uint16_t pix = on ? fg : bg;
      if (pix != bg || fg != bg) {
        fillRect(x + col * scale, y + row * scale, scale, scale, pix);
      }
    }
  }
  if (fg != bg) {
    fillRect(x + 5 * scale, y, scale, 8 * scale, bg);
  }
}

int16_t DivDisplay::drawString(const char* text, int32_t x, int32_t y, uint8_t font) {
  if (!text) return 0;
  const uint8_t useFont = font ? font : textFont_;
  const uint8_t scale = std::max<uint8_t>(textSize_, useFont > 1 ? 2 : 1);
  const int16_t w = textWidth(text, useFont);
  const int16_t h = charHeight(useFont);
  if (textDatum_ == TC_DATUM || textDatum_ == MC_DATUM || textDatum_ == BC_DATUM) x -= w / 2;
  if (textDatum_ == TR_DATUM || textDatum_ == MR_DATUM || textDatum_ == BR_DATUM) x -= w;
  if (textDatum_ == ML_DATUM || textDatum_ == MC_DATUM || textDatum_ == MR_DATUM) y -= h / 2;
  if (textDatum_ == BL_DATUM || textDatum_ == BC_DATUM || textDatum_ == BR_DATUM) y -= h;
  int32_t cx = x;
  for (const char* p = text; *p; ++p) {
    if (*p == '\n') { cx = x; y += h; continue; }
    drawGlyph(*p, cx, y, scale, textFg_, textBg_);
    cx += 6 * scale;
  }
  return w;
}

int16_t DivDisplay::drawCentreString(const char* text, int32_t x, int32_t y, uint8_t font) {
  const uint8_t oldDatum = textDatum_;
  textDatum_ = TC_DATUM;
  const int16_t w = drawString(text, x, y, font);
  textDatum_ = oldDatum;
  return w;
}

int16_t DivDisplay::drawNumber(long n, int32_t x, int32_t y, uint8_t font) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%ld", n);
  return drawString(buf, x, y, font);
}

int16_t DivDisplay::drawChar(char c, int32_t x, int32_t y, uint8_t font) {
  char s[2] = {c, 0};
  return drawString(s, x, y, font);
}

size_t DivDisplay::print(const char* s) {
  if (!s) return 0;
  size_t written = 0;
  for (const char* p = s; *p; ++p) {
    if (*p == '\n') {
      cursorX_ = 0;
      cursorY_ += charHeight(textFont_);
    } else {
      drawChar(*p, cursorX_, cursorY_, textFont_);
      cursorX_ += charWidth(textFont_);
      if (textWrap_ && cursorX_ + charWidth(textFont_) > width_) {
        cursorX_ = 0;
        cursorY_ += charHeight(textFont_);
      }
    }
    ++written;
  }
  return written;
}

size_t DivDisplay::print(char c) { char s[2] = {c, 0}; return print(s); }
size_t DivDisplay::print(int v) { char b[32]; std::snprintf(b, sizeof(b), "%d", v); return print(b); }
size_t DivDisplay::print(unsigned int v) { char b[32]; std::snprintf(b, sizeof(b), "%u", v); return print(b); }
size_t DivDisplay::print(long v) { char b[32]; std::snprintf(b, sizeof(b), "%ld", v); return print(b); }
size_t DivDisplay::print(unsigned long v) { char b[32]; std::snprintf(b, sizeof(b), "%lu", v); return print(b); }
size_t DivDisplay::print(float v) { return print(static_cast<double>(v), 2); }
size_t DivDisplay::print(float v, int decimals) { return print(static_cast<double>(v), decimals); }
size_t DivDisplay::print(double v) { return print(v, 2); }
size_t DivDisplay::print(double v, int decimals) {
  char fmt[12], b[48];
  if (decimals < 0) decimals = 2;
  if (decimals > 8) decimals = 8;
  std::snprintf(fmt, sizeof(fmt), "%%.%df", decimals);
  std::snprintf(b, sizeof(b), fmt, v);
  return print(b);
}
size_t DivDisplay::println() { return print("\n"); }

size_t DivDisplay::printf(const char* fmt, ...) {
  char buf[192];
  va_list ap;
  va_start(ap, fmt);
  const int n = std::vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  if (n <= 0) return 0;
  if (static_cast<size_t>(n) < sizeof(buf)) return print(buf);
  std::vector<char> dyn(static_cast<size_t>(n) + 1);
  va_start(ap, fmt);
  std::vsnprintf(dyn.data(), dyn.size(), fmt, ap);
  va_end(ap);
  return print(dyn.data());
}

void DivDisplay::writecommand(uint8_t cmd) { sendCommand(cmd); }
void DivDisplay::writedata(uint8_t data) { setDc(1); sendByte(data); }

void DivDisplay::pushPixels(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t* pixels) {
  if (!pixels || w <= 0 || h <= 0) return;
  if (x < 0 || y < 0 || x + w > width_ || y + h > height_) {
    for (int32_t yy = 0; yy < h; ++yy) {
      for (int32_t xx = 0; xx < w; ++xx) {
        drawPixel(x + xx, y + yy, pixels[yy * w + xx]);
      }
    }
    return;
  }
  setAddrWindow(x, y, w, h);
  std::vector<uint16_t> line(w);
  setDc(1);
  for (int32_t yy = 0; yy < h; ++yy) {
    for (int32_t xx = 0; xx < w; ++xx) line[xx] = beColor(pixels[yy * w + xx]);
    sendBytes(reinterpret_cast<const uint8_t*>(line.data()), line.size() * 2);
  }
}

DivSprite::DivSprite(DivDisplay* parent) : parent_(parent) {}

DivSprite::~DivSprite() {
  deleteSprite();
}

bool DivSprite::createSprite(int16_t w, int16_t h) {
  if (w <= 0 || h <= 0) return false;
  const size_t count = static_cast<size_t>(w) * static_cast<size_t>(h);
  if (count > (SIZE_MAX / sizeof(uint16_t))) {
    ESP_LOGW(kTag, "sprite size overflow: %dx%d", (int)w, (int)h);
    return false;
  }
  const size_t bytes = count * sizeof(uint16_t);
  const size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  if (largest < bytes) {
    ESP_LOGW(kTag, "sprite alloc skipped: %dx%d needs %u bytes, largest block %u",
             (int)w, (int)h, (unsigned)bytes, (unsigned)largest);
    return false;
  }

  uint16_t* next = static_cast<uint16_t*>(heap_caps_malloc(bytes, MALLOC_CAP_8BIT));
  if (!next) {
    ESP_LOGW(kTag, "sprite alloc failed: %dx%d needs %u bytes, largest block %u",
             (int)w, (int)h, (unsigned)bytes, (unsigned)largest);
    return false;
  }

  deleteSprite();
  width_ = w;
  height_ = h;
  pixels_ = next;
  pixelCount_ = count;
  std::fill_n(pixels_, pixelCount_, TFT_BLACK);
  return true;
}

void DivSprite::deleteSprite() {
  if (pixels_) {
    heap_caps_free(pixels_);
    pixels_ = nullptr;
  }
  pixelCount_ = 0;
  width_ = height_ = 0;
}

void DivSprite::pushSprite(int32_t x, int32_t y) {
  if (parent_ && pixels_) parent_->pushPixels(x, y, width_, height_, pixels_);
}

void DivSprite::writePixel(int32_t x, int32_t y, uint16_t color) {
  if (x < 0 || y < 0 || x >= width_ || y >= height_ || !pixels_) return;
  pixels_[static_cast<size_t>(y) * width_ + x] = color;
}

void DivSprite::writeFillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color) {
  if (w <= 0 || h <= 0 || !pixels_) return;
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > width_) w = width_ - x;
  if (y + h > height_) h = height_ - y;
  if (w <= 0 || h <= 0) return;
  for (int32_t yy = 0; yy < h; ++yy) {
    std::fill_n(&pixels_[static_cast<size_t>(y + yy) * width_ + x], w, color);
  }
}

void DivSprite::fillScreen(uint16_t color) {
  if (pixels_) std::fill_n(pixels_, pixelCount_, color);
}
void DivSprite::fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color) { writeFillRect(x, y, w, h, color); }
void DivSprite::drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color) {
  drawLine(x, y, x + w - 1, y, color); drawLine(x, y + h - 1, x + w - 1, y + h - 1, color);
  drawLine(x, y, x, y + h - 1, color); drawLine(x + w - 1, y, x + w - 1, y + h - 1, color);
}
void DivSprite::drawRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t, uint16_t color) { drawRect(x, y, w, h, color); }
void DivSprite::fillRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t, uint16_t color) { fillRect(x, y, w, h, color); }
void DivSprite::drawBitmap(int32_t x, int32_t y, const uint8_t* bitmap, int32_t w, int32_t h, uint16_t color) {
  if (!bitmap || !pixels_) return;
  const int32_t byteWidth = (w + 7) / 8;
  for (int32_t yy = 0; yy < h; ++yy) {
    for (int32_t xx = 0; xx < w; ++xx) {
      if (bitmap[yy * byteWidth + xx / 8] & (0x80 >> (xx & 7))) {
        writePixel(x + xx, y + yy, color);
      }
    }
  }
}
void DivSprite::drawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t color) {
  const int32_t dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  const int32_t dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
  int32_t err = dx + dy;
  while (true) {
    writePixel(x0, y0, color);
    if (x0 == x1 && y0 == y1) break;
    const int32_t e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}
void DivSprite::drawCircle(int32_t x0, int32_t y0, int32_t r, uint16_t color) {
  for (int32_t a = 0; a < 360; a += 5) {
    const float rad = a * 3.1415926f / 180.0f;
    writePixel(x0 + static_cast<int32_t>(std::cos(rad) * r), y0 + static_cast<int32_t>(std::sin(rad) * r), color);
  }
}
void DivSprite::fillCircle(int32_t x0, int32_t y0, int32_t r, uint16_t color) {
  for (int32_t y = -r; y <= r; ++y) {
    const int32_t dx = static_cast<int32_t>(std::sqrt(static_cast<float>(r * r - y * y)));
    writeFillRect(x0 - dx, y0 + y, dx * 2 + 1, 1, color);
  }
}
void DivSprite::fillTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                             int32_t x2, int32_t y2, uint16_t color) {
  if (y0 > y1) { std::swap(y0, y1); std::swap(x0, x1); }
  if (y1 > y2) { std::swap(y2, y1); std::swap(x2, x1); }
  if (y0 > y1) { std::swap(y0, y1); std::swap(x0, x1); }
  auto interp = [](int32_t xA, int32_t yA, int32_t xB, int32_t yB, int32_t y) {
    return yB == yA ? xA : xA + (xB - xA) * (y - yA) / (yB - yA);
  };
  for (int32_t y = y0; y <= y2; ++y) {
    const bool lower = y > y1;
    const int32_t xa = interp(x0, y0, x2, y2, y);
    const int32_t xb = lower ? interp(x1, y1, x2, y2, y) : interp(x0, y0, x1, y1, y);
    writeFillRect(std::min(xa, xb), y, std::abs(xa - xb) + 1, 1, color);
  }
}
int16_t DivSprite::textWidth(const char* text, uint8_t font) const {
  const uint8_t scale = std::max<uint8_t>(textSize_, font > 1 ? 2 : 1);
  return text ? std::strlen(text) * 6 * scale : 0;
}
void DivSprite::drawGlyph(char c, int32_t x, int32_t y, uint8_t scale, uint16_t fg, uint16_t bg) {
  for (int row = 0; row < 7; ++row) {
    const uint8_t bits = glyphRow(c, row);
    for (int col = 0; col < 5; ++col) {
      writeFillRect(x + col * scale, y + row * scale, scale, scale, (bits & (1 << (4 - col))) ? fg : bg);
    }
  }
}
void DivSprite::drawString(const char* text, int32_t x, int32_t y, uint8_t font) {
  if (!text) return;
  const uint8_t scale = std::max<uint8_t>(textSize_, font > 1 ? 2 : 1);
  const int16_t w = textWidth(text, font);
  if (textDatum_ == TC_DATUM || textDatum_ == MC_DATUM || textDatum_ == BC_DATUM) x -= w / 2;
  for (const char* p = text; *p; ++p) {
    drawGlyph(*p, x, y, scale, textFg_, textBg_);
    x += 6 * scale;
  }
}
void DivSprite::setTextColor(uint16_t fg) { textFg_ = fg; textBg_ = fg; }
void DivSprite::setTextColor(uint16_t fg, uint16_t bg) { textFg_ = fg; textBg_ = bg; }
void DivSprite::setTextSize(uint8_t size) { textSize_ = std::max<uint8_t>(1, size); }
void DivSprite::setTextFont(uint8_t font) { textFont_ = font; }
void DivSprite::setTextDatum(uint8_t datum) { textDatum_ = datum; }
