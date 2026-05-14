# M5Unit-CardKB2-UserDemo

**Factory firmware** for the [M5Stack Unit CardKB2](https://docs.m5stack.com/en/products/sku/u215) (**SKU: U215**), built with [ESP-IDF](https://github.com/espressif/esp-idf). It provides matrix keyboard scanning, RGB status LED, and key output over **I2C / UART / ESP-NOW / BLE HID**—including manufacturing / QA hooks such as I2C-triggered production-test mode.

-----------------
## Operating mode

| Mode | Description |
|------|-------------|
| **I2C** (default) | I2C slave reports characters / key data to the host. |
| **UART** | Custom frames with key index and press / release over UART. |
| **ESP-NOW** | Broadcasts key frames (similar framing to UART). |
| **BLE HID** | Bluetooth HID keyboard, including arrow keys via Fn combos. |

### Switching modes

Hold **Fn + Sym**, then press **1 / 2 / 3 / 4** on the top number row to select **I2C / UART / ESP-NOW / BLE HID** respectively.

**Note:** **Fn + 1** alone (top-left number key) sends **ESC** in most modes; it does **not** switch modes.

--------------------
## Build environment

* idf version: ESP-IDF v6.1-dev-dirty

```bash
cd src
idf.py set-target esp32c61
idf.py build
idf.py -p <PORT> flash monitor
```

## License

This project is licensed under the MIT License—see [LICENSE](LICENSE).
