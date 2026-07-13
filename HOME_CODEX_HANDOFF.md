# Home Codex Handoff - Sea-Doo Dashboard

Use this as the first message/context when continuing from another Codex install.

Project path:
`C:\Users\mdonahue1\STM32CubeIDE\workspace_1.19.0\jet_ski_dashboard`

## Current Hardware State

- MCU: STM32 Nucleo-F446RE.
- TFT: LCDWiki / Hosyond 4.0 inch SPI TFT, ST7796S.
- GPS: ATGM336H.
- GPS data receive is working on:
  - GPS TX -> Arduino D2 / PA10 / USART1_RX
  - GND shared
- USART2_RX / PA3 / D0 was abandoned for GPS because the Nucleo ST-LINK VCP path interfered with that pin.
- New 10 Hz GPS experiment requires command wiring:
  - Nucleo PA2 / Arduino D1 / USART2_TX -> GPS RX

## Current Firmware State

- `main.c` stays small and calls module code.
- `GPS_Init(&huart1)` receives NMEA over USART1.
- `GPS_SendStartupConfig(&huart2)` was added before `GPS_Init()` to send startup commands out USART2_TX.
- The startup config sends both PMTK and PCAS/CASIC-style commands:
  - PMTK sentence mask attempt: RMC + VTG only
  - PMTK 100 ms update attempts
  - PCAS 100 ms update attempt
- Baud is still 9600 for now. This is intentionally conservative.

## What To Test Next

1. Wire GPS RX to D1 / PA2 if not already connected.
2. Flash the firmware.
3. Watch the display GPS bars:
   - left = any UART byte
   - middle = complete NMEA line
   - right = valid RMC fix
4. Check whether speed updates feel faster than 1 Hz.
5. If GPS becomes flaky, errors rise, or bars drop:
   - 10 Hz may be too much at 9600 baud.
   - Next step is coordinated GPS + STM32 baud change to 38400 or 115200.
   - Keep sentence output reduced to RMC/VTG only.

## Display/UI State

- Current UI is a circular speed gauge with:
  - center block MPH digits
  - quiet outer gauge ring
  - light grey gauge border
  - thick swept speed arc
  - small `80` max-speed label
  - GPS status pills near the bottom
- Bottom speed bar was removed.
- Top decorative LED row and dense ring dots were removed.
- Digit redraw now erases only old digit cells instead of one large rectangle, because the large rectangle damaged the arc during 0/1/2 MPH bench-test changes.

## Build/Flash

Build from `Debug`:

```powershell
$env:PATH = 'C:\ST\STM32CubeIDE_1.19.0\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.13.3.rel1.win32_1.0.0.202411081344\tools\bin;' + $env:PATH
& 'C:\ST\STM32CubeIDE_1.19.0\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.make.win32_2.2.0.202409170845\tools\bin\make.exe' all -j8
```

Flash:

```powershell
& 'C:\ST\STM32CubeIDE_1.19.0\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer.win32_2.2.200.202503041107\tools\bin\STM32_Programmer_CLI.exe' -c port=SWD -w 'Debug\jet_ski_dashboard.elf' -v -rst
```

