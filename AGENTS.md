# Sea-Doo Digital Dashboard Context

This repo is an STM32CubeIDE 1.19 project for a Sea-Doo digital dashboard.

## Project Basics

- MCU: STM32 Nucleo-F446RE.
- Language: C only.
- Framework: STM32 HAL plus CubeMX-generated setup.
- Keep `main.c` small. Put display, GPS, and future RPM logic in modules.
- Preserve CubeMX `USER CODE` sections.
- Do not rewrite the ST7796 display driver from scratch unless necessary.

## Hardware

Display:
- LCDWiki / Hosyond 4.0 inch SPI TFT.
- Controller: ST7796S.
- SPI1 wiring:
  - PA5 -> TFT SCK
  - PA7 -> TFT SDI/MOSI
  - PA6 -> TFT SDO/MISO, optional/unused
  - PB5 -> TFT CS
  - PC9 -> TFT DC/RS (carrier PCB routing; older firmware incorrectly used PC7)
  - PA9 -> TFT RESET

GPS:
- Module: ATGM336H.
- GPS TX is currently wired to Arduino D2 / PA10.
- Firmware uses USART1_RX on PA10 for GPS receive.
- GPS GND must share Nucleo GND.
- USART2 RX on PA3 / Arduino D0 was avoided because the Nucleo ST-LINK virtual COM path held/interfered with that pin.
- Optional GPS command path: PA2 / Arduino D1 / USART2_TX -> GPS RX.

SD card:
- Shares SPI1 with the TFT: PA5 SCK, PA7 MOSI, PA6 MISO.
- PB4 -> SD CS; TFT remains on PB5 CS.
- `sd_card.c` proves SPI initialization and sector reads, but no FAT filesystem/logger exists yet.

ESP32:
- Module: Seeed Studio XIAO ESP32-C6, built with Arduino ESP32 core 3.3.13.
- STM32 PC6 / USART6_TX -> XIAO D7 / GPIO17 RX.
- XIAO D6 / GPIO16 TX -> STM32 PC7 / USART6_RX.
- UART link is 115200 8N1. `esp_link.c` parses heartbeat and passive scan frames.
- ESP sketch: `esp32/xiao_c6_link_test/xiao_c6_link_test.ino`.

Future RPM:
- Sensor: IFM IFC204 Hall sensor.
- Not implemented yet.

## Current Firmware Structure

Core/Inc:
- `st7796.h`: ST7796 display API and RGB565 color constants.
- `gps.h`: GPS data model and UART callback interface.
- `esp_link.h`: ESP32 carrier UART parser and scan counters.
- `sd_card.h`: raw SPI SD initialization/read interface.
- `splash_image.h`: temporary indexed-RLE startup image API.
- `dash_ui.h`: dashboard UI API.
- `main.h`: CubeMX pin defines and HAL include.

Core/Src:
- `st7796.c`: ST7796 SPI driver.
- `gps.c`: interrupt-driven RMC/GGA/GSA/GSV/VTG parser.
- `esp_link.c`: interrupt-driven USART6 line receiver for ESP32 frames.
- `sd_card.c`: shared-SPI SD card initialization, read, and status polling.
- `splash_image.c`: temporary RLE splash image data.
- `dash_ui.c`: dashboard drawing and circular gauge UI.
- `main.c`: HAL init, module init, and main loop.
- `stm32f4xx_hal_msp.c`: low-level peripheral pin/IRQ setup.
- `stm32f4xx_it.c`: interrupt handlers.

## GPS Debug History

The display and SPI path are proven working.

GPS debugging found:
- PA3 / D0 read high even disconnected because of the Nucleo USART2/ST-LINK VCP path.
- Raw GPIO edge testing on PA10 / D2 proved the GPS TX signal was active.
- Firmware was converted to USART1_RX on PA10.
- When GPS is working:
  - left GPS status bar = UART bytes received
  - middle GPS status bar = complete NMEA line received
  - right GPS status bar = valid RMC fix
- `gps.c` parses speed/course, latitude/longitude, altitude, fix quality, satellite counts, DOP, and GSV SNR data.
- Startup requests RMC + GGA at 5 Hz and GSA + GSV about once per second. This rate was chosen to stay within 9600 baud.
- Course is course-over-ground, not a magnetic compass heading; it is unreliable while stationary.
- Stationary GPS position wander can produce a few MPH of false speed. A low-speed deadband is recommended but not implemented yet.

`GPS_DEBUG_SHOW_RX_COUNT_AS_SPEED` in `main.c` should normally be `0U`. If set to `1U`, it shows UART byte count as fake speed until a valid GPS speed is parsed, which causes a startup number "spasm" after flashing/reset. Use it only for UART debugging.

## UI Notes

The UI is in `dash_ui.c`.

Current design:
- Clean circular speed gauge around the center speed.
- Heavy block digits drawn from a tiny custom 5x7 bitmap digit font.
- GPS diagnostic bars are near the lower center under the MPH label.
- The display driver has no framebuffer. Drawing is direct over SPI using filled rectangles and pixels.
- The UI now uses smoother primitives where useful: a quiet outer arc, light grey gauge border, thick swept speed arc, a few radial reference ticks, rounded diagnostic pills, and lightly rounded digit cells.
- The top decorative LED row and dense circular dot clutter were removed because they made the speedometer screen too busy.
- The bottom horizontal speed bar was removed to keep the MPH screen cleaner.
- The Nucleo blue user button cycles three screens: MPH gauge -> Wi-Fi scan -> satellite quality -> MPH gauge.
- The MPH screen includes `HDG ---` / `HDG 000..359` but intentionally has no AP counter.
- When GPS speed is invalid, the MPH screen replaces stale digits with a cached `NO GPS FIX` message and restores the gauge when data returns.
- Wi-Fi scan mode shows AP count, scan/UART counters, latitude, longitude, altitude, and satellites used.
- Satellite mode shows fix type, satellites used/in view, four strongest SNR bars, HDOP, and HDOP-derived estimated accuracy.
- SD and ESP status badges remain visible; the three GPS diagnostic pills show bytes, lines, and valid fix.
- Speed digit redraw now erases only the old digit cells, then restores gauge arcs/ticks, so the gauge ring is not damaged by a large black clear rectangle.
- GPS startup calls `GPS_SendStartupConfig(&huart2)` before `GPS_Init(&huart1)`; configuration requires PA2 / D1 connected to GPS RX.
- `st7796.c` now also exposes helper primitives for future UI polish:
  - `ST7796_RGB565`
  - `ST7796_DimColor`
  - `ST7796_DrawLine`
  - `ST7796_DrawThickLine`
  - `ST7796_DrawCircle`
  - `ST7796_FillCircle`
  - `ST7796_FillRoundRect`
  - `ST7796_DrawRoundRect`
  - `ST7796_PointOnCircle`
  - `ST7796_DrawArc`
  - `ST7796_DrawThickArc`
  - `ST7796_DrawRadialTick`
  - `ST7796_DrawGaugeTicks`
  - `ST7796_DrawNeedle`
  - `ST7796_DrawLedBar`
  - `ST7796_FillHorizontalBar`
- `st7796.c` also has a reusable 5x7 bitmap text layer:
  - `ST7796_DrawChar5x7`
  - `ST7796_DrawString5x7`
  - `ST7796_TextWidth5x7`
  - It supports digits, uppercase letters, space, dash, dot, colon, slash, and percent. Use the `scale` argument to make it larger.
- JPG/background image support is not implemented yet. It is possible on SPI TFTs, but it needs an image decoder and a storage plan, so prefer primitives for gauges and live data first.

## ESP32 Passive Scanner

- Passive Wi-Fi scanning only: no association, injection, or deauthentication.
- Frames sent to STM32 include `C6,READY`, `WD,BEGIN`, `WD,AP`, and `WD,END`.
- AP records contain scan ID, channel, RSSI, security code, BSSID, and CSV-sanitized SSID.
- GPS-tagged FAT/CSV logging to SD is the next major feature; it is not implemented yet.
- The current CHUD Dynamics splash remains the default. A future user-supplied image should be added as an alternate selectable splash, not overwrite the default asset.

## Build

CubeIDE generated the Debug makefile. If `make` is not on PATH, use CubeIDE's bundled make and GCC from:

- Current desktop installation is on `Q:\STM32CubeIDE_1.19.0`; other machines may use `C:\ST\STM32CubeIDE_1.19.0`.
- Use CubeIDE's bundled GNU make and GCC plugin directories.

Build command from `Debug`:

```powershell
$env:PATH = '<CubeIDE>\plugins\com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.13.3.rel1.win32_1.0.0.202411081344\tools\bin;' + $env:PATH
& '<CubeIDE>\plugins\com.st.stm32cube.ide.mcu.externaltools.make.win32_2.2.0.202409170845\tools\bin\make.exe' all -j8
```

The generated Debug makefile uses the portable linker script path `../STM32F446RETX_FLASH.ld`.
On the current Windows desktop, mandatory ASLR causes legacy ST tools to fail with error 623. The local workaround temporarily disables `ForceRelocateImages` only for the affected executable, performs the build/flash, and restores it in `finally`. Do not leave the mitigation disabled.

## Collaboration Preference

When changing this repo:
- Teach while coding. Explain HAL, UART, SPI transactions, display registers, RGB565 colors, and CubeMX regeneration boundaries.
- Keep edits scoped.
- Prefer small, testable steps.
- Build after firmware changes when possible.
