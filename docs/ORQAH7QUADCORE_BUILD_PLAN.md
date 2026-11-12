# Orqa H7 QuadCore — build environment & plan

## Status

- Toolchain: Arm GNU Toolchain 13.3.Rel1 under `tools/` (`make SDK=arm platform-tools-install`)
- Config source: `configs/ORQA/ORQAH7QUADCORE/config.h` from `cursor/orqa-config-3ad0` / pinio branch hwdef, rebased onto current config tip as `cursor/orqa-config-e068`
- Verified local build on this branch:

```bash
export PATH="$(pwd)/tools/arm-gnu-toolchain-13.3.rel1-x86_64-arm-none-eabi/bin:$PATH"
make configs
make ORQAH7QUADCORE
# -> obj/betaflight_2026.12.0-alpha_STM32H743_ORQAH7QUADCORE.hex
```

## Board facts (from Orqa / ArduPilot OrqaH7QuadCore)

| Item | Value |
|------|--------|
| MCU | STM32H743 |
| IMU | Dual ICM-42688 (SPI1 + SPI4) |
| Baro | DPS310 (I2C2) |
| OSD | AT7456E (SPI3) |
| Storage | W25Q128 flash (SPI2) + microSD (SDMMC1) |
| RC default | UART3 (GHST / half-duplex TX) |
| GPS | UART7 |
| ESC telem | UART8 |
| SiK / radio | UART6 |
| PINIO1 | PD0 (cam switch) |
| HSE | 8 MHz |

## Build commands

```bash
# One-time environment (already done in this Cloud Agent image)
sudo apt-get install -y build-essential clang-18 g++-14 cmake curl \
  gdb-multiarch git libblocksruntime-dev python3 python3-intelhex xz-utils
git submodule update --init -- src/config lib/modules/dronecan/libcanard
make SDK=arm platform-tools-install

# Everyday Orqa H7 QuadCore build (APEX branch)
export PATH="$(pwd)/tools/arm-gnu-toolchain-13.3.rel1-x86_64-arm-none-eabi/bin:$PATH"
make ORQAH7QUADCORE
# or: make CONFIG=ORQAH7QUADCORE

# Flash (DFU: hold BOOT, plug USB-C)
make ORQAH7QUADCORE_flash   # or dfu_flash / configurator local hex
```

## Follow-up work before flight / cloud-build submission

1. **BOARD_NAME vs directory** — leaf dir is `ORQAH7QUADCORE` but `BOARD_NAME` is `ORQAH7`. Align them (`BOARD_NAME ORQAH7QUADCORE` or rename the directory).
2. **UART6 TX pin** — config has `PC5`; ArduPilot hwdef uses `PC6` for USART6_TX. Confirm against schematic / working 4.4.1 dump.
3. **GPS UART** — config sets `GPS_UART` and `SERIALRX_UART` both to USART3. ArduPilot maps GPS to UART7; likely should be `SERIAL_PORT_USART7`.
4. **TIMER_PIN_MAPPING** — missing. Add timer/DMA map (motors PD12/PD13, PA1/PA0, PA2/PA3, PB1/PB0; servos PE6/PE5) via `EXTRA_FLAGS=-DUSE_TIMER_MAP_PRINT` on hardware, then bake into `config.h`.
5. **Defaults cleanup** — drop unused/duplicate `USE_RX_VTX_HYBRID`; set `SYSTEM_HSE_MHZ 8`; enable GHST if that remains the stock RC path; set blackbox default (SD vs flash).
6. **APEX on this board** — H743 has 2 MB flash so `USE_APEX` stays enabled; assign `apex_uart` to a free UART (likely UART6 / SiK port) when integrating Adaptive Payload EXchange.
7. **Hardware bring-up checklist** — DFU flash → MSP connect → gyro/baro/OSD/SD detect → motors (props off) → RX on UART3 → GPS on UART7 → PINIO cam switch.

## Reference

- Config branch: `cursor/orqa-config-e068` (content from `cursor/orqa-config-3ad0`)
- Pinio / Battery Heat branch that carried the original submodule pointer: `cursor/pinio-osd-gpio-3ad0`
- ArduPilot hwdef: `libraries/AP_HAL_ChibiOS/hwdef/OrqaH7QuadCore/hwdef.dat`
