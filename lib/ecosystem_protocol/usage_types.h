#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Shared POD for the usage.push command and the usage app. */
typedef struct usage_snapshot {
    char     provider[12];
    uint8_t  session_pct;
    uint16_t session_reset_min;
    uint8_t  week_pct;
    uint16_t week_reset_min;
    char     limit_state[16];
    char     acct[8];
    bool     ok;
    char     cc_state[16];
    char     cc_msg[64];
    uint32_t rx_millis;
    uint32_t seq;
} usage_snapshot_t;
