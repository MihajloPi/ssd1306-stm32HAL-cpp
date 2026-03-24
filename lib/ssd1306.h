#pragma once

#include "stm32f4xx_hal.h"
#include "fonts.h"
#include <cstdint>

class SSD1306 {
public:
    enum class Color : uint8_t {
        Black = 0x00,
        White = 0x01
    };

    static constexpr uint8_t  I2C_ADDR   = 0x78;
    static constexpr uint16_t WIDTH      = 128;
    static constexpr uint16_t HEIGHT     = 64;

    explicit SSD1306(I2C_HandleTypeDef &hi2c);

    bool     Init();
    void     UpdateScreen();
    void     Fill(Color color);
    void     DrawPixel(uint8_t x, uint8_t y, Color color);
    char     WriteChar(char ch, FontDef font, Color color);
    char     WriteString(const char *str, FontDef font, Color color);
    void     SetCursor(uint8_t x, uint8_t y);
    void     InvertColors();

private:
    I2C_HandleTypeDef &_hi2c;

    uint8_t  _buffer[WIDTH * HEIGHT / 8]{};
    uint16_t _cursorX   = 0;
    uint16_t _cursorY   = 0;
    bool     _inverted  = false;
    bool     _initialized = false;

    bool WriteCommand(uint8_t cmd);
};
