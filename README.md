# SSD1306 OLED Driver — STM32F411 (I²C + DMA)

A C++ driver for the **SSD1306 128×64 OLED** display targeting the
**STM32F411** family. It uses the STM32 HAL I²C peripheral with an optional
**DMA transfer path** so the CPU is free while the display updates in the
background.

> **Based on [ssd1306-stm32HAL](https://github.com/afiskon/stm32-ssd1306)** by
> afiskon and contributors, licensed under MIT. This fork refactors the driver
> into a C++ class, switches to horizontal addressing mode, and adds non-blocking
> DMA screen updates.

---

## Related projects

- **4-wire SPI support** — [afiskon/stm32-ssd1306](https://github.com/afiskon/stm32-ssd1306),
  the upstream C library this work is derived from.
- **Custom font generator** — [the-this-pointer/glcd-font-calculator](https://github.com/the-this-pointer/glcd-font-calculator)
  for creating additional `FontDef`-compatible bitmap fonts.

---

## What changed from the original

| Area | Before | After |
|---|---|---|
| Addressing mode | Page (0x10) | **Horizontal (0x00)** |
| `updateScreen()` | 8 blocking `Mem_Write` calls in a loop | **1 blocking `Mem_Write`** call for all 1 024 bytes |
| Non-blocking update | Not available | **`updateScreenDMA()`** — returns immediately, transfer runs in background |
| DMA guard | — | **`isBusy()`** + **`OnTransferComplete()`** prevent overlapping transfers |
| Buffer alignment | Unspecified | **`alignas(4)`** — required for STM32 DMA on the AHB bus |
| `I2C_MEMADD_SIZE_8BIT` | Implicit | Explicit, matching the HAL prototype |

### Why horizontal addressing?

In the original **page addressing** mode the software must:
1. Send a set-page command.
2. Send 128 bytes of pixel data.
3. Repeat × 8 pages = **8 separate I²C transactions, 8 blocking waits**.

In **horizontal addressing** mode the SSD1306's internal pointer
auto-increments column-by-column then page-by-page across the full display.
Setting the address window once is enough; then a single 1 024-byte burst
covers everything. This is essential for DMA because HAL's
`HAL_I2C_Mem_Write_DMA()` launches a **single** DMA transfer.

---

## CubeMX configuration

### 1 — I²C peripheral

Open your I²C peripheral (e.g. **I2C1**) and set:

| Parameter | Value |
|---|---|
| I2C Speed Mode | Fast Mode (400 kHz) recommended |
| DMA Settings → TX | Add stream; e.g. DMA1 Stream 6, Channel 1 (I2C1_TX) |
| DMA Priority | Medium or High |
| NVIC → DMA stream IRQ | ✅ Enabled |
| NVIC → I2Cx EV IRQ | ✅ Enabled |

Common STM32F411 DMA mappings for I²C TX:

| Peripheral | DMA | Stream | Channel |
|---|---|---|---|
| I2C1\_TX | DMA1 | Stream 6 | Ch 1 |
| I2C1\_TX (alt) | DMA1 | Stream 1 | Ch 0 |
| I2C2\_TX | DMA1 | Stream 7 | Ch 7 |
| I2C3\_TX | DMA1 | Stream 4 | Ch 3 |

### 2 — Linker / startup

No special changes needed. The `_buffer` array lives in SRAM which is fully
DMA-accessible on all STM32F4 devices.

---

## API reference

```cpp
// Construction
SSD1306 display(hi2c1);   // pass the HAL handle

// Must call before using the display
bool ok = display.init();

// Drawing (writes to the in-RAM framebuffer only)
display.fill(SSD1306::Color::Black);
display.setCursor(0, 0);
display.writeString("Hello!", Font_7x10, SSD1306::Color::White);
display.drawPixel(63, 31, SSD1306::Color::White);
display.invertColors();   // toggle software inversion

// Push framebuffer → display (blocking)
display.updateScreen();

// Push framebuffer → display (non-blocking DMA)
if (!display.isBusy())
    display.updateScreenDMA();
```

### Method summary

| Method | Description |
|---|---|
| `init()` | Send SSD1306 init command sequence, clear display. Blocking. |
| `updateScreen()` | Blocking single-call framebuffer flush (1 024 bytes). |
| `updateScreenDMA()` | Non-blocking DMA flush. Returns `false` if busy. |
| `isBusy()` | `true` while a DMA transfer is in flight. |
| `onTransferComplete(hi2c)` | Call from `HAL_I2C_MemTxCpltCallback`. Clears busy flag. |
| `fill(color)` | Fill entire framebuffer with Black or White. |
| `drawPixel(x, y, color)` | Set/clear a single pixel. |
| `writeChar(ch, font, color)` | Render a character at the cursor; advance cursor. |
| `writeString(str, font, color)` | Render a null-terminated string. |
| `setCursor(x, y)` | Move the text cursor. |
| `invertColors()` | Toggle software colour inversion. |

---

## Wiring up the DMA callback

HAL fires `HAL_I2C_MemTxCpltCallback` when the DMA transfer completes.
Forward it to the driver so it can clear the busy flag.

**Option A — global instance (simplest)**

```c
/* main.c or stm32f4xx_it.c */
extern SSD1306 display;   // declared in main.cpp

#ifdef __cplusplus
extern "C" {
#endif

void HAL_I2C_MemTxCpltCallback(I2C_HandleTypeDef *hi2c)
{
    display.onTransferComplete(hi2c);
}

#ifdef __cplusplus
}
#endif
```

**Option B — multiple displays / no global**

Maintain a static pointer or use a registry pattern inside a thin C wrapper.

---

## Usage examples

### Bare-metal super-loop

```cpp
SSD1306 display(hi2c1);

int main(void)
{
    HAL_Init();
    SystemClock_Config();
    MX_I2C1_Init();

    display.init();

    uint32_t frame = 0;
    char buf[32];

    for (;;)
    {
        display.fill(SSD1306::Color::Black);
        display.setCursor(0, 0);

        snprintf(buf, sizeof(buf), "Frame %lu", frame++);
        display.writeString(buf, Font_7x10, SSD1306::Color::White);

        // Non-blocking: CPU returns immediately
        if (!display.isBusy())
            display.updateScreenDMA();

        // ... do other work here while the display transfers in background ...
        HAL_Delay(16);   // ~60 fps cap
    }
}
```

### FreeRTOS task

```cpp
void DisplayTask(void *arg)
{
    auto *disp = static_cast<SSD1306 *>(arg);

    for (;;)
    {
        disp->fill(SSD1306::Color::Black);
        disp->setCursor(0, 0);
        disp->writeString("RTOS demo", Font_11x18, SSD1306::Color::White);

        // Blocking update — task simply yields while HAL does the transfer
        disp->updateScreen();

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
```

---

## File listing

```
ssd1306.h      Driver class declaration
ssd1306.cpp    Driver implementation
fonts.h        FontDef struct + extern declarations (unchanged)
fonts.c        Font bitmap data (unchanged)
```

---

## Licence

MIT — use freely in commercial and personal projects.