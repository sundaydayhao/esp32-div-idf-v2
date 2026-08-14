#pragma once

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <string>

#include "div_platform.h"
#include "board_config.h"
#include "tft_setup.h"

#define TFT_BLACK       0x0000
#define TFT_NAVY        0x000F
#define TFT_DARKGREEN   0x03E0
#define TFT_DARKCYAN    0x03EF
#define TFT_MAROON      0x7800
#define TFT_PURPLE      0x780F
#define TFT_OLIVE       0x7BE0
#define TFT_LIGHTGREY   0xC618
#define TFT_DARKGREY    0x7BEF
#define TFT_BLUE        0x001F
#define TFT_GREEN       0x07E0
#define TFT_CYAN        0x07FF
#define TFT_RED         0xF800
#define TFT_MAGENTA     0xF81F
#define TFT_YELLOW      0xFFE0
#define TFT_WHITE       0xFFFF
#define TFT_ORANGE      0xFD20

#define TL_DATUM 0
#define TC_DATUM 1
#define TR_DATUM 2
#define ML_DATUM 3
#define MC_DATUM 4
#define MR_DATUM 5
#define BL_DATUM 6
#define BC_DATUM 7
#define BR_DATUM 8

#define ILI9341_VSCRDEF 0x33
#define ILI9341_VSCRSADD 0x37

class DivDisplay {
 public:
  DivDisplay();
  void init();
  void setRotation(uint8_t rotation);
  int16_t width() const { return width_; }
  int16_t height() const { return height_; }

  void fillScreen(uint16_t color);
  void fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color);
  void drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color);
  void drawRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t color);
  void fillRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t color);
  void drawFastHLine(int32_t x, int32_t y, int32_t w, uint16_t color);
  void drawFastVLine(int32_t x, int32_t y, int32_t h, uint16_t color);
  void drawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t color);
  void drawPixel(int32_t x, int32_t y, uint16_t color);
  void drawCircle(int32_t x0, int32_t y0, int32_t r, uint16_t color);
  void fillCircle(int32_t x0, int32_t y0, int32_t r, uint16_t color);
  void fillTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                    int32_t x2, int32_t y2, uint16_t color);
  void drawBitmap(int32_t x, int32_t y, const uint8_t* bitmap, int32_t w, int32_t h, uint16_t color);

  void setCursor(int32_t x, int32_t y);
  void setTextColor(uint16_t fg);
  void setTextColor(uint16_t fg, uint16_t bg);
  void setTextSize(uint8_t size);
  void setTextFont(uint8_t font);
  void setTextDatum(uint8_t datum);
  void setTextWrap(bool wrap);
  int16_t textWidth(const char* text, uint8_t font = 0) const;
  int16_t textWidth(const DivString& text, uint8_t font = 0) const { return textWidth(text.c_str(), font); }
  int16_t drawString(const char* text, int32_t x, int32_t y, uint8_t font = 0);
  int16_t drawString(const DivString& text, int32_t x, int32_t y, uint8_t font = 0) {
    return drawString(text.c_str(), x, y, font);
  }
  int16_t drawCentreString(const char* text, int32_t x, int32_t y, uint8_t font = 0);
  int16_t drawCentreString(const DivString& text, int32_t x, int32_t y, uint8_t font = 0) {
    return drawCentreString(text.c_str(), x, y, font);
  }
  int16_t drawNumber(long n, int32_t x, int32_t y, uint8_t font = 0);
  int16_t drawChar(char c, int32_t x, int32_t y, uint8_t font = 0);

  size_t print(const char* s);
  size_t print(const DivString& s) { return print(s.c_str()); }
  size_t print(char c);
  size_t print(int v);
  size_t print(unsigned int v);
  size_t print(long v);
  size_t print(unsigned long v);
  size_t print(float v);
  size_t print(float v, int decimals);
  size_t print(double v);
  size_t print(double v, int decimals);
  size_t println();
  template <typename T>
  size_t println(const T& v) {
    const size_t n = print(v);
    return n + println();
  }
  size_t printf(const char* fmt, ...);

  void startWrite() {}
  void endWrite() {}
  void writecommand(uint8_t cmd);
  void writedata(uint8_t data);

 protected:
  friend class DivSprite;
  virtual void writePixel(int32_t x, int32_t y, uint16_t color);
  virtual void writeFillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color);
  virtual void pushPixels(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t* pixels);

 private:
  void initPanel();
  void sendCommand(uint8_t cmd, const uint8_t* data = nullptr, size_t len = 0);
  void setAddrWindow(int32_t x, int32_t y, int32_t w, int32_t h);
  int16_t charWidth(uint8_t font) const;
  int16_t charHeight(uint8_t font) const;
  void drawGlyph(char c, int32_t x, int32_t y, uint8_t scale, uint16_t fg, uint16_t bg);

  int16_t width_;
  int16_t height_;
  uint8_t rotation_;
  uint16_t textFg_;
  uint16_t textBg_;
  uint8_t textSize_;
  uint8_t textFont_;
  uint8_t textDatum_;
  bool textWrap_;
  int32_t cursorX_;
  int32_t cursorY_;
};

class DivSprite {
 public:
  explicit DivSprite(DivDisplay* parent);
  ~DivSprite();
  bool createSprite(int16_t w, int16_t h);
  void deleteSprite();
  void setColorDepth(uint8_t bpp) { colorDepth_ = bpp; }
  void pushSprite(int32_t x, int32_t y);
  int16_t width() const { return width_; }
  int16_t height() const { return height_; }

  void fillScreen(uint16_t color);
  void fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color);
  void drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color);
  void drawRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t color);
  void fillRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t color);
  void drawBitmap(int32_t x, int32_t y, const uint8_t* bitmap, int32_t w, int32_t h, uint16_t color);
  void drawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t color);
  void drawCircle(int32_t x0, int32_t y0, int32_t r, uint16_t color);
  void fillCircle(int32_t x0, int32_t y0, int32_t r, uint16_t color);
  void fillTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                    int32_t x2, int32_t y2, uint16_t color);
  void drawString(const char* text, int32_t x, int32_t y, uint8_t font = 0);
  void drawString(const DivString& text, int32_t x, int32_t y, uint8_t font = 0) {
    drawString(text.c_str(), x, y, font);
  }
  void setTextColor(uint16_t fg);
  void setTextColor(uint16_t fg, uint16_t bg);
  void setTextSize(uint8_t size);
  void setTextFont(uint8_t font);
  void setTextDatum(uint8_t datum);

 private:
  void writePixel(int32_t x, int32_t y, uint16_t color);
  void writeFillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color);
  int16_t textWidth(const char* text, uint8_t font = 0) const;
  void drawGlyph(char c, int32_t x, int32_t y, uint8_t scale, uint16_t fg, uint16_t bg);

  DivDisplay* parent_;
  uint16_t* pixels_ = nullptr;
  size_t pixelCount_ = 0;
  int16_t width_ = 0;
  int16_t height_ = 0;
  uint8_t colorDepth_ = 16;
  uint16_t textFg_ = TFT_WHITE;
  uint16_t textBg_ = TFT_BLACK;
  uint8_t textSize_ = 1;
  uint8_t textFont_ = 1;
  uint8_t textDatum_ = TL_DATUM;
};
