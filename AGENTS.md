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
  - PC7 -> TFT DC/RS
  - PA9 -> TFT RESET

GPS:
- Module: ATGM336H.
- GPS TX is currently wired to Arduino D2 / PA10.
- Firmware uses USART1_RX on PA10 for GPS receive.
- GPS GND must share Nucleo GND.
- USART2 RX on PA3 / Arduino D0 was avoided because the Nucleo ST-LINK virtual COM path held/interfered with that pin.

Future RPM:
- Sensor: IFM IFC204 Hall sensor.
- Not implemented yet.

## Current Firmware Structure

Core/Inc:
- `st7796.h`: ST7796 display API and RGB565 color constants.
- `gps.h`: GPS data model and UART callback interface.
- `dash_ui.h`: dashboard UI API.
- `main.h`: CubeMX pin defines and HAL include.

Core/Src:
- `st7796.c`: ST7796 SPI driver.
- `gps.c`: interrupt-driven NMEA/RMC parser.
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
- `gps.c` parses RMC and VTG speed sentences. GPS speed can still lag acceleration because most modules output fixes at about 1 Hz by default and the receiver itself filters/solves motion.

`GPS_DEBUG_SHOW_RX_COUNT_AS_SPEED` in `main.c` should normally be `0U`. If set to `1U`, it shows UART byte count as fake speed until a valid GPS speed is parsed, which causes a startup number "spasm" after flashing/reset. Use it only for UART debugging.

## UI Notes

The UI is in `dash_ui.c`.

Current design:
- Clean circular speed gauge around the center speed.
- Heavy block digits drawn from a tiny custom 5x7 bitmap digit font.
- GPS diagnostic bars are near the lower center under the MPH label.
- The display driver has no framebuffer. Drawing is direct over SPI using filled rectangles and pixels.
- The UI now uses smoother primitives where useful: a quiet outer arc, thick swept speed arc, a few radial reference ticks, rounded diagnostic pills, a slim horizontal speed bar, and lightly rounded digit cells.
- The top decorative LED row and dense circular dot clutter were removed because they made the speedometer screen too busy.
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

## Build

CubeIDE generated the Debug makefile. If `make` is not on PATH, use CubeIDE's bundled make and GCC from:

- `C:\ST\STM32CubeIDE_1.19.0\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.make.win32_2.2.0.202409170845\tools\bin\make.exe`
- `C:\ST\STM32CubeIDE_1.19.0\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.13.3.rel1.win32_1.0.0.202411081344\tools\bin`

Build command from `Debug`:

```powershell
$env:PATH = 'C:\ST\STM32CubeIDE_1.19.0\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.13.3.rel1.win32_1.0.0.202411081344\tools\bin;' + $env:PATH
& 'C:\ST\STM32CubeIDE_1.19.0\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.make.win32_2.2.0.202409170845\tools\bin\make.exe' all -j8
```

## Collaboration Preference

When changing this repo:
- Teach while coding. Explain HAL, UART, SPI transactions, display registers, RGB565 colors, and CubeMX regeneration boundaries.
- Keep edits scoped.
- Prefer small, testable steps.
- Build after firmware changes when possible.
