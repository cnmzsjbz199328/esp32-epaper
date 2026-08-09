#include "audio_id.h"

#include <Wire.h>
#include "bsp_pins.h"

#define BSP_ES8311_ADDR      0x18
#define BSP_ES8311_CHIP_ID1  0x83
#define BSP_ES8311_CHIP_ID2  0x11

static bool read_reg(uint8_t reg, uint8_t* out)
{
    Wire.beginTransmission(BSP_ES8311_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((int)BSP_ES8311_ADDR, 1) != 1) return false;
    *out = Wire.read();
    return true;
}

bool bsp_audio_chip_id_read(uint8_t* id1, uint8_t* id2, uint8_t* ver)
{
    Wire.begin(BSP_PIN_I2C_SDA, BSP_PIN_I2C_SCL, BSP_I2C_FREQ);
    uint8_t a = 0, b = 0, v = 0;
    if (!read_reg(0xFD, &a)) return false;
    if (!read_reg(0xFE, &b)) return false;
    if (!read_reg(0xFF, &v)) return false;
    if (id1) *id1 = a;
    if (id2) *id2 = b;
    if (ver) *ver = v;
    return true;
}

