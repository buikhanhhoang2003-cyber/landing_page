# Smart Datalogger Codebase Analysis

This repository is a mixed embedded project centered on an STM32 microcontroller and a separate ESP32 project skeleton. The most complete and active logic lives in the STM32 firmware under [Script_STM32](Script_STM32), where the device acts as a smart monitor/controller with:

- A 320x240 color TFT display
- ADC and digital input monitoring
- Relay and DAC output control
- A MLX90640 thermal camera over I2C
- A data-logger / upload UI placeholder

## 1. Repository structure

- [Script_STM32](Script_STM32) — main firmware project for the STM32 board.
  - [Script_STM32/Core/Src/main.c](Script_STM32/Core/Src/main.c) — the main control loop, UI state machine, ADC reading, thermal camera processing, and output control logic.
  - [Script_STM32/Core/Inc/main.h](Script_STM32/Core/Inc/main.h) — pin definitions and HAL includes.
  - [Script_STM32/Core/Src/ILI9341_STM32_Driver.c](Script_STM32/Core/Src/ILI9341_STM32_Driver.c) — low-level LCD driver for the ILI9341 controller.
  - [Script_STM32/Core/Src/ILI9341_GFX.c](Script_STM32/Core/Src/ILI9341_GFX.c) — graphics primitives for drawing text, rectangles, lines, and circles.
  - [Script_STM32/Core/Src/MLX90640_API.c](Script_STM32/Core/Src/MLX90640_API.c) — thermal camera calibration and temperature computation routines.
  - [Script_STM32/Core/Src/MLX90640_I2C_Driver.c](Script_STM32/Core/Src/MLX90640_I2C_Driver.c) — I2C access layer for the MLX90640.
- [esp32_smartlogger](esp32_smartlogger) — ESP32 firmware skeleton.
  - [esp32_smartlogger/platformio.ini](esp32_smartlogger/platformio.ini) — PlatformIO config.
  - [esp32_smartlogger/src/main.cpp](esp32_smartlogger/src/main.cpp) — currently empty placeholder.
- Root docs: [Diagram.pdf](Diagram.pdf), [Schematic .pdf](Schematic%20.pdf), [BLOCK V1.pdf](BLOCK%20V1.pdf), [How_to_read_this_document.pdf](How_to_read_this_document.pdf) — likely hardware and board documentation.

## 2. What the STM32 project does

The board is acting as a smart monitoring and control panel. The runtime loop in [Script_STM32/Core/Src/main.c](Script_STM32/Core/Src/main.c) continuously:

1. Reads digital inputs.
2. Reads ADC channels.
3. Cycles through UI modes.
4. Updates the thermal camera view when in thermal mode.
5. Drives DAC/relay outputs in output-control mode.

The system is organized around a central `AppState_t` structure and a `ThermalState_t` structure, which hold dynamic runtime state like ADC values, current UI mode, thermal statistics, and output status.

## 3. Main runtime flow

The program entry point is `main()` in [Script_STM32/Core/Src/main.c](Script_STM32/Core/Src/main.c).

### Startup sequence

- `HAL_Init()` initializes the HAL and Systick timer.
- `SystemClock_Config()` configures the MCU clock tree.
- `MX_*_Init()` initializes GPIO, DMA, ADC, CAN, DAC, SPI, UART, and I2C.
- `ILI9341_Init()` initializes the display controller.
- `UI_Init()` configures the UI and draws the startup menu.
- ADC calibration is started for both ADC channels.

### Main loop behavior

The infinite loop does the following every iteration:

- Reads `HAL_GetTick()` as `now`.
- Reads digital input pins using a 10 ms period.
- Reads ADC1 and ADC2 using a 20 ms period.
- Converts ADC values to millivolts.
- Uses the raw ADC value to update the DAC code.
- Decides the active mode by elapsed time from a menu start tick.
- Draws a UI mode when the mode changes.
- Runs mode-specific updates:
  - Mode 1: dashboard display refresh
  - Mode 2: thermal camera processing
  - Mode 3: output control screen and relay/DAC updates
  - Mode 4: placeholder for data logging

## 4. UI architecture

The UI code is centered around a set of drawing helpers in [Script_STM32/Core/Src/main.c](Script_STM32/Core/Src/main.c):

- `UI_Init()`
- `UI_DrawModeMenu()`
- `UI_DrawDashboard()`
- `UI_DrawThermalCamera()`
- `UI_DrawActiveControl()`
- `UI_DrawDataLogger()`
- `UI_DrawCurrentMode()`
- `UI_DrawSensorCard()`
- `UI_DrawRelay()`

### UI modes

The code defines a `currentMode` state with these intended modes:

- `0`: mode menu
- `1`: sensor monitor dashboard
- `2`: thermal camera view
- `3`: output control view
- `4`: data logger view

A basic timed mode rotation is implemented:

- Under 10 seconds: menu mode
- Next 3 seconds: dashboard
- Next 3 seconds: thermal camera
- Next 3 seconds: output control
- Then repeat

This means the display is designed to cycle through screens automatically, even though the user can also navigate via the mode selection concept.

## 5. Hardware I/O model

The board pin map is declared in [Script_STM32/Core/Inc/main.h](Script_STM32/Core/Inc/main.h). The main external interfaces are:

- ADC1 / ADC2: measurement inputs
- GPIO digital inputs: X1–X4
- GPIO outputs: Y1–Y4 relays or output channels
- I2C1: MLX90640 thermal sensor
- SPI2: TFT display
- CAN / UART / USART: communication interfaces configured but not deeply used in the visible logic
- DAC channel 1 with DMA: analog output generation

### GPIO grouping

The code defines `IO4State_t` to pack four digital bits into one byte:

- `ch1`..`ch4`
- Used for status of the four digital inputs/outputs.

This is a compact pattern useful on embedded systems where bitfields are cheaper than full structs.

## 6. Thermal camera subsystem

The thermal system is the most substantive functional block after the UI.

### Relevant files

- [Script_STM32/Core/Src/main.c](Script_STM32/Core/Src/main.c)
- [Script_STM32/Core/Src/MLX90640_API.c](Script_STM32/Core/Src/MLX90640_API.c)
- [Script_STM32/Core/Src/MLX90640_I2C_Driver.c](Script_STM32/Core/Src/MLX90640_I2C_Driver.c)
- [Script_STM32/Core/Inc/MLX90640_API.h](Script_STM32/Core/Inc/MLX90640_API.h)

### Thermal state

`ThermalState_t` stores:

- `params` — MLX90640 calibration parameters
- `frameData[834]` — raw thermal frame data
- `temperature[768]` — computed temperature array for all pixels
- `minTemp`, `maxTemp`, `centerTemp`, `ambientTemp`
- `lastFrameTick`, `lastInitTick`
- `error`, `frameErrorCount`, `subpageMask`, `initialized`

### Thermal flow

1. `Thermal_Init()` checks whether the MLX90640 exists on the I2C bus.
2. It initializes the I2C driver and reads the EEPROM via `MLX90640_DumpEE()`.
3. It extracts calibration + compensation parameters with `MLX90640_ExtractParameters()`.
4. It sets refresh rate and chess mode.
5. If init succeeds, `thermal.initialized = 1`.

During updates:

1. `Thermal_Update()` is called in mode 2.
2. If not initialized, it retries once per second.
3. Every 250 ms it calls `MLX90640_GetFrameData()`.
4. It calculates ambient temperature using `MLX90640_GetTa()`.
5. It computes the per-pixel temperatures using `MLX90640_CalculateTo()`.
6. It corrects broken/outlier pixels with `MLX90640_BadPixelsCorrection()`.
7. When both subpages are collected, it updates statistics and draws the thermal image.

### Thermal display rendering

The thermal image is drawn as a 32x24 grid, where each pixel is rendered as a block:

- `THERMAL_IMAGE_X = 32`
- `THERMAL_IMAGE_Y = 40`
- `THERMAL_PIXEL_WIDTH = 8`
- `THERMAL_PIXEL_HEIGHT = 6`

`Thermal_GetColor()` converts temperature values to a blue-to-red heat map. `Thermal_UpdateStatistics()` computes min, max, and center-temperature values. The screen displays `MIN`, `CTR`, and `MAX` values at the bottom of the thermal panel.

## 7. Driver layer

### LCD driver

[Script_STM32/Core/Src/ILI9341_STM32_Driver.c](Script_STM32/Core/Src/ILI9341_STM32_Driver.c) contains the low-level SPI driver for the ILI9341 LCD. It provides:

- `ILI9341_WriteCommand()`
- `ILI9341_WriteData()`
- `ILI9341_WriteBuffer()`
- `ILI9341_SetAddress()`
- `ILI9341_Reset()`
- `ILI9341_Enable()`
- `ILI9341_Init()`

This layer abstracts the physical LCD communications so the rest of the firmware can simply call drawing helpers.

### MLX90640 driver

[Script_STM32/Core/Src/MLX90640_I2C_Driver.c](Script_STM32/Core/Src/MLX90640_I2C_Driver.c) wraps the hardware-specific I2C access for the thermal sensor. It exposes wrappers like:

- `MLX90640_I2CRead()`
- `MLX90640_I2CWrite()`
- `MLX90640_I2CInit()`

This is important because the high-level API layer in [Script_STM32/Core/Src/MLX90640_API.c](Script_STM32/Core/Src/MLX90640_API.c) expects a clean I2C implementation and does not deal with STM32-specific register details.

## 8. ADC and sensor reading logic

Two ADC channels are used in the main loop:

- `app.adc.mv[0]`
- `app.adc.mv[1]`

These are read every `ADC_READ_PERIOD_MS = 20ms` and scaled to millivolts using:

- `ADC_VREF_MV = 3300`
- `ADC_MAX_VALUE = 4095`

This value is then re-used to compute the DAC code in `app.dac.sample`:

- `app.dac.sample = ((uint32_t)app.adc.mv[0] * ADC_MAX_VALUE) / ADC_VREF_MV;`

This implies the first ADC input is effectively being used as a reference/control source for the DAC output.

## 9. Output control mode

Mode 3 is dedicated to output testing/control. It reads the states of output pins and renders a UI with:

- relay state cards for R1..R4
- DAC code
- DAC voltage
- DMA status
- communication output placeholders

When mode 3 becomes active, the code starts the DAC DMA stream:

- `HAL_DAC_Start_DMA(&hdac, DAC_CHANNEL_1, (uint32_t *)&app.dac.sample, 1U, DAC_ALIGN_12B_R)`

This means the controller is generating a periodic analog value, likely used to test or drive hardware analog outputs.

## 10. Data logger / ESP32 upload area

Mode 4 is present but mostly a visual placeholder in [Script_STM32/Core/Src/main.c](Script_STM32/Core/Src/main.c). The card displays:

- logging interval
- status
- ESP32 upload interval
- last send time
- stored and pending record counts

This suggests the firmware was designed to upload sampled data to an ESP32, but that implementation is not complete in the current codebase.

## 11. ESP32 side

The ESP32 folder is currently minimal. [esp32_smartlogger/platformio.ini](esp32_smartlogger/platformio.ini) configures an `esp32dev` board with the Arduino framework, but [esp32_smartlogger/src/main.cpp](esp32_smartlogger/src/main.cpp) is empty. So the ESP32 side appears to be a planned or partially started companion device rather than a working implementation yet.

## 12. Likely architecture intent

This project reads like a combined industrial sensor demo / laboratory instrument:

- A microcontroller board monitors analog/digital values.
- A color TFT presents several operational modes.
- A thermal camera provides visual temperature mapping.
- The system can display and control outputs.
- It may eventually log and upload data to a wireless node.

In short: it is a smart monitoring/control platform, not a conventional application framework.

## 13. Good mental model for reading the code

Think of the firmware as a single-file state-driven controller, with these layers:

- Hardware init layer: HAL, clock tree, peripheral setup
- UI layer: drawing functions and screen state changes
- Control layer: ADC + digital input reads
- Thermal layer: MLX90640 calibration and temperature rendering
- Output layer: DAC and relay state handling
- Future logging layer: placeholders for storage and upload

The main complexity is concentrated in [Script_STM32/Core/Src/main.c](Script_STM32/Core/Src/main.c), which acts like a central orchestrator.

## 14. Notable implementation observations

- The project uses direct HAL calls and state-by-state polling rather than a full RTOS.
- The main loop is event-driven by time thresholds (`*_PERIOD_MS`) and mode transitions.
- The code is functional but not fully polished: some modes are placeholders, and the UI is static with automatic mode rotation.
- The thermal camera code is the most complete algorithmic subsystem in the project.

## 15. Suggested starting points for future work

If you want to understand or extend the project, start in this order:

1. [Script_STM32/Core/Src/main.c](Script_STM32/Core/Src/main.c) — central logic and runtime state
2. [Script_STM32/Core/Inc/main.h](Script_STM32/Core/Inc/main.h) — board pin mapping and hardware references
3. [Script_STM32/Core/Src/ILI9341_STM32_Driver.c](Script_STM32/Core/Src/ILI9341_STM32_Driver.c) — display hardware layer
4. [Script_STM32/Core/Src/MLX90640_API.c](Script_STM32/Core/Src/MLX90640_API.c) — thermal conversion and calibration
5. [Script_STM32/Core/Src/MLX90640_I2C_Driver.c](Script_STM32/Core/Src/MLX90640_I2C_Driver.c) — I2C low-level interface

This is the quickest path to understanding how the system behaves as a whole.
