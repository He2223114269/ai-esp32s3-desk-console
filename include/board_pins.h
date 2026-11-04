#pragma once

#include <Arduino.h>

constexpr uint8_t PIN_I2C_SDA = 41;
constexpr uint8_t PIN_I2C_SCL = 42;
constexpr uint32_t I2C_CLOCK_HZ = 400000;
constexpr uint8_t ADDR_XL9555 = 0x20;

constexpr uint8_t XL_PIN_BEEP = 3;
constexpr uint8_t XL_PIN_KEY_AI = 15;     // P17
constexpr uint8_t XL_PIN_KEY_FOCUS = 14;  // P16
constexpr uint8_t XL_PIN_KEY_TOOLS = 13;  // P15
constexpr uint8_t XL_PIN_KEY_MENU = 12;   // P14
// XL9555 P12 and P13 control the LCD reset and power/backlight.
constexpr uint8_t XL_PIN_LCD_RST = 10;
constexpr uint8_t XL_PIN_LCD_PWR = 11;

constexpr int8_t PIN_LCD_CS = 21;
constexpr int8_t PIN_LCD_SCLK = 12;
constexpr int8_t PIN_LCD_MOSI = 11;
constexpr int8_t PIN_LCD_DC = 40;
