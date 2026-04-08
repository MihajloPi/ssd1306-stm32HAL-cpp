#pragma once

#include "stm32f4xx_hal.h"
#include "fonts.h"
#include <cstdint>

/**
 * @file  ssd1306.h
 * @brief SSD1306 128×64 OLED driver for STM32F411 – I²C with optional DMA.
 *
 * Key changes over the blocking original:
 *  - Horizontal addressing mode lets the entire 1 024-byte framebuffer be sent
 *    in one I²C transaction instead of eight page-by-page blocking writes.
 *  - updateScreenDMA()    – non-blocking transfer via HAL DMA.
 *  - updateScreen()       – blocking transfer (single HAL call, no loop).
 *  - isBusy()             – poll before starting a new transfer.
 *  - onTransferComplete() – call from HAL_I2C_MemTxCpltCallback().
 *
 * CubeMX setup (required for DMA)
 * --------------------------------
 *  1. Open the I²C peripheral → DMA Settings → Add TX stream.
 *     STM32F411 typical mappings:
 *       I2C1_TX  →  DMA1 Stream 6, Channel 1   (or Stream 1, Ch 0)
 *       I2C2_TX  →  DMA1 Stream 7, Channel 7
 *       I2C3_TX  →  DMA1 Stream 4, Channel 3
 *  2. Set DMA stream priority to at least Medium.
 *  3. Enable the DMA stream global interrupt in NVIC Settings.
 *  4. Enable the I²C event interrupt in NVIC Settings.
 *
 * Callback wiring (in main.c or stm32f4xx_it.c)
 * -----------------------------------------------
 *  @code
 *  extern SSD1306 display;   // your global instance
 *
 *  void HAL_I2C_MemTxCpltCallback(I2C_HandleTypeDef *hi2c)
 *  {
 *      display.OnTransferComplete(hi2c);
 *  }
 *  @endcode
 */
class SSD1306 {
public:
    // ------------------------------------------------------------------ types
    enum class Color : uint8_t {
        Black = 0x00,
        White = 0x01
    };

    // --------------------------------------------------------------- constants
    static constexpr uint8_t  I2C_ADDR = 0x78;             ///< 8-bit write addr (0x3C << 1)
    static constexpr uint16_t WIDTH    = 128;
    static constexpr uint16_t HEIGHT   = 64;
    static constexpr uint16_t BUF_SIZE = WIDTH * HEIGHT / 8; ///< 1 024 bytes

    // ------------------------------------------------------------ construction
    /**
     * @param hi2c  HAL I²C handle. Must have a DMA TX stream configured if
     *              updateScreenDMA() is to be used.
     */
    explicit SSD1306(I2C_HandleTypeDef &hi2c);

    // --------------------------------------------------------- initialisation
    /**
     * @brief  Send the init command sequence and clear the display (blocking).
     * @return true on success, false if any I²C write failed.
     */
    bool init();

    // ------------------------------------------------- non-blocking DMA update
    /**
     * @brief  Push the framebuffer to the display using DMA (non-blocking).
     *
     * The function resets the SSD1306 address window, then hands the 1 024-byte
     * buffer to HAL_I2C_Mem_Write_DMA().  It returns immediately; the hardware
     * completes the transfer in the background.
     *
     * @note   Returns false immediately (without starting a transfer) if a
     *         previous DMA operation has not yet completed.  Check isBusy() or
     *         wait for onTransferComplete() before calling again.
     *
     * @return true  DMA transfer successfully queued.
     * @return false Peripheral busy, or HAL returned an error.
     */
    bool updateScreenDMA();

    // ---------------------------------------------------- blocking sync update
    /**
     * @brief  Push the framebuffer using a blocking I²C write (no DMA).
     *
     * Useful during initialisation or in RTOS tasks that simply block until
     * the frame is committed.
     *
     * @return true on success.
     */
    bool updateScreen();

    // ------------------------------------------------------- DMA state query
    /** @return true while a DMA transfer is in progress. */
    bool isBusy() const { return _dmaInProgress; }

    /**
     * @brief  Clear the busy flag once the DMA ISR fires.
     *
     * Wire this into HAL_I2C_MemTxCpltCallback() – see the header-level
     * code example above.
     *
     * @param hi2c  Pointer provided by the HAL callback; used to match the
     *              correct peripheral when multiple I²C buses are in use.
     */
    void onTransferComplete(I2C_HandleTypeDef *hi2c);

    // --------------------------------------------------------- drawing helpers
    /**
     * @brief Fill the entire framebuffer with one colour (does not update the
     *        display – call updateScreen[DMA]() afterwards).
     */
    void fill(Color color);

    /** @brief Set/clear a single pixel in the framebuffer. */
    void drawPixel(uint8_t x, uint8_t y, Color color);

    /**
     * @brief  Render a single character at the current cursor position.
     * @return The character on success, 0 if it would exceed the display edge.
     */
    char writeChar(char ch, FontDef font, Color color);

    /**
     * @brief  Render a null-terminated string starting at the current cursor.
     * @return The character that caused a failure, or '\0' on complete success.
     */
    char writeString(const char *str, FontDef font, Color color);

    /** @brief Move the text cursor to (x, y) in pixels. */
    void setCursor(uint8_t x, uint8_t y);

    /**
     * @brief  Toggle colour inversion for all subsequent drawPixel() calls.
     *
     * Note: this is a software inversion applied at draw time; for a
     * hardware-level inversion use SSD1306 command 0xA7 instead.
     */
    void invertColors();

private:
    // ------------------------------------------------------- private helpers
    bool writeCommand(uint8_t cmd);

    /**
     * @brief  (Re-)set the SSD1306 column and page address window to cover
     *         the full 128×64 display.  Must be called before each UpdateScreen
     *         transfer so the controller's internal pointer is at (0, 0).
     */
    bool setAddressWindow();

    // ------------------------------------------------------------------ state
    I2C_HandleTypeDef &_hi2c;

    /**
     * Aligned to a 4-byte boundary.  STM32 DMA requires the source buffer to
     * reside in a DMA-accessible memory region (SRAM) and be word-aligned when
     * using the AHB/APB DMA bus on the F4 family.
     */
    alignas(4) uint8_t _buffer[BUF_SIZE]{};

    uint16_t _cursorX     = 0;
    uint16_t _cursorY     = 0;
    bool     _inverted    = false;
    bool     _initialized = false;

    /**
     * Set to true when a DMA transfer is in flight; cleared by
     * onTransferComplete().  Declared volatile because it is written from
     * inside an ISR context (via the HAL callback).
     */
    volatile bool _dmaInProgress = false;
};