# ES8311 驱动（第三方源码，原样引入）

- **来源**：https://github.com/waveshareteam/ESP32-S3-LCD-0.85
  `example/Arduino-3.2.0/examples/01_i2s_audio/{es8311.h,es8311_reg.h,es8311.cpp}`
- **原始出处**：Espressif Systems，SPDX-License-Identifier: **Apache-2.0**
- **引入日期**：2026-08-04
- **本地改动**：无（原样引入）

## 注意

Waveshare 把原厂驱动的 I2C 读写从 IDF 的 `i2c_master_write_to_device()`
改成了 Arduino 的 `Wire`（见 `es8311.cpp` 中被注释掉的原实现）。

因此 **调用任何 es8311_* 函数前必须先 `Wire.begin(SDA, SCL)`**，
否则 I2C 传输会静默失败。`bsp_audio_init()` 已经处理了这一点。

`es8311_create()` 的 `i2c_port` 参数在这个改版里实际未被使用，
但仍需传入合法值（`I2C_NUM_0`）。
