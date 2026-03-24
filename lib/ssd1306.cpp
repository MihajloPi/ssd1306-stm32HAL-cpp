#include "ssd1306.h"

SSD1306::SSD1306(I2C_HandleTypeDef &hi2c) : _hi2c(hi2c) {}

bool SSD1306::WriteCommand(uint8_t cmd)
{
    return HAL_I2C_Mem_Write(&_hi2c, I2C_ADDR, 0x00, 1, &cmd, 1, 10) == HAL_OK;
}

bool SSD1306::Init()
{
    HAL_Delay(100);

    bool ok = true;
    ok &= WriteCommand(0xAE);   // Display off
    ok &= WriteCommand(0x20);   // Memory addressing mode
    ok &= WriteCommand(0x10);   // Page addressing mode
    ok &= WriteCommand(0xB0);   // Page start address
    ok &= WriteCommand(0xC8);   // COM output scan direction
    ok &= WriteCommand(0x00);   // Low column address
    ok &= WriteCommand(0x10);   // High column address
    ok &= WriteCommand(0x40);   // Start line address
    ok &= WriteCommand(0x81);   // Contrast control
    ok &= WriteCommand(0xFF);
    ok &= WriteCommand(0xA1);   // Segment re-map
    ok &= WriteCommand(0xA6);   // Normal display
    ok &= WriteCommand(0xA8);   // Multiplex ratio
    ok &= WriteCommand(HEIGHT - 1);
    ok &= WriteCommand(0xA4);   // Output follows RAM
    ok &= WriteCommand(0xD3);   // Display offset
    ok &= WriteCommand(0x00);
    ok &= WriteCommand(0xD5);   // Clock divide ratio
    ok &= WriteCommand(0xF0);
    ok &= WriteCommand(0xD9);   // Pre-charge period
    ok &= WriteCommand(0x22);
    ok &= WriteCommand(0xDA);   // COM pins hardware config
    ok &= WriteCommand(0x12);   // Alternative pin config, no LR remap
    ok &= WriteCommand(0xDB);   // VCOMH deselect level
    ok &= WriteCommand(0x20);   // 0.77 × Vcc
    ok &= WriteCommand(0x8D);   // DC-DC enable
    ok &= WriteCommand(0x14);
    ok &= WriteCommand(0xAF);   // Display on

    if (!ok) return false;

    Fill(Color::Black);
    UpdateScreen();

    _cursorX     = 0;
    _cursorY     = 0;
    _initialized = true;
    return true;
}

void SSD1306::Fill(Color color)
{
    uint8_t fill = (color == Color::Black) ? 0x00 : 0xFF;
    for (auto &byte : _buffer)
        byte = fill;
}

void SSD1306::UpdateScreen()
{
    for (uint8_t page = 0; page < 8; page++) {
        WriteCommand(static_cast<uint8_t>(0xB0 + page));
        WriteCommand(0x00);
        WriteCommand(0x10);
        HAL_I2C_Mem_Write(&_hi2c, I2C_ADDR, 0x40, 1,
                          &_buffer[WIDTH * page], WIDTH, 100);
    }
}

void SSD1306::DrawPixel(uint8_t x, uint8_t y, Color color)
{
    if (x >= WIDTH || y >= HEIGHT) return;

    Color c = _inverted ? (color == Color::Black ? Color::White : Color::Black) : color;

    if (c == Color::White)
        _buffer[x + (y / 8) * WIDTH] |=  (1 << (y % 8));
    else
        _buffer[x + (y / 8) * WIDTH] &= ~(1 << (y % 8));
}

char SSD1306::WriteChar(char ch, FontDef font, Color color)
{
    if (_cursorX + font.FontWidth  > WIDTH ||
        _cursorY + font.FontHeight > HEIGHT)
        return 0;

    for (uint32_t row = 0; row < font.FontHeight; row++) {
        uint16_t bits = font.data[(static_cast<uint8_t>(ch) - 32) * font.FontHeight + row];
        for (uint32_t col = 0; col < font.FontWidth; col++) {
            Color px = ((bits << col) & 0x8000)
                       ? color
                       : (color == Color::White ? Color::Black : Color::White);
            DrawPixel(static_cast<uint8_t>(_cursorX + col),
                      static_cast<uint8_t>(_cursorY + row), px);
        }
    }

    _cursorX += font.FontWidth;
    return ch;
}

char SSD1306::WriteString(const char *str, FontDef font, Color color)
{
    while (*str) {
        if (WriteChar(*str, font, color) != *str)
            return *str;
        str++;
    }
    return *str;
}

void SSD1306::SetCursor(uint8_t x, uint8_t y)
{
    _cursorX = x;
    _cursorY = y;
}

void SSD1306::InvertColors()
{
    _inverted = !_inverted;
}
