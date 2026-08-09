#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <Wire.h>

class ES7210 {
public:
    ES7210(TwoWire& wire = Wire, uint8_t addr = 0x40) : _wire(wire), _addr(addr) {}

    bool init(uint8_t gain_db) {
        _wire.beginTransmission(_addr);
        if (_wire.endTransmission() != 0) return false;

        uint8_t gain = gain_db / 3;
        if (gain > 14) gain = 14;

        writeReg(0x00, 0xFF);
        writeReg(0x00, 0x41);
        writeReg(0x01, 0x3F);
        writeReg(0x09, 0x30);
        writeReg(0x0A, 0x30);
        writeReg(0x23, 0x2A);
        writeReg(0x22, 0x0A);
        writeReg(0x20, 0x0A);
        writeReg(0x21, 0x2A);

        updateReg(0x08, 0x01, 0x00);

        writeReg(0x40, 0x43);
        writeReg(0x41, 0x70);
        writeReg(0x42, 0x70);
        writeReg(0x07, 0x20);
        writeReg(0x02, 0xC1);

        for (int i = 0; i < 4; i++) updateReg(0x43 + i, 0x10, 0x00);
        writeReg(0x4B, 0xFF);
        writeReg(0x4C, 0xFF);

        updateReg(0x01, 0x0B, 0x00);
        writeReg(0x4B, 0x00);
        updateReg(0x43, 0x10, 0x10);
        updateReg(0x43, 0x0F, gain);
        updateReg(0x44, 0x10, 0x10);
        updateReg(0x44, 0x0F, gain);

        writeReg(0x12, 0x00);

        uint8_t iface = 0;
        readReg(0x11, &iface);
        iface = (iface & 0x1F) | 0x60;
        iface = (iface & 0xFC) | 0x00;
        writeReg(0x11, iface);

        uint8_t off_reg = 0;
        readReg(0x01, &off_reg);
        writeReg(0x01, off_reg);
        writeReg(0x06, 0x00);
        writeReg(0x40, 0x43);
        writeReg(0x47, 0x08);
        writeReg(0x48, 0x08);
        writeReg(0x49, 0x08);
        writeReg(0x4A, 0x08);
        writeReg(0x40, 0x43);
        writeReg(0x00, 0x71);
        writeReg(0x00, 0x41);

        return true;
    }

private:
    TwoWire& _wire;
    uint8_t  _addr;

    bool writeReg(uint8_t reg, uint8_t val) {
        _wire.beginTransmission(_addr);
        _wire.write(reg);
        _wire.write(val);
        return _wire.endTransmission() == 0;
    }

    bool readReg(uint8_t reg, uint8_t* val) {
        _wire.beginTransmission(_addr);
        _wire.write(reg);
        if (_wire.endTransmission(false) != 0) return false;
        if (_wire.requestFrom((int)_addr, 1) != 1) return false;
        *val = _wire.read();
        return true;
    }

    bool updateReg(uint8_t reg, uint8_t mask, uint8_t val) {
        uint8_t v;
        if (!readReg(reg, &v)) return false;
        v = (v & ~mask) | (val & mask);
        return writeReg(reg, v);
    }
};
