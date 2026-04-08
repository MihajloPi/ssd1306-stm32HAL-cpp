#include "ssd1306.h"

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

SSD1306::SSD1306(I2C_HandleTypeDef &hi2c) : _hi2c(hi2c) {}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

bool SSD1306::writeCommand(uint8_t cmd)
{
    // Control byte 0x00 → Co=0, D/C#=0 (command stream)
    return HAL_I2C_Mem_Write(&_hi2c, I2C_ADDR,
                             0x00, I2C_MEMADD_SIZE_8BIT,
                             &cmd, 1, 10) == HAL_OK;
}

bool SSD1306::setAddressWindow()
{
    // Column address: 0 … 127
    bool ok = true;
    ok &= writeCommand(0x21);
    ok &= writeCommand(0x00);
    ok &= writeCommand(0x7F);

    // Page address: 0 … 7  (8 pages × 8 rows = 64 rows)
    ok &= writeCommand(0x22);
    ok &= writeCommand(0x00);
    ok &= writeCommand(0x07);

    return ok;
}

// ---------------------------------------------------------------------------
// Initialisation
// ---------------------------------------------------------------------------

bool SSD1306::init()
{
    HAL_Delay(100);   // Power-on stabilisation

    bool ok = true;

    ok &= writeCommand(0xAE);          // Display OFF

    // --- Memory addressing ------------------------------------------------
    // Use HORIZONTAL addressing (0x00) instead of the original page mode
    // (0x10).  In horizontal mode the internal pointer auto-increments
    // column-by-column, then page-by-page, so the entire 1 024-byte buffer
    // can be sent in a single I²C transaction / DMA burst.
    ok &= writeCommand(0x20);
    ok &= writeCommand(0x00);          // 0x00 = horizontal, 0x10 = page

    // --- Display geometry -------------------------------------------------
    ok &= writeCommand(0xB0);          // Page start (ignored in horizontal mode, harmless)
    ok &= writeCommand(0xC8);          // COM output scan direction (remapped)
    ok &= writeCommand(0x00);          // Low nibble of column start address
    ok &= writeCommand(0x10);          // High nibble of column start address
    ok &= writeCommand(0x40);          // Display start line = 0

    // --- Contrast ---------------------------------------------------------
    ok &= writeCommand(0x81);
    ok &= writeCommand(0xFF);          // Maximum contrast

    // --- Segment / display settings ---------------------------------------
    ok &= writeCommand(0xA1);          // Segment re-map (column 127 → SEG0)
    ok &= writeCommand(0xA6);          // Normal display (0xA7 = inverted)
    ok &= writeCommand(0xA8);          // Multiplex ratio …
    ok &= writeCommand(HEIGHT - 1);    // … = 63

    ok &= writeCommand(0xA4);          // Entire display ON: output follows RAM

    // --- Timing & driving -------------------------------------------------
    ok &= writeCommand(0xD3);          // Display offset …
    ok &= writeCommand(0x00);          // … = 0 (no vertical shift)

    ok &= writeCommand(0xD5);          // Display clock divide ratio / oscillator …
    ok &= writeCommand(0xF0);          // … ratio=1, frequency=max

    ok &= writeCommand(0xD9);          // Pre-charge period …
    ok &= writeCommand(0x22);          // … phase1=2, phase2=2 DCLKs

    ok &= writeCommand(0xDA);          // COM pins hardware configuration …
    ok &= writeCommand(0x12);          // … alternative pin config, no L/R remap

    ok &= writeCommand(0xDB);          // VCOMH deselect level …
    ok &= writeCommand(0x20);          // … ≈ 0.77 × Vcc

    // --- Charge pump ------------------------------------------------------
    ok &= writeCommand(0x8D);          // Charge pump setting …
    ok &= writeCommand(0x14);          // … enable (required for 3.3 V supply)

    ok &= writeCommand(0xAF);          // Display ON

    if (!ok) return false;

    // Clear framebuffer and push it synchronously before declaring ready
    fill(Color::Black);
    if (!updateScreen()) return false;

    _cursorX     = 0;
    _cursorY     = 0;
    _initialized = true;
    return true;
}

// ---------------------------------------------------------------------------
// Screen update – blocking
// ---------------------------------------------------------------------------

bool SSD1306::updateScreen()
{
    if (!setAddressWindow()) return false;

    // Send the entire framebuffer in one blocking call.
    // Control byte 0x40 → Co=0, D/C#=1 (data stream).
    return HAL_I2C_Mem_Write(&_hi2c, I2C_ADDR,
                             0x40, I2C_MEMADD_SIZE_8BIT,
                             _buffer, BUF_SIZE,
                             100) == HAL_OK;
}

// ---------------------------------------------------------------------------
// Screen update – DMA (non-blocking)
// ---------------------------------------------------------------------------

bool SSD1306::updateScreenDMA()
{
    if (_dmaInProgress) return false;   // Previous transfer still running

    if (!setAddressWindow()) return false;

    _dmaInProgress = true;

    if (HAL_I2C_Mem_Write_DMA(&_hi2c, I2C_ADDR,
                               0x40, I2C_MEMADD_SIZE_8BIT,
                               _buffer, BUF_SIZE) != HAL_OK)
    {
        _dmaInProgress = false;
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// DMA transfer-complete callback
// ---------------------------------------------------------------------------

void SSD1306::onTransferComplete(I2C_HandleTypeDef *hi2c)
{
    if (hi2c == &_hi2c)
        _dmaInProgress = false;
}

// ---------------------------------------------------------------------------
// Framebuffer helpers
// ---------------------------------------------------------------------------

void SSD1306::fill(Color color)
{
    uint8_t fill = (color == Color::Black) ? 0x00 : 0xFF;
    for (auto &byte : _buffer)
        byte = fill;
}

void SSD1306::drawPixel(uint8_t x, uint8_t y, Color color)
{
    if (x >= WIDTH || y >= HEIGHT) return;

    // Apply software inversion if active
    if (_inverted)
        color = (color == Color::Black) ? Color::White : Color::Black;

    const uint16_t idx = x + (y / 8) * WIDTH;
    const uint8_t  bit = static_cast<uint8_t>(1u << (y % 8));

    if (color == Color::White)
        _buffer[idx] |=  bit;
    else
        _buffer[idx] &= ~bit;
}

// ---------------------------------------------------------------------------
// Text rendering
// ---------------------------------------------------------------------------

char SSD1306::writeChar(char ch, FontDef font, Color color)
{
    // Bounds check
    if (_cursorX + font.FontWidth  > WIDTH ||
        _cursorY + font.FontHeight > HEIGHT)
        return 0;

    const uint32_t charOffset =
        static_cast<uint32_t>(static_cast<uint8_t>(ch) - 32u) * font.FontHeight;

    for (uint32_t row = 0; row < font.FontHeight; ++row)
    {
        uint16_t bits = font.data[charOffset + row];

        for (uint32_t col = 0; col < font.FontWidth; ++col)
        {
            // MSB of bits corresponds to the leftmost pixel of the glyph row
            Color px = ((bits << col) & 0x8000u)
                       ? color
                       : (color == Color::White ? Color::Black : Color::White);

            drawPixel(static_cast<uint8_t>(_cursorX + col),
                      static_cast<uint8_t>(_cursorY + row), px);
        }
    }

    _cursorX += font.FontWidth;
    return ch;
}

char SSD1306::writeString(const char *str, FontDef font, Color color)
{
    while (*str)
    {
        if (writeChar(*str, font, color) != *str)
            return *str;   // Return the failing character
        ++str;
    }
    return '\0';
}

// ---------------------------------------------------------------------------
// Cursor & inversion
// ---------------------------------------------------------------------------

void SSD1306::setCursor(uint8_t x, uint8_t y)
{
    _cursorX = x;
    _cursorY = y;
}

void SSD1306::invertColors()
{
    _inverted = !_inverted;
}