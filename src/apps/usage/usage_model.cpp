#include "usage_model.h"

#include <Arduino.h>
#include <string.h>

namespace {
usage_snapshot_t s_snapshots[USAGE_PROVIDER_MAX] = {};
bool s_used[USAGE_PROVIDER_MAX] = {};
uint32_t s_seq = 0;

void copy_field(char* dst, size_t dst_len, const char* src)
{
    if (!dst || dst_len == 0) return;
    snprintf(dst, dst_len, "%s", src ? src : "");
}

int find_provider(const char* provider)
{
    if (!provider || provider[0] == '\0') return -1;
    for (int i = 0; i < USAGE_PROVIDER_MAX; i++) {
        if (s_used[i] && strcmp(s_snapshots[i].provider, provider) == 0) return i;
    }
    return -1;
}
}

extern "C" void usage_model_apply(const usage_snapshot_t* in)
{
    if (!in || in->provider[0] == '\0') {
        Serial.println("[usage] drop empty provider");
        return;
    }

    int slot = find_provider(in->provider);
    if (slot < 0) {
        for (int i = 0; i < USAGE_PROVIDER_MAX; i++) {
            if (!s_used[i]) {
                slot = i;
                s_used[i] = true;
                break;
            }
        }
    }
    if (slot < 0) {
        Serial.printf("[usage] WARN provider table full: %s\n", in->provider);
        return;
    }

    usage_snapshot_t& out = s_snapshots[slot];
    memset(&out, 0, sizeof(out));
    copy_field(out.provider, sizeof(out.provider), in->provider);
    out.session_pct = in->session_pct > 100 ? 100 : in->session_pct;
    out.week_pct = in->week_pct > 100 ? 100 : in->week_pct;
    out.session_reset_min = in->session_reset_min;
    out.week_reset_min = in->week_reset_min;
    copy_field(out.limit_state, sizeof(out.limit_state), in->limit_state);
    if (out.limit_state[0] == '\0') copy_field(out.limit_state, sizeof(out.limit_state), "allowed");
    copy_field(out.acct, sizeof(out.acct), in->acct);
    out.ok = in->ok;
    copy_field(out.cc_state, sizeof(out.cc_state), in->cc_state);
    copy_field(out.cc_msg, sizeof(out.cc_msg), in->cc_msg);
    out.rx_millis = in->rx_millis ? in->rx_millis : millis();
    out.seq = ++s_seq;
    if (out.seq == 0) out.seq = ++s_seq;
}

extern "C" const usage_snapshot_t* usage_model_get(const char* provider)
{
    const int slot = find_provider(provider);
    return slot < 0 ? nullptr : &s_snapshots[slot];
}

extern "C" int usage_model_count(void)
{
    int count = 0;
    while (count < USAGE_PROVIDER_MAX && s_used[count]) count++;
    return count;
}

extern "C" const usage_snapshot_t* usage_model_at(int slot)
{
    if (slot < 0 || slot >= USAGE_PROVIDER_MAX || !s_used[slot]) return nullptr;
    return &s_snapshots[slot];
}

extern "C" uint32_t usage_model_age_ms(const char* provider)
{
    const usage_snapshot_t* snapshot = usage_model_get(provider);
    return snapshot ? millis() - snapshot->rx_millis : UINT32_MAX;
}

extern "C" uint32_t usage_model_seq(void)
{
    return s_seq;
}
