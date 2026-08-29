#include "bsp.h"

#include <Arduino.h>
#include <Wire.h>

#include "bsp_pins.h"

namespace {

uint8_t crc8(const uint8_t* data, size_t len)
{
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; bit++) {
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

bool command(uint16_t value)
{
    Wire.beginTransmission(BSP_SHTC3_I2C_ADDR);
    Wire.write((uint8_t)(value >> 8));
    Wire.write((uint8_t)value);
    return Wire.endTransmission() == 0;
}

bool read_bytes(uint8_t* out, size_t len)
{
    if (Wire.requestFrom((int)BSP_SHTC3_I2C_ADDR, (int)len) != (int)len) return false;
    for (size_t i = 0; i < len; i++) out[i] = Wire.read();
    return true;
}

}

bool bsp_shtc3_read(float* t_c, float* rh)
{
    if (!t_c || !rh) return false;
    Wire.begin(BSP_PIN_I2C_SDA, BSP_PIN_I2C_SCL, BSP_I2C_FREQ);

    if (!command(0x3517)) return false;
    delay(2);
    if (!command(0x7866)) {
        (void)command(0xB098);
        return false;
    }
    delay(15);

    uint8_t raw[6] = {0};
    const bool ok = read_bytes(raw, sizeof(raw));
    (void)command(0xB098);
    if (!ok || crc8(raw, 2) != raw[2] || crc8(raw + 3, 2) != raw[5]) return false;

    const uint16_t raw_t = ((uint16_t)raw[0] << 8) | raw[1];
    const uint16_t raw_h = ((uint16_t)raw[3] << 8) | raw[4];
    *t_c = -45.0f + 175.0f * ((float)raw_t / 65536.0f);
    *rh = 100.0f * ((float)raw_h / 65536.0f);
    return *t_c > -40.0f && *t_c < 125.0f && *rh >= 0.0f && *rh <= 100.0f;
}
