#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <lvgl.h>
#include <WiFi.h>
#include <WiFiManager.h>

#include <cstring>
#include <initializer_list>
#include <ctime>

#include "board_pins.h"
#include "ai_bridge.h"

namespace {

constexpr uint8_t LCD_SWRESET = 0x01;
constexpr uint8_t LCD_SLPOUT = 0x11;
constexpr uint8_t LCD_COLMOD = 0x3A;
constexpr uint8_t LCD_MADCTL = 0x36;
constexpr uint8_t LCD_CASET = 0x2A;
constexpr uint8_t LCD_RASET = 0x2B;
constexpr uint8_t LCD_INVON = 0x21;
constexpr uint8_t LCD_NORON = 0x13;
constexpr uint8_t LCD_DISPON = 0x29;
constexpr uint8_t LCD_RAMWR = 0x2C;

constexpr int16_t SCREEN_WIDTH = 320;
constexpr int16_t SCREEN_HEIGHT = 240;
constexpr uint32_t FOCUS_DURATION_SECONDS = 25U * 60U;

constexpr lv_color_t COLOR_BG = LV_COLOR_MAKE(0x0E, 0x10, 0x16);
constexpr lv_color_t COLOR_PANEL = LV_COLOR_MAKE(0x17, 0x1A, 0x24);
constexpr lv_color_t COLOR_PANEL_ALT = LV_COLOR_MAKE(0x1D, 0x20, 0x2C);
constexpr lv_color_t COLOR_LINE = LV_COLOR_MAKE(0x2A, 0x2E, 0x3A);
constexpr lv_color_t COLOR_TEXT = LV_COLOR_MAKE(0xF4, 0xF5, 0xF7);
constexpr lv_color_t COLOR_MUTED = LV_COLOR_MAKE(0xA6, 0xAC, 0xBC);
constexpr lv_color_t COLOR_SOFT = LV_COLOR_MAKE(0x74, 0x7C, 0x8E);
constexpr lv_color_t COLOR_PRIMARY = LV_COLOR_MAKE(0x8B, 0x86, 0xD9);
constexpr lv_color_t COLOR_PRIMARY_LIGHT = LV_COLOR_MAKE(0xAA, 0xA5, 0xF0);
constexpr lv_color_t COLOR_BLUE = LV_COLOR_MAKE(0x6C, 0x9B, 0xE8);
constexpr lv_color_t COLOR_SUCCESS = LV_COLOR_MAKE(0x80, 0xC9, 0xA3);
constexpr lv_color_t COLOR_WARNING = LV_COLOR_MAKE(0xE8, 0xB5, 0x6F);
constexpr lv_color_t COLOR_ERROR = LV_COLOR_MAKE(0xE3, 0x7C, 0x87);

class XL9555 {
 public:
  bool begin() {
    Wire.beginTransmission(ADDR_XL9555);
    if (Wire.endTransmission() != 0) return false;

    output0_ = static_cast<uint8_t>(1U << XL_PIN_BEEP);
    output1_ = 0x00;
    writeRegister(0x02, output0_);
    writeRegister(0x03, output1_);

    config0_ = static_cast<uint8_t>(1U << XL_PIN_BEEP);
    config1_ = static_cast<uint8_t>((1U << (XL_PIN_KEY_AI - 8)) |
                                    (1U << (XL_PIN_KEY_FOCUS - 8)) |
                                    (1U << (XL_PIN_KEY_TOOLS - 8)) |
                                    (1U << (XL_PIN_KEY_MENU - 8)));
    writeRegister(0x06, config0_);
    writeRegister(0x07, config1_);
    return true;
  }

  void writePin(uint8_t pin, bool high) {
    if (pin < 8) {
      if (high) output0_ |= (1U << pin);
      else output0_ &= ~(1U << pin);
      writeRegister(0x02, output0_);
    } else {
      const uint8_t bit = pin - 8;
      if (high) output1_ |= (1U << bit);
      else output1_ &= ~(1U << bit);
      writeRegister(0x03, output1_);
    }
  }

  uint16_t readInputs() {
    Wire.beginTransmission(ADDR_XL9555);
    Wire.write(0x00);
    if (Wire.endTransmission(false) != 0) return 0xFFFF;
    if (Wire.requestFrom(static_cast<int>(ADDR_XL9555), 2) != 2) {
      return 0xFFFF;
    }
    const uint8_t low = Wire.read();
    const uint8_t high = Wire.read();
    return static_cast<uint16_t>(low | (static_cast<uint16_t>(high) << 8));
  }

 private:
  void writeRegister(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(ADDR_XL9555);
    Wire.write(reg);
    Wire.write(value);
    Wire.endTransmission();
  }

  uint8_t output0_ = 0;
  uint8_t output1_ = 0;
  uint8_t config0_ = 0xFF;
  uint8_t config1_ = 0xFF;
};

class RawST7789 {
 public:
  void begin() {
    pinMode(PIN_LCD_CS, OUTPUT);
    pinMode(PIN_LCD_DC, OUTPUT);
    digitalWrite(PIN_LCD_CS, HIGH);
    digitalWrite(PIN_LCD_DC, HIGH);
    SPI.begin(PIN_LCD_SCLK, -1, PIN_LCD_MOSI, PIN_LCD_CS);

    sendCommand(LCD_SWRESET);
    delay(150);
    sendCommand(LCD_SLPOUT);
    delay(120);
    sendCommand(LCD_COLMOD);
    sendData8(0x55);
    sendCommand(LCD_MADCTL);
    sendData8(0x60);
    sendCommand(0xB2);
    sendData({0x0C, 0x0C, 0x00, 0x33, 0x33});
    sendCommand(0xB7);
    sendData8(0x35);
    sendCommand(0xBB);
    sendData8(0x19);
    sendCommand(0xC0);
    sendData8(0x2C);
    sendCommand(0xC2);
    sendData8(0x01);
    sendCommand(0xC3);
    sendData8(0x12);
    sendCommand(0xC4);
    sendData8(0x20);
    sendCommand(0xC6);
    sendData8(0x0F);
    sendCommand(0xD0);
    sendData({0xA4, 0xA1});
    sendCommand(0xE0);
    sendData({0xD0, 0x04, 0x0D, 0x11, 0x13, 0x2B, 0x3F, 0x54,
              0x4C, 0x18, 0x0D, 0x0B, 0x1F, 0x23});
    sendCommand(0xE1);
    sendData({0xD0, 0x04, 0x0C, 0x11, 0x13, 0x2C, 0x3F, 0x44,
              0x51, 0x2F, 0x1F, 0x1F, 0x20, 0x23});
    sendCommand(LCD_INVON);
    sendCommand(LCD_NORON);
    delay(10);
    sendCommand(LCD_DISPON);
    delay(100);
  }

  void pushPixels(int16_t x, int16_t y, int16_t width, int16_t height,
                  const uint16_t *pixels) {
    if (!pixels || width <= 0 || height <= 0) return;
    startWrite();
    setWindow(x, y, x + width - 1, y + height - 1);
    const uint32_t count = static_cast<uint32_t>(width) *
                           static_cast<uint32_t>(height);
    for (uint32_t index = 0; index < count; ++index) {
      SPI.transfer16(pixels[index]);
    }
    endWrite();
  }

 private:
  void startWrite() {
    SPI.beginTransaction(SPISettings(40000000, MSBFIRST, SPI_MODE0));
    digitalWrite(PIN_LCD_CS, LOW);
  }

  void endWrite() {
    digitalWrite(PIN_LCD_CS, HIGH);
    SPI.endTransaction();
  }

  void sendCommand(uint8_t command) {
    SPI.beginTransaction(SPISettings(40000000, MSBFIRST, SPI_MODE0));
    digitalWrite(PIN_LCD_CS, LOW);
    digitalWrite(PIN_LCD_DC, LOW);
    SPI.transfer(command);
    digitalWrite(PIN_LCD_CS, HIGH);
    SPI.endTransaction();
  }

  void sendData8(uint8_t value) {
    SPI.beginTransaction(SPISettings(40000000, MSBFIRST, SPI_MODE0));
    digitalWrite(PIN_LCD_CS, LOW);
    digitalWrite(PIN_LCD_DC, HIGH);
    SPI.transfer(value);
    digitalWrite(PIN_LCD_CS, HIGH);
    SPI.endTransaction();
  }

  void sendData(std::initializer_list<uint8_t> bytes) {
    SPI.beginTransaction(SPISettings(40000000, MSBFIRST, SPI_MODE0));
    digitalWrite(PIN_LCD_CS, LOW);
    digitalWrite(PIN_LCD_DC, HIGH);
    for (const uint8_t byte : bytes) SPI.transfer(byte);
    digitalWrite(PIN_LCD_CS, HIGH);
    SPI.endTransaction();
  }

  void setWindow(int16_t x0, int16_t y0, int16_t x1, int16_t y1) {
    digitalWrite(PIN_LCD_DC, LOW);
    SPI.transfer(LCD_CASET);
    digitalWrite(PIN_LCD_DC, HIGH);
    SPI.transfer16(static_cast<uint16_t>(x0));
    SPI.transfer16(static_cast<uint16_t>(x1));
    digitalWrite(PIN_LCD_DC, LOW);
    SPI.transfer(LCD_RASET);
    digitalWrite(PIN_LCD_DC, HIGH);
    SPI.transfer16(static_cast<uint16_t>(y0));
    SPI.transfer16(static_cast<uint16_t>(y1));
    digitalWrite(PIN_LCD_DC, LOW);
    SPI.transfer(LCD_RAMWR);
    digitalWrite(PIN_LCD_DC, HIGH);
  }
};

enum UiPage : uint8_t {
  PAGE_AI = 0,
  PAGE_FOCUS = 1,
  PAGE_TOOLS = 2,
};

enum AiState : uint8_t {
  AI_READY = 0,
  AI_LISTENING = 1,
  AI_THINKING = 2,
  AI_ERROR = 3,
};

XL9555 expander;
RawST7789 lcd;
lv_disp_draw_buf_t drawBuffer;
lv_disp_drv_t displayDriver;
lv_color_t displayBuffer[SCREEN_WIDTH * 20];

UiPage currentPage = PAGE_AI;
AiState aiState = AI_READY;
bool displayReady = false;
uint32_t lastLvglTick = 0;
uint32_t lastKeyScan = 0;
uint16_t lastPressedKeys = 0;
uint32_t focusRemainingSeconds = FOCUS_DURATION_SECONDS;
uint32_t focusLastTickMs = 0;
uint32_t menuPressedAtMs = 0;
uint32_t lastToolsUpdateMs = 0;
bool focusRunning = false;
bool focusHasStarted = false;
bool menuPressedOnActionPage = false;
bool menuLongPressHandled = false;
bool timeSyncStarted = false;
bool wifiEverConnected = false;
char wifiPortalSsid[16] = {};
char wifiPortalPassword[16] = {};
const char wifiInfoConfigLink[] = R"rawliteral(<script>
document.addEventListener('DOMContentLoaded',function(){
  if(window.location.pathname!=='/info')return;
  var link=document.createElement('a');
  link.href='/wifi';
  link.textContent='Configure Wi-Fi';
  link.style.cssText='display:block;margin:12px 0;padding:10px;border-radius:5px;background:#1fa3ec;color:#fff;text-align:center;font-weight:bold;text-decoration:none';
  var wrap=document.querySelector('.wrap')||document.body;
  wrap.insertBefore(link,wrap.firstChild);
});
</script>)rawliteral";

WiFiManager wifiManager;
lv_obj_t *aiPage = nullptr;
lv_obj_t *focusPage = nullptr;
lv_obj_t *toolsPage = nullptr;
lv_obj_t *navActive = nullptr;
lv_obj_t *navButtons[3] = {nullptr, nullptr, nullptr};
lv_obj_t *aiStateLabel = nullptr;
lv_obj_t *aiSubtitle = nullptr;
lv_obj_t *aiMessage = nullptr;
lv_obj_t *aiInputHint = nullptr;
lv_obj_t *aiHeaderStatus = nullptr;
lv_obj_t *aiOrbGlow = nullptr;
lv_obj_t *aiOrbOuter = nullptr;
lv_obj_t *aiOrbRing = nullptr;
lv_obj_t *aiOrbInner = nullptr;
lv_obj_t *aiOrbCore = nullptr;
lv_obj_t *focusHeaderStatusLabel = nullptr;
lv_obj_t *focusTimeLabel = nullptr;
lv_obj_t *focusStatusLabel = nullptr;
lv_obj_t *focusProgressFill = nullptr;
lv_obj_t *focusButton = nullptr;
lv_obj_t *toolsHeaderStatusLabel = nullptr;
lv_obj_t *toolsHeaderDot = nullptr;
lv_obj_t *toolTimeLabel = nullptr;
lv_obj_t *wifiNameLabel = nullptr;
lv_obj_t *wifiValueLabel = nullptr;

constexpr size_t kChatHistoryLength = 320;
char chatHistory[kChatHistoryLength] = "How can I help?";
char serialPrompt[192] = {};
size_t serialPromptLength = 0;
bool aiAnswerStreaming = false;
bool aiAnswerHasStarted = false;
uint32_t aiLastChunkMs = 0;
bool aiInputHintWaiting = false;

lv_color_t stateMainColor() {
  if (aiState == AI_LISTENING) return COLOR_ERROR;
  if (aiState == AI_THINKING) return COLOR_BLUE;
  if (aiState == AI_ERROR) return COLOR_WARNING;
  return COLOR_PRIMARY;
}

lv_color_t stateCoreColor() {
  if (aiState == AI_LISTENING) return COLOR_ERROR;
  if (aiState == AI_ERROR) return COLOR_WARNING;
  return COLOR_PRIMARY_LIGHT;
}

void clearObject(lv_obj_t *object) {
  lv_obj_remove_style_all(object);
  lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLLABLE);
}

lv_obj_t *makePanel(lv_obj_t *parent, int16_t x, int16_t y, int16_t width,
                    int16_t height, lv_color_t fill, lv_color_t border,
                    uint16_t radius = 8, bool shadow = true) {
  lv_obj_t *panel = lv_obj_create(parent);
  clearObject(panel);
  lv_obj_set_pos(panel, x, y);
  lv_obj_set_size(panel, width, height);
  lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(panel, fill, 0);
  lv_obj_set_style_border_width(panel, 1, 0);
  lv_obj_set_style_border_color(panel, border, 0);
  lv_obj_set_style_radius(panel, radius, 0);
  lv_obj_set_style_pad_all(panel, 0, 0);
  if (shadow) {
    lv_obj_set_style_shadow_width(panel, 5, 0);
    lv_obj_set_style_shadow_opa(panel, LV_OPA_10, 0);
    lv_obj_set_style_shadow_color(panel, LV_COLOR_MAKE(0, 0, 0), 0);
    lv_obj_set_style_shadow_ofs_x(panel, 2, 0);
    lv_obj_set_style_shadow_ofs_y(panel, 2, 0);
  }
  return panel;
}

lv_obj_t *makeLabel(lv_obj_t *parent, const char *text, int16_t x, int16_t y,
                    int16_t width, int16_t height, const lv_font_t *font,
                    lv_color_t color,
                    lv_text_align_t align = LV_TEXT_ALIGN_LEFT) {
  lv_obj_t *label = lv_label_create(parent);
  clearObject(label);
  lv_label_set_text(label, text);
  lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
  const lv_coord_t lineHeight = lv_font_get_line_height(font);
  lv_obj_set_pos(label, x, y);
  lv_obj_set_size(label, width, lineHeight + 1);
  lv_obj_set_style_text_font(label, font, 0);
  lv_obj_set_style_text_color(label, color, 0);
  lv_obj_set_style_text_align(label, align, 0);
  lv_obj_set_style_text_letter_space(label, 0, 0);
  lv_obj_set_style_text_line_space(label, 0, 0);
  return label;
}

lv_obj_t *makeCircle(lv_obj_t *parent, int16_t x, int16_t y, int16_t size,
                     lv_color_t fill, lv_color_t border, uint8_t borderWidth) {
  lv_obj_t *circle = makePanel(parent, x, y, size, size, fill, border,
                                LV_RADIUS_CIRCLE, false);
  lv_obj_set_style_border_width(circle, borderWidth, 0);
  return circle;
}

lv_obj_t *makeButton(lv_obj_t *parent, const char *text, int16_t x, int16_t y,
                     int16_t width, int16_t height, lv_color_t fill,
                     lv_color_t textColor, lv_event_cb_t callback,
                     intptr_t userData = 0) {
  lv_obj_t *button = makePanel(parent, x, y, width, height, fill, fill, 7,
                                false);
  lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_color(button, fill, LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_STATE_PRESSED);
  lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED,
                      reinterpret_cast<void *>(userData));
  const lv_coord_t lineHeight = lv_font_get_line_height(&lv_font_montserrat_8);
  const int16_t labelY = max<int16_t>(0, (height - lineHeight) / 2);
  makeLabel(button, text, 0, labelY, width, lineHeight,
            &lv_font_montserrat_8, textColor, LV_TEXT_ALIGN_CENTER);
  return button;
}

void flushDisplay(lv_disp_drv_t *driver, const lv_area_t *area,
                  lv_color_t *color) {
  const int16_t width = static_cast<int16_t>(area->x2 - area->x1 + 1);
  const int16_t height = static_cast<int16_t>(area->y2 - area->y1 + 1);
  lcd.pushPixels(area->x1, area->y1, width, height,
                 reinterpret_cast<const uint16_t *>(color));
  lv_disp_flush_ready(driver);
}

void setPage(UiPage page);
void setAiState(AiState state);
void renderChatMessage();
void startWifiPortal();
void updateToolsUi(uint32_t now);

void navEvent(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  setPage(static_cast<UiPage>(
      reinterpret_cast<intptr_t>(lv_event_get_user_data(event))));
}

void clearAiConversation() {
  aiBridgeClearHistory();
  aiAnswerStreaming = false;
  aiAnswerHasStarted = false;
  strlcpy(chatHistory, "How can I help?", kChatHistoryLength);
  renderChatMessage();
  setAiState(AI_READY);
}

void setFocusButtonText(const char *text) {
  if (!focusButton) return;
  lv_obj_t *label = lv_obj_get_child(focusButton, 0);
  if (label) lv_label_set_text(label, text);
}

void updateFocusUi() {
  if (!focusTimeLabel || !focusStatusLabel || !focusHeaderStatusLabel) return;

  char timeText[6];
  snprintf(timeText, sizeof(timeText), "%02lu:%02lu",
           static_cast<unsigned long>(focusRemainingSeconds / 60U),
           static_cast<unsigned long>(focusRemainingSeconds % 60U));
  lv_label_set_text(focusTimeLabel, timeText);

  const bool complete = focusRemainingSeconds == 0;
  lv_label_set_text(focusHeaderStatusLabel,
                    complete ? "DONE"
                             : focusRunning ? "RUN"
                                            : focusHasStarted ? "PAUSED" : "READY");
  lv_label_set_text(focusStatusLabel,
                    complete ? "COMPLETE"
                             : focusRunning ? "FOCUSING"
                                            : focusHasStarted ? "PAUSED" : "READY");
  if (focusProgressFill) {
    const uint32_t elapsedSeconds = FOCUS_DURATION_SECONDS - focusRemainingSeconds;
    const int32_t progressWidth =
        static_cast<int32_t>((elapsedSeconds * 164U) / FOCUS_DURATION_SECONDS);
    lv_obj_set_width(focusProgressFill, progressWidth);
  }
  setFocusButtonText(complete ? "RESET"
                              : focusRunning ? "PAUSE"
                                             : focusHasStarted ? "RESUME"
                                                               : "START");

  if (focusButton) {
    lv_obj_set_style_bg_color(focusButton,
                              focusRunning ? COLOR_WARNING : COLOR_PRIMARY, 0);
    lv_obj_set_style_border_color(focusButton,
                                  focusRunning ? COLOR_WARNING : COLOR_PRIMARY,
                                  0);
  }
}

void resetFocusTimer() {
  focusRemainingSeconds = FOCUS_DURATION_SECONDS;
  focusRunning = false;
  focusHasStarted = false;
  focusLastTickMs = millis();
  updateFocusUi();
}

void toggleFocusTimer() {
  if (focusRemainingSeconds == 0) {
    resetFocusTimer();
    return;
  }

  focusRunning = !focusRunning;
  focusHasStarted = true;
  if (focusRunning) focusLastTickMs = millis();
  updateFocusUi();
}

void updateFocusTimer(uint32_t now) {
  if (!focusRunning) return;
  const uint32_t elapsedMs = now - focusLastTickMs;
  if (elapsedMs < 1000U) return;

  const uint32_t elapsedSeconds = elapsedMs / 1000U;
  focusLastTickMs += elapsedSeconds * 1000U;
  if (elapsedSeconds >= focusRemainingSeconds) {
    focusRemainingSeconds = 0;
    focusRunning = false;
  } else {
    focusRemainingSeconds -= elapsedSeconds;
  }
  updateFocusUi();
}

void focusEvent(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  toggleFocusTimer();
}

lv_obj_t *makePage(lv_obj_t *screen) {
  lv_obj_t *page = lv_obj_create(screen);
  clearObject(page);
  lv_obj_set_pos(page, 0, 0);
  lv_obj_set_size(page, SCREEN_WIDTH, 196);
  lv_obj_set_style_bg_color(page, COLOR_BG, 0);
  lv_obj_set_style_bg_opa(page, LV_OPA_COVER, 0);
  return page;
}

lv_obj_t *makeHeader(lv_obj_t *page, const char *title, const char *status,
                     lv_color_t statusColor, lv_obj_t **dotOutput = nullptr) {
  makeLabel(page, title, 16, 10, 160, 16, &lv_font_montserrat_12, COLOR_TEXT);
  lv_obj_t *dot = makeCircle(page, 250, 13, 5, statusColor, statusColor, 0);
  lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);
  if (dotOutput) *dotOutput = dot;
  lv_obj_t *statusLabel =
      makeLabel(page, status, 262, 10, 42, 11, &lv_font_montserrat_8,
                COLOR_MUTED, LV_TEXT_ALIGN_LEFT);
  lv_obj_t *line = lv_obj_create(page);
  clearObject(line);
  lv_obj_set_pos(line, 16, 34);
  lv_obj_set_size(line, 288, 1);
  lv_obj_set_style_bg_color(line, COLOR_LINE, 0);
  lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
  return statusLabel;
}

void buildAiPage(lv_obj_t *page) {
  aiHeaderStatus = makeHeader(page, "ODES", "LOCAL", COLOR_SUCCESS);

  aiStateLabel = makeLabel(page, "READY TO TALK", 16, 42, 288, 10,
                           &lv_font_montserrat_8, COLOR_MUTED,
                           LV_TEXT_ALIGN_CENTER);

  aiOrbGlow = makeCircle(page, 125, 59, 69, COLOR_PRIMARY, COLOR_PRIMARY, 0);
  lv_obj_set_style_bg_opa(aiOrbGlow, LV_OPA_10, 0);
  aiOrbOuter = makeCircle(page, 131, 65, 57, COLOR_PANEL_ALT, COLOR_PRIMARY, 1);
  aiOrbRing = makeCircle(page, 137, 71, 45, COLOR_BG, COLOR_PRIMARY_LIGHT, 1);
  lv_obj_set_style_bg_opa(aiOrbRing, LV_OPA_TRANSP, 0);
  aiOrbInner = makeCircle(page, 145, 79, 29, COLOR_BG, COLOR_BLUE, 1);
  lv_obj_set_style_bg_opa(aiOrbInner, LV_OPA_TRANSP, 0);
  aiOrbCore =
      makeCircle(page, 153, 87, 14, COLOR_PRIMARY_LIGHT, COLOR_PRIMARY_LIGHT, 0);

  aiSubtitle = makeLabel(page, "BOOK OF SONGS", 16, 128, 288, 10,
                         &lv_font_montserrat_8, COLOR_PRIMARY_LIGHT,
                         LV_TEXT_ALIGN_CENTER);

  lv_obj_t *messagePanel =
      makePanel(page, 16, 138, 288, 27, COLOR_PANEL, COLOR_LINE, 8, true);
  aiMessage = makeLabel(messagePanel, chatHistory, 11, 4, 266, 19,
                        &lv_font_montserrat_8, COLOR_TEXT);
  lv_obj_set_height(aiMessage, 19);
  lv_label_set_long_mode(aiMessage, LV_LABEL_LONG_WRAP);

  lv_obj_t *inputPanel =
      makePanel(page, 16, 169, 288, 25, COLOR_PANEL_ALT, COLOR_LINE, 8, false);
  makeLabel(inputPanel, "USB", 7, 7, 25, 10, &lv_font_montserrat_8,
            COLOR_PRIMARY_LIGHT);
  aiInputHint = makeLabel(inputPanel, "TYPE + ENTER", 39, 7, 112, 10,
                          &lv_font_montserrat_8, COLOR_SOFT);
  makeLabel(inputPanel, "HOLD MENU", 190, 7, 90, 10,
            &lv_font_montserrat_8, COLOR_MUTED, LV_TEXT_ALIGN_RIGHT);
}

void buildFocusPage(lv_obj_t *page) {
  focusHeaderStatusLabel = makeHeader(page, "FOCUS", "READY", COLOR_PRIMARY);
  lv_obj_t *ring = makeCircle(page, 31, 61, 64, COLOR_BG, COLOR_LINE, 5);
  lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, 0);
  lv_obj_clear_flag(ring, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(ring, LV_OBJ_FLAG_CLICKABLE);
  focusTimeLabel = makeLabel(page, "25:00", 22, 81, 82, 22,
                             &lv_font_montserrat_18, COLOR_TEXT,
                             LV_TEXT_ALIGN_CENTER);

  makeLabel(page, "FOCUS SESSION", 140, 64, 164, 11,
            &lv_font_montserrat_8, COLOR_MUTED);
  focusStatusLabel = makeLabel(page, "READY", 140, 81, 164, 12,
                               &lv_font_montserrat_10, COLOR_PRIMARY_LIGHT);
  lv_obj_t *progressTrack =
      makePanel(page, 140, 99, 164, 4, COLOR_LINE, COLOR_LINE, 3, false);
  focusProgressFill = makePanel(progressTrack, 0, 0, 164, 4, COLOR_PRIMARY,
                                COLOR_PRIMARY, 3, false);
  lv_obj_set_style_border_width(focusProgressFill, 0, 0);
  makeLabel(page, "25 MIN SESSION", 140, 109, 164, 11,
            &lv_font_montserrat_8, COLOR_SOFT);

  lv_obj_t *task = makePanel(page, 16, 149, 288, 35, COLOR_PANEL, COLOR_LINE,
                             8, true);
  makeLabel(task, "CURRENT TASK", 11, 5, 226, 10, &lv_font_montserrat_8,
            COLOR_MUTED);
  makeLabel(task, "UI DESIGN", 11, 17, 226, 12, &lv_font_montserrat_10,
            COLOR_TEXT);
  focusButton = makeButton(task, "START", 237, 7, 43, 21, COLOR_PRIMARY,
                           COLOR_BG, focusEvent);
  updateFocusUi();
}

void buildToolsPage(lv_obj_t *page) {
  toolsHeaderStatusLabel =
      makeHeader(page, "TOOLS", "WAIT", COLOR_WARNING, &toolsHeaderDot);
  const int16_t cardWidth = 138;
  const int16_t cardHeight = 58;
  const int16_t left = 16;
  const int16_t right = 165;
  const int16_t top = 48;
  const int16_t bottom = 116;

  lv_obj_t *time = makePanel(page, left, top, 139, cardHeight,
                             COLOR_PANEL, COLOR_LINE, 10, true);
  makeCircle(time, 12, 16, 5, COLOR_PRIMARY, COLOR_PRIMARY, 0);
  makeLabel(time, "TIME", 25, 13, 102, 10, &lv_font_montserrat_8, COLOR_MUTED);
  toolTimeLabel = makeLabel(time, "--:--", 25, 30, 102, 14,
                            &lv_font_montserrat_10, COLOR_TEXT);

  lv_obj_t *wifi = makePanel(page, right, top, 139, cardHeight,
                             COLOR_PANEL, COLOR_LINE, 10, true);
  makeCircle(wifi, 12, 16, 5, COLOR_WARNING, COLOR_WARNING, 0);
  wifiNameLabel = makeLabel(wifi, "WI-FI", 25, 13, 102, 10,
                            &lv_font_montserrat_8, COLOR_MUTED);
  wifiValueLabel = makeLabel(wifi, "CONNECTING", 25, 30, 102, 14,
                             &lv_font_montserrat_10, COLOR_TEXT);

  lv_obj_t *device = makePanel(page, left, bottom, 139, cardHeight,
                               COLOR_PANEL, COLOR_LINE, 10, true);
  makeCircle(device, 12, 16, 5, COLOR_SUCCESS, COLOR_SUCCESS, 0);
  makeLabel(device, "DEVICE", 25, 13, 102, 10, &lv_font_montserrat_8,
            COLOR_MUTED);
  makeLabel(device, "NORMAL", 25, 30, 102, 14, &lv_font_montserrat_10,
            COLOR_TEXT);

  lv_obj_t *sensor = makePanel(page, right, bottom, 139, cardHeight,
                               COLOR_PANEL, COLOR_LINE, 10, true);
  makeCircle(sensor, 12, 16, 5, COLOR_BLUE, COLOR_BLUE, 0);
  makeLabel(sensor, "SENSOR", 25, 13, 102, 10, &lv_font_montserrat_8,
            COLOR_MUTED);
  makeLabel(sensor, "READY", 25, 30, 102, 14, &lv_font_montserrat_10,
            COLOR_TEXT);
}

void buildNavigation(lv_obj_t *screen) {
  lv_obj_t *nav = makePanel(screen, 10, 201, 300, 32, COLOR_PANEL, COLOR_LINE,
                            9, true);
  const char *labels[] = {"ODES", "FOCUS", "TOOLS"};
  const int16_t centers[] = {0, 100, 200};
  for (uint8_t index = 0; index < 3; ++index) {
    navButtons[index] =
        makeButton(nav, labels[index], centers[index] + 1, 1, 99, 30,
                   index == 0 ? COLOR_PANEL_ALT : COLOR_PANEL,
                   index == 0 ? COLOR_PRIMARY_LIGHT : COLOR_MUTED, navEvent,
                   index);
    if (index > 0) {
      lv_obj_t *separator = lv_obj_create(nav);
      clearObject(separator);
      lv_obj_set_pos(separator, centers[index], 0);
      lv_obj_set_size(separator, 1, 30);
      lv_obj_set_style_bg_color(separator, COLOR_LINE, 0);
      lv_obj_set_style_bg_opa(separator, LV_OPA_COVER, 0);
    }
  }

  navActive = lv_obj_create(nav);
  clearObject(navActive);
  lv_obj_set_pos(navActive, 18, 29);
  lv_obj_set_size(navActive, 64, 2);
  lv_obj_set_style_bg_color(navActive, COLOR_PRIMARY, 0);
  lv_obj_set_style_bg_opa(navActive, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(navActive, 2, 0);
}

void buildUi() {
  lv_obj_t *screen = lv_scr_act();
  lv_obj_clean(screen);
  lv_obj_set_style_bg_color(screen, COLOR_BG, 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

  aiPage = makePage(screen);
  focusPage = makePage(screen);
  toolsPage = makePage(screen);
  buildAiPage(aiPage);
  buildFocusPage(focusPage);
  buildToolsPage(toolsPage);
  buildNavigation(screen);
  setPage(PAGE_AI);
}

void setPage(UiPage page) {
  currentPage = page;
  if (page == PAGE_AI) lv_obj_clear_flag(aiPage, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(aiPage, LV_OBJ_FLAG_HIDDEN);
  if (page == PAGE_FOCUS) lv_obj_clear_flag(focusPage, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(focusPage, LV_OBJ_FLAG_HIDDEN);
  if (page == PAGE_TOOLS) lv_obj_clear_flag(toolsPage, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(toolsPage, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_x(navActive, 18 + static_cast<int16_t>(page) * 100);
  for (uint8_t index = 0; index < 3; ++index) {
    const bool active = index == static_cast<uint8_t>(page);
    lv_obj_set_style_bg_color(navButtons[index],
                              active ? COLOR_PANEL_ALT : COLOR_PANEL, 0);
    lv_obj_set_style_border_color(navButtons[index],
                                  active ? COLOR_PRIMARY : COLOR_PANEL, 0);
    lv_obj_t *label = lv_obj_get_child(navButtons[index], 0);
    if (label) {
      lv_obj_set_style_text_color(label,
                                  active ? COLOR_PRIMARY_LIGHT : COLOR_MUTED,
                                  0);
    }
  }
}

void setAiState(AiState state) {
  aiState = state;
  const bool listening = state == AI_LISTENING;
  const bool thinking = state == AI_THINKING;
  const lv_color_t main = stateMainColor();

  lv_label_set_text(aiStateLabel, listening ? "LISTENING"
                                           : thinking ? "THINKING"
                                                      : state == AI_ERROR
                                                            ? "ERROR"
                                                            : "READY TO TALK");
  lv_label_set_text(aiSubtitle, listening ? "RECORDING"
                                         : thinking ? "CONTACTING AI"
                                                    : state == AI_ERROR
                                                          ? "REQUEST FAILED"
                                                          : "BOOK OF SONGS");
  lv_obj_set_style_text_color(aiStateLabel,
                              listening ? COLOR_ERROR
                                        : state == AI_ERROR ? COLOR_WARNING
                                                            : COLOR_MUTED,
                              0);
  lv_obj_set_style_text_color(aiSubtitle,
                              state == AI_ERROR ? COLOR_WARNING
                                                : listening ? COLOR_ERROR
                                                            : main,
                              0);
  lv_obj_set_style_bg_opa(aiOrbGlow,
                          listening || state == AI_ERROR ? LV_OPA_20
                                                         : LV_OPA_10,
                          0);
  lv_obj_set_style_bg_color(aiOrbGlow, main, 0);
  lv_obj_set_style_border_color(aiOrbOuter, main, 0);
  lv_obj_set_style_border_color(aiOrbInner,
                                thinking ? COLOR_PRIMARY_LIGHT : COLOR_BLUE,
                                0);
  lv_obj_set_style_bg_color(aiOrbCore, stateCoreColor(), 0);
  if (aiHeaderStatus) {
    const bool online = WiFi.status() == WL_CONNECTED;
    lv_label_set_text(aiHeaderStatus,
                      state == AI_ERROR ? "ERROR" : online ? "ONLINE" : "OFF");
    lv_obj_set_style_text_color(aiHeaderStatus,
                                state == AI_ERROR ? COLOR_WARNING
                                                  : online ? COLOR_SUCCESS
                                                           : COLOR_MUTED,
                                0);
  }
}

void appendChatText(const char *text) {
  if (text == nullptr || text[0] == '\0') return;
  const size_t textLength = strlen(text);
  if (textLength >= sizeof(chatHistory)) {
    text += textLength - sizeof(chatHistory) + 1;
    strlcpy(chatHistory, text, sizeof(chatHistory));
    return;
  }
  size_t used = strlen(chatHistory);
  const size_t overflow = used + textLength + 1 > sizeof(chatHistory)
                              ? used + textLength + 1 - sizeof(chatHistory)
                              : 0;
  if (overflow > 0) {
    memmove(chatHistory, chatHistory + overflow, used - overflow + 1);
    used -= overflow;
  }
  memcpy(chatHistory + used, text, textLength + 1);
}

void renderChatMessage() {
  if (!aiMessage) return;
  const size_t length = strlen(chatHistory);
  const char *visible = chatHistory;
  if (length > 76) {
    visible += length - 76;
    while ((*visible & 0xC0) == 0x80) ++visible;
  }
  lv_label_set_text(aiMessage, visible);
}

void showChatLine(const char *text) {
  strlcpy(chatHistory, text, kChatHistoryLength);
  renderChatMessage();
}

void submitChatLine(const char *text) {
  char line[kChatHistoryLength] = {};
  snprintf(line, sizeof(line), "YOU: %s", text);
  showChatLine(line);
  aiAnswerStreaming = true;
  aiAnswerHasStarted = false;
  aiBridgeSubmit(text);
  setAiState(AI_THINKING);
}

void handleSerialCommand(char *line) {
  line[strcspn(line, "\r\n")] = '\0';
  if (line[0] == '\0') return;

  if (strncmp(line, "ask ", 4) == 0) {
    submitChatLine(line + 4);
    return;
  }
  if (strncmp(line, "url ", 4) == 0) {
    char token[80] = {};
    aiBridgeLoadEndpoint(chatHistory, kChatHistoryLength, token, sizeof(token));
    aiBridgeSaveEndpoint(line + 4, token);
    showChatLine("AI endpoint updated.");
    return;
  }
  if (strncmp(line, "token ", 6) == 0) {
    char url[120] = {};
    aiBridgeLoadEndpoint(url, sizeof(url), chatHistory, kChatHistoryLength);
    aiBridgeSaveEndpoint(url, line + 6);
    showChatLine("AI token updated.");
    return;
  }
  submitChatLine(line);
}

void pumpAiBridge(uint32_t now) {
  char chunk[96] = {};
  bool final = false;
  while (aiBridgePoll(chunk, sizeof(chunk), &final)) {
    if (chunk[0] != '\0') {
      if (!aiAnswerHasStarted) {
        strlcpy(chatHistory, "ODES: ", kChatHistoryLength);
        aiAnswerHasStarted = true;
      }
      appendChatText(chunk);
      renderChatMessage();
      aiLastChunkMs = now;
    }
    if (final) {
      aiAnswerStreaming = false;
      const AiBridgeStatus bridgeStatus = aiBridgeStatus();
      if (bridgeStatus == AiBridgeStatus::kError) {
        setAiState(AI_ERROR);
        lv_label_set_text(aiSubtitle, aiBridgeLastError());
      } else {
        setAiState(AI_READY);
      }
    }
  }
}

void pollSerialPrompt(uint32_t now) {
  while (Serial.available() > 0) {
    const int value = Serial.read();
    if (value < 0) break;
    if (value == '\r') continue;
    if (value == '\n') {
      serialPrompt[serialPromptLength] = '\0';
      handleSerialCommand(serialPrompt);
      serialPrompt[0] = '\0';
      serialPromptLength = 0;
      continue;
    }
    if (serialPromptLength + 1 < sizeof(serialPrompt)) {
      serialPrompt[serialPromptLength++] = static_cast<char>(value);
    }
  }
  const bool waiting = serialPromptLength > 0 && now - aiLastChunkMs > 4000U;
  if (waiting != aiInputHintWaiting) {
    lv_label_set_text(aiInputHint,
                      waiting ? "PRESS ENTER" : "TYPE + ENTER");
    aiInputHintWaiting = waiting;
  }
}

void startWifiPortal() {
  if (wifiManager.getConfigPortalActive()) return;
  WiFi.mode(WIFI_AP_STA);
  wifiManager.startConfigPortal(wifiPortalSsid, wifiPortalPassword);
}

void updateToolsUi(uint32_t now) {
  if (!toolTimeLabel || !wifiNameLabel || !wifiValueLabel) return;

  const bool connected = WiFi.status() == WL_CONNECTED;
  const bool portalActive = wifiManager.getConfigPortalActive();
  if (aiHeaderStatus && aiState != AI_ERROR) {
    lv_label_set_text(aiHeaderStatus, connected ? "ONLINE" : "OFF");
    lv_obj_set_style_text_color(aiHeaderStatus,
                                connected ? COLOR_SUCCESS : COLOR_MUTED, 0);
  }
  if (connected) {
    wifiEverConnected = true;
    if (!timeSyncStarted) {
      configTzTime("CST-8", "ntp.aliyun.com", "pool.ntp.org");
      timeSyncStarted = true;
    }
    lv_label_set_text(wifiNameLabel, "WI-FI");
    lv_label_set_text(wifiValueLabel, "ONLINE");
    lv_label_set_text(toolsHeaderStatusLabel, "ONLINE");
    lv_obj_set_style_bg_color(toolsHeaderDot, COLOR_SUCCESS, 0);
    lv_obj_set_style_border_color(toolsHeaderDot, COLOR_SUCCESS, 0);
  } else if (portalActive) {
    char nameText[20];
    char passwordText[24];
    snprintf(nameText, sizeof(nameText), "AP %s", wifiPortalSsid);
    snprintf(passwordText, sizeof(passwordText), "PW %s", wifiPortalPassword);
    lv_label_set_text(wifiNameLabel, nameText);
    lv_label_set_text(wifiValueLabel, passwordText);
    lv_label_set_text(toolsHeaderStatusLabel, "SETUP");
    lv_obj_set_style_bg_color(toolsHeaderDot, COLOR_WARNING, 0);
    lv_obj_set_style_border_color(toolsHeaderDot, COLOR_WARNING, 0);
  } else {
    lv_label_set_text(wifiNameLabel, "WI-FI");
    lv_label_set_text(wifiValueLabel,
                      wifiEverConnected ? "OFFLINE" : "CONNECTING");
    lv_label_set_text(toolsHeaderStatusLabel, "OFF");
    lv_obj_set_style_bg_color(toolsHeaderDot, COLOR_MUTED, 0);
    lv_obj_set_style_border_color(toolsHeaderDot, COLOR_MUTED, 0);
  }

  time_t epoch = time(nullptr);
  if (epoch > 1704067200) {
    struct tm localTime;
    char timeText[6];
    localtime_r(&epoch, &localTime);
    strftime(timeText, sizeof(timeText), "%H:%M", &localTime);
    lv_label_set_text(toolTimeLabel, timeText);
  } else {
    lv_label_set_text(toolTimeLabel, "--:--");
  }

  lastToolsUpdateMs = now;
}

void setupNetwork() {
  const uint64_t chipId = ESP.getEfuseMac();
  snprintf(wifiPortalSsid, sizeof(wifiPortalSsid), "ODES-%04X",
           static_cast<unsigned int>(chipId & 0xFFFFU));
  snprintf(wifiPortalPassword, sizeof(wifiPortalPassword), "%s", "12345678");

  wifiManager.setCustomHeadElement(wifiInfoConfigLink);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  wifiManager.setConfigPortalBlocking(false);
  wifiManager.setConfigPortalTimeout(0);
  wifiManager.setConnectTimeout(5);
  wifiManager.autoConnect(wifiPortalSsid, wifiPortalPassword);
  updateToolsUi(millis());
}

void scanKeys() {
  const uint32_t now = millis();
  if (now - lastKeyScan < 35) return;
  lastKeyScan = now;

  const uint16_t inputs = expander.readInputs();
  const uint16_t pressed =
      static_cast<uint16_t>(~inputs) &
      static_cast<uint16_t>((1U << XL_PIN_KEY_AI) |
                            (1U << XL_PIN_KEY_FOCUS) |
                            (1U << XL_PIN_KEY_TOOLS) |
                            (1U << XL_PIN_KEY_MENU));
  const uint16_t newPress = pressed & ~lastPressedKeys;

  const uint16_t menuMask = static_cast<uint16_t>(1U << XL_PIN_KEY_MENU);
  const bool menuWasPressed = (lastPressedKeys & menuMask) != 0;
  const bool menuIsPressed = (pressed & menuMask) != 0;
  if (menuIsPressed && !menuWasPressed) {
    menuPressedAtMs = now;
    menuPressedOnActionPage = true;
    menuLongPressHandled = false;
  }
  if (menuIsPressed && menuPressedOnActionPage && !menuLongPressHandled &&
      now - menuPressedAtMs >= 800U) {
    if (currentPage == PAGE_FOCUS) resetFocusTimer();
    else if (currentPage == PAGE_TOOLS) startWifiPortal();
    else if (currentPage == PAGE_AI) clearAiConversation();
    menuLongPressHandled = true;
  }
  if (!menuIsPressed && menuWasPressed) {
    if (menuPressedOnActionPage && currentPage == PAGE_FOCUS &&
        !menuLongPressHandled) {
      toggleFocusTimer();
    }
    menuPressedOnActionPage = false;
    menuLongPressHandled = false;
  }

  lastPressedKeys = pressed;

  if (newPress & (1U << XL_PIN_KEY_AI)) {
    setPage(PAGE_AI);
    if (serialPromptLength > 0) {
      snprintf(chatHistory, kChatHistoryLength, "YOU: %s", serialPrompt);
      renderChatMessage();
      aiAnswerStreaming = true;
      aiAnswerHasStarted = false;
      aiBridgeSubmit(serialPrompt);
      setAiState(AI_THINKING);
      serialPrompt[0] = '\0';
      serialPromptLength = 0;
    } else {
      strlcpy(chatHistory, "Ask me anything over the serial console.",
              kChatHistoryLength);
      renderChatMessage();
    }
  }
  if (newPress & (1U << XL_PIN_KEY_FOCUS)) setPage(PAGE_FOCUS);
  if (newPress & (1U << XL_PIN_KEY_TOOLS)) setPage(PAGE_TOOLS);
}

void setupDisplay() {
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, I2C_CLOCK_HZ);
  const bool expanderReady = expander.begin();
  Serial.printf("XL9555: %s\n", expanderReady ? "ok" : "not found");
  if (!expanderReady) return;

  expander.writePin(XL_PIN_LCD_PWR, true);
  expander.writePin(XL_PIN_LCD_RST, false);
  delay(20);
  expander.writePin(XL_PIN_LCD_RST, true);
  delay(120);

  lcd.begin();
  lv_init();
  lv_disp_draw_buf_init(&drawBuffer, displayBuffer, nullptr,
                        SCREEN_WIDTH * 20);
  lv_disp_drv_init(&displayDriver);
  displayDriver.hor_res = SCREEN_WIDTH;
  displayDriver.ver_res = SCREEN_HEIGHT;
  displayDriver.flush_cb = flushDisplay;
  displayDriver.draw_buf = &drawBuffer;
  lv_disp_drv_register(&displayDriver);

  buildUi();
  displayReady = true;
  lastLvglTick = millis();
  Serial.println("LVGL: ODES UI ready");
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println("AI ESP32-S3 LVGL UI");
  Serial.println("Commands: ask <text> | url <endpoint> | token <bearer>");
  setupDisplay();
  if (displayReady) {
    lv_timer_handler();
    setupNetwork();
  }
  aiBridgeBegin();
}

void loop() {
  if (!displayReady) {
    delay(1000);
    return;
  }

  scanKeys();
  wifiManager.process();
  const uint32_t now = millis();
  pollSerialPrompt(now);
  pumpAiBridge(now);
  updateFocusTimer(now);
  if (now - lastToolsUpdateMs >= 1000U) updateToolsUi(now);
  lv_tick_inc(now - lastLvglTick);
  lastLvglTick = now;
  lv_timer_handler();
  delay(5);
}
