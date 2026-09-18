#include "touch.h"

#include <Wire.h>
#include <Arduino.h>
#include "bsp_pins.h"

static bool s_ready = false;
static bool s_last_down = false;

static void ft6336_reset(void)
{
    pinMode(BSP_PIN_TOUCH_INT, INPUT_PULLUP);
    pinMode(BSP_PIN_TOUCH_RST, OUTPUT);
    digitalWrite(BSP_PIN_TOUCH_RST, HIGH);
    delay(100);
    digitalWrite(BSP_PIN_TOUCH_RST, LOW);
    delay(100);
    digitalWrite(BSP_PIN_TOUCH_RST, HIGH);
    delay(100);
}

static bool reg_read(uint8_t reg, uint8_t* buf, size_t len)
{
    Wire.beginTransmission(BSP_TOUCH_I2C_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((int)BSP_TOUCH_I2C_ADDR, (int)len) != (int)len) return false;
    for (size_t i = 0; i < len; i++) buf[i] = Wire.read();
    return true;
}

bool bsp_touch_init(void)
{
    s_ready = false;
    ft6336_reset();
    Wire.begin(BSP_PIN_I2C_SDA, BSP_PIN_I2C_SCL, BSP_I2C_FREQ);

    uint8_t head[7] = {0};
    if (!reg_read(0x00, head, sizeof(head))) {
        Serial.printf("[touch] FT6336 @0x%02X read failed\n", BSP_TOUCH_I2C_ADDR);
        return false;
    }

    const uint8_t points = head[2] & 0x0F;
    const uint16_t x = (uint16_t)(((head[3] & 0x0F) << 8) | head[4]);
    const uint16_t y = (uint16_t)(((head[5] & 0x0F) << 8) | head[6]);
    if (points > 2 || (points > 0 && (x >= BSP_EPD_W || y >= BSP_EPD_H))) {
        Serial.printf("[touch] FT6336 odd boot sample n%u x%u y%u\n", points, x, y);
    }

    s_ready = true;
    Serial.printf("[touch] FT6336 ready: n%u int%d\n", points, digitalRead(BSP_PIN_TOUCH_INT));
    return true;
}

bool bsp_touch_ready(void)
{
    return s_ready;
}

bool bsp_touch_raw(uint8_t out[7])
{
    if (!s_ready || !out) return false;
    return reg_read(0x00, out, 7);
}

bool bsp_touch_read(bsp_touch_point_t* p)
{
    if (p) {
        p->down = false;
        p->x = 0;
        p->y = 0;
        p->gesture = 0;
        p->points = 0;
    }
    if (!s_ready) return false;

    uint8_t head[7] = {0};
    if (!reg_read(0x00, head, sizeof(head))) return false;

    const uint8_t points = head[2] & 0x0F;
    const uint16_t x = (uint16_t)(((head[3] & 0x0F) << 8) | head[4]);
    const uint16_t y = (uint16_t)(((head[5] & 0x0F) << 8) | head[6]);
    if (p) {
        p->points = points;
        p->gesture = head[1];
        p->x = x;
        p->y = y;
        p->down = points > 0 && points <= 2 && x < BSP_EPD_W && y < BSP_EPD_H;
        if (p->down != s_last_down) {
            Serial.printf("[touch] %s n%u x%u y%u\n",
                          p->down ? "down" : "up", points, x, y);
            s_last_down = p->down;
        }
    }
    return points > 0 && points <= 2 && x < BSP_EPD_W && y < BSP_EPD_H;
}

void bsp_touch_monitor(void)
{
    if (!s_ready) return;
    static uint32_t last = 0;
    bsp_touch_point_t p;
    if (bsp_touch_read(&p)) {
        Serial.printf("[touch] n%u g%02X x%u y%u\n", p.points, p.gesture, p.x, p.y);
        last = millis();
    } else if (millis() - last > 1000) {
        last = millis();
    }
}
