#include "touch.h"

#include <Wire.h>
#include <Arduino.h>
#include "bsp_pins.h"

static bool s_ready = false;
static bool s_last_down = false;
static uint32_t s_i2c_fail_streak = 0;
static uint32_t s_i2c_fail_total = 0;
static uint32_t s_i2c_fail_logged_at = 0;

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
    const uint8_t end_err = Wire.endTransmission(false);
    bool ok = end_err == 0;
    int got = 0;
    if (ok) {
        got = Wire.requestFrom((int)BSP_TOUCH_I2C_ADDR, (int)len);
        ok = got == (int)len;
    }
    if (ok) {
        for (size_t i = 0; i < len; i++) buf[i] = Wire.read();
        if (s_i2c_fail_streak > 0) {
            /* Diagnostic added while chasing the PHOTOS touch-unresponsive bug:
             * the FT6336 shares the I2C bus (SDA47/SCL48) with the ES8311 codec,
             * which is muted/unmuted on every scene's audio start/stop. This
             * confirms whether that contention is corrupting/failing touch reads. */
            Serial.printf("[touch] i2c recovered after %lu consecutive failures\n",
                          (unsigned long)s_i2c_fail_streak);
        }
        s_i2c_fail_streak = 0;
        return true;
    }
    s_i2c_fail_streak++;
    s_i2c_fail_total++;
    const uint32_t now = millis();
    if (now - s_i2c_fail_logged_at >= 300) {
        s_i2c_fail_logged_at = now;
        Serial.printf("[touch] i2c read FAIL streak=%lu total=%lu end_err=%u got=%d\n",
                      (unsigned long)s_i2c_fail_streak, (unsigned long)s_i2c_fail_total,
                      end_err, got);
    }
    return false;
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
