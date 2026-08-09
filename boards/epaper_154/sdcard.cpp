#include "sdcard.h"

#include <SD_MMC.h>
#include <string.h>
#include "bsp_pins.h"

static bool s_ready = false;

bool bsp_sd_init(void)
{
    if (s_ready) return true;

    if (!SD_MMC.setPins(BSP_PIN_SD_CLK, BSP_PIN_SD_CMD, BSP_PIN_SD_D0)) {
        Serial.println("[sd] setPins failed");
        return false;
    }
    if (!SD_MMC.begin("/sdcard", true, false)) {
        Serial.println("[sd] begin failed (no card / non-FAT / pin/contact issue)");
        return false;
    }
    if (SD_MMC.cardType() == CARD_NONE) {
        Serial.println("[sd] mounted but card type is NONE");
        SD_MMC.end();
        return false;
    }
    s_ready = true;
    return true;
}

void bsp_sd_deinit(void)
{
    if (!s_ready) return;
    SD_MMC.end();
    s_ready = false;
}

bool bsp_sd_ready(void)
{
    return s_ready;
}

uint64_t bsp_sd_size_mb(void)
{
    return s_ready ? SD_MMC.cardSize() / 1048576ULL : 0;
}

const char* bsp_sd_type(void)
{
    if (!s_ready) return "-";
    switch (SD_MMC.cardType()) {
        case CARD_MMC:  return "MMC";
        case CARD_SD:   return "SDSC";
        case CARD_SDHC: return "SDHC";
        case CARD_NONE: return "NONE";
        default:        return "UNKNOWN";
    }
}

bool bsp_sd_rw_check(void)
{
    if (!s_ready) return false;

    static const char* kPath = "/epaper154_bsp_rw.txt";
    static const char* kText = "epaper_154 sd rw\n";
    File f = SD_MMC.open(kPath, FILE_WRITE);
    if (!f) return false;
    const size_t written = f.print(kText);
    f.close();
    if (written != strlen(kText)) {
        SD_MMC.remove(kPath);
        return false;
    }

    char buf[32] = {0};
    f = SD_MMC.open(kPath, FILE_READ);
    if (!f) return false;
    const size_t got = f.readBytes(buf, sizeof(buf) - 1);
    f.close();
    SD_MMC.remove(kPath);

    return got == strlen(kText) && strcmp(buf, kText) == 0;
}

