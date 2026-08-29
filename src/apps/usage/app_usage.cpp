#include "app_usage.h"

#include <Arduino.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "bsp.h"
#include "bsp_pins.h"
#include "usage_model.h"
#include "../../shell/shell.h"

#ifndef USAGE_PARTIAL_MAX_STREAK
#define USAGE_PARTIAL_MAX_STREAK 6
#endif

namespace {
constexpr uint32_t USAGE_STALE_MS = 7u * 3600u * 1000u;
constexpr uint32_t USAGE_CD_POLL_MS = 60000u;
constexpr int MAX_VISIBLE_PROVIDERS = 2;

enum usage_view_t { VIEW_OVERVIEW, VIEW_FOCUS };
usage_view_t s_view = VIEW_OVERVIEW;
int s_focus_slot = 0;
int s_partial_streak = 0;
uint32_t s_last_seq = 0;
uint32_t s_last_cd_poll = 0;

struct draw_cache_t {
    bool valid;
    char provider[12];
    char acct[8];
    char limit_state[16];
    char cc_state[16];
    uint8_t session_pct;
    uint8_t week_pct;
    bool ok;
    bool stale;
    bool session_due;
    char session_bucket[8];
    char week_bucket[8];
};
draw_cache_t s_cache[USAGE_PROVIDER_MAX] = {};

uint32_t age_ms(const usage_snapshot_t* snapshot, uint32_t now)
{
    return snapshot ? now - snapshot->rx_millis : UINT32_MAX;
}

uint16_t remaining_minutes(const usage_snapshot_t* snapshot, bool session, uint32_t now)
{
    if (!snapshot) return 0;
    const uint16_t reset = session ? snapshot->session_reset_min : snapshot->week_reset_min;
    if (reset == 0) return 0;
    const uint32_t age = age_ms(snapshot, now);
    const uint32_t elapsed = age == UINT32_MAX ? UINT32_MAX : age / 60000u;
    return elapsed >= reset ? 0 : (uint16_t)(reset - elapsed);
}

void bucket_label(uint16_t minutes, bool session, char* out, size_t out_len)
{
    if (minutes == 0) snprintf(out, out_len, "due");
    else if (session && minutes > 180) snprintf(out, out_len, ">3h");
    else if (session && minutes > 120) snprintf(out, out_len, "~2h");
    else if (session && minutes > 60) snprintf(out, out_len, "~1h");
    else if (session && minutes > 30) snprintf(out, out_len, "<30m");
    else if (session) snprintf(out, out_len, "<10m");
    else if (minutes > 4320) snprintf(out, out_len, ">3d");
    else if (minutes > 2880) snprintf(out, out_len, "~2d");
    else if (minutes > 1440) snprintf(out, out_len, "~1d");
    else if (minutes > 720) snprintf(out, out_len, "<12h");
    else snprintf(out, out_len, "<3h");
}

void uppercase_provider(const char* provider, char* out, size_t out_len)
{
    if (!provider) provider = "?";
    size_t i = 0;
    for (; provider[i] && i + 1 < out_len; i++) out[i] = (char)toupper((unsigned char)provider[i]);
    out[i] = '\0';
}

bool is_stale(const usage_snapshot_t* snapshot, uint32_t now)
{
    return !snapshot || age_ms(snapshot, now) > USAGE_STALE_MS;
}

void capture_cache(uint32_t now)
{
    for (int slot = 0; slot < USAGE_PROVIDER_MAX; slot++) {
        draw_cache_t& cache = s_cache[slot];
        const usage_snapshot_t* snapshot = usage_model_at(slot);
        memset(&cache, 0, sizeof(cache));
        if (!snapshot) continue;
        cache.valid = true;
        snprintf(cache.provider, sizeof(cache.provider), "%s", snapshot->provider);
        snprintf(cache.acct, sizeof(cache.acct), "%s", snapshot->acct);
        snprintf(cache.limit_state, sizeof(cache.limit_state), "%s", snapshot->limit_state);
        snprintf(cache.cc_state, sizeof(cache.cc_state), "%s", snapshot->cc_state);
        cache.session_pct = snapshot->session_pct;
        cache.week_pct = snapshot->week_pct;
        cache.ok = snapshot->ok;
        cache.stale = is_stale(snapshot, now);
        cache.session_due = remaining_minutes(snapshot, true, now) == 0;
        bucket_label(remaining_minutes(snapshot, true, now), true,
                     cache.session_bucket, sizeof(cache.session_bucket));
        bucket_label(remaining_minutes(snapshot, false, now), false,
                     cache.week_bucket, sizeof(cache.week_bucket));
    }
}

bool cache_differs(uint32_t now)
{
    for (int slot = 0; slot < USAGE_PROVIDER_MAX; slot++) {
        const usage_snapshot_t* snapshot = usage_model_at(slot);
        const draw_cache_t& cache = s_cache[slot];
        if (!snapshot) {
            if (cache.valid) return true;
            continue;
        }
        char session_bucket[8];
        char week_bucket[8];
        bucket_label(remaining_minutes(snapshot, true, now), true, session_bucket, sizeof(session_bucket));
        bucket_label(remaining_minutes(snapshot, false, now), false, week_bucket, sizeof(week_bucket));
        if (!cache.valid || strcmp(cache.provider, snapshot->provider) != 0 ||
            strcmp(cache.acct, snapshot->acct) != 0 ||
            strcmp(cache.limit_state, snapshot->limit_state) != 0 ||
            strcmp(cache.cc_state, snapshot->cc_state) != 0 ||
            cache.session_pct != snapshot->session_pct || cache.week_pct != snapshot->week_pct ||
            cache.ok != snapshot->ok || cache.stale != is_stale(snapshot, now) ||
            cache.session_due != (remaining_minutes(snapshot, true, now) == 0) ||
            strcmp(cache.session_bucket, session_bucket) != 0 ||
            strcmp(cache.week_bucket, week_bucket) != 0) return true;
    }
    return false;
}

void draw_bar(int y, const char* label, uint8_t pct, const char* countdown, bool due)
{
    constexpr int x = 22;
    constexpr int width = 104;
    constexpr int height = 12;
    bsp_ui_fb_draw_text(2, y + 2, label, 1);
    bsp_ui_fb_fill_rect(x, y, width, 1, true);
    bsp_ui_fb_fill_rect(x, y + height - 1, width, 1, true);
    bsp_ui_fb_fill_rect(x, y, 1, height, true);
    bsp_ui_fb_fill_rect(x + width - 1, y, 1, height, true);
    if (due) {
        for (int dx = 2; dx < width - 2; dx++) {
            for (int dy = 2; dy < height - 2; dy++) {
                if ((dx + dy) % 7 < 2) bsp_ui_fb_fill_rect(x + dx, y + dy, 1, 1, true);
            }
        }
        bsp_ui_fb_draw_text(130, y + 2, "~", 1);
    } else {
        const int fill = (width - 4) * pct / 100;
        if (fill > 0) bsp_ui_fb_fill_rect(x + 2, y + 2, fill, height - 4, true);
        char percent[5];
        snprintf(percent, sizeof(percent), "%3u%%", pct);
        bsp_ui_fb_draw_text(130, y + 2, percent, 1);
    }
    if (countdown) bsp_ui_fb_draw_text(174, y + 2, countdown, 1);
}

void draw_provider_section(const usage_snapshot_t* snapshot, int slot, uint32_t now, bool focused)
{
    const int top = focused ? 30 : 28 + slot * 48;
    char provider[12];
    uppercase_provider(snapshot ? snapshot->provider : "?", provider, sizeof(provider));
    bsp_ui_fb_draw_text(5, top, provider, 1);
    if (snapshot && snapshot->acct[0]) {
        const int acct_x = BSP_EPD_W - (int)strlen(snapshot->acct) * 6 - 2;
        bsp_ui_fb_draw_text(acct_x, top, snapshot->acct, 1);
    }
    if (!snapshot) {
        bsp_ui_fb_draw_text(5, top + 18, "NO HOST", 1);
        return;
    }

    char session_bucket[8];
    char week_bucket[8];
    const uint16_t session_left = remaining_minutes(snapshot, true, now);
    const uint16_t week_left = remaining_minutes(snapshot, false, now);
    bucket_label(session_left, true, session_bucket, sizeof(session_bucket));
    bucket_label(week_left, false, week_bucket, sizeof(week_bucket));
    draw_bar(top + 12, "5H", snapshot->session_pct, session_bucket, session_left == 0);
    draw_bar(top + 28, "7D", snapshot->week_pct, week_bucket, week_left == 0);

    if (is_stale(snapshot, now)) {
        bsp_ui_fb_draw_text(5, top + 42, "NO HOST", 1);
        bsp_ui_fb_fill_rect(0, top - 2, BSP_EPD_W, 1, true);
        bsp_ui_fb_fill_rect(0, top + 45, BSP_EPD_W, 1, true);
        bsp_ui_fb_fill_rect(0, top - 2, 1, 48, true);
        bsp_ui_fb_fill_rect(BSP_EPD_W - 1, top - 2, 1, 48, true);
    }
}

int status_severity(const usage_snapshot_t* snapshot)
{
    if (!snapshot || !snapshot->ok) return 4;
    if (strcmp(snapshot->limit_state, "limited") == 0) return 3;
    if (strcmp(snapshot->limit_state, "rejected") == 0) return 2;
    if (strcmp(snapshot->limit_state, "allowed_warning") == 0) return 1;
    return 0;
}

void draw_banner(uint32_t now)
{
    const usage_snapshot_t* worst = nullptr;
    int worst_score = -1;
    for (int slot = 0; slot < MAX_VISIBLE_PROVIDERS; slot++) {
        const usage_snapshot_t* snapshot = usage_model_at(slot);
        if (snapshot && (!worst || status_severity(snapshot) > worst_score)) {
            worst = snapshot;
            worst_score = status_severity(snapshot);
        }
    }
    char line[32];
    if (!worst) snprintf(line, sizeof(line), "NO HOST");
    else {
        char provider[12];
        char state[16];
        uppercase_provider(worst->provider, provider, sizeof(provider));
        if (!worst->ok) snprintf(state, sizeof(state), "HOST ?");
        else if (strcmp(worst->limit_state, "allowed") == 0) snprintf(state, sizeof(state), "OK");
        else if (strcmp(worst->limit_state, "allowed_warning") == 0) snprintf(state, sizeof(state), "WARNING");
        else if (strcmp(worst->limit_state, "rejected") == 0) snprintf(state, sizeof(state), "REJECTED");
        else if (strcmp(worst->limit_state, "limited") == 0) snprintf(state, sizeof(state), "LIMITED");
        else snprintf(state, sizeof(state), "%.12s", worst->limit_state);
        snprintf(line, sizeof(line), "%s %s", provider, state);
    }
    bsp_ui_fb_draw_text(5, 126, line, 1);
    if (worst_score >= 2) {
        bsp_ui_fb_fill_rect(0, 123, BSP_EPD_W, 1, true);
        bsp_ui_fb_fill_rect(0, 138, BSP_EPD_W, 1, true);
        bsp_ui_fb_fill_rect(0, 123, 1, 16, true);
        bsp_ui_fb_fill_rect(BSP_EPD_W - 1, 123, 1, 16, true);
    }
    (void)now;
}

void draw_footer(uint32_t now)
{
    char line[40] = "upd";
    for (int slot = 0; slot < MAX_VISIBLE_PROVIDERS; slot++) {
        const usage_snapshot_t* snapshot = usage_model_at(slot);
        if (!snapshot) continue;
        const uint32_t age = age_ms(snapshot, now);
        char age_text[12];
        if (age >= USAGE_STALE_MS) snprintf(age_text, sizeof(age_text), "NO HOST");
        else if (age >= 3600000u) snprintf(age_text, sizeof(age_text), "%luh", (unsigned long)(age / 3600000u));
        else if (age >= 60000u) snprintf(age_text, sizeof(age_text), "%lum", (unsigned long)(age / 60000u));
        else snprintf(age_text, sizeof(age_text), "%lus", (unsigned long)(age / 1000u));
        char provider[12];
        uppercase_provider(snapshot->provider, provider, sizeof(provider));
        char part[24];
        snprintf(part, sizeof(part), " %s %s", provider, age_text);
        strncat(line, part, sizeof(line) - strlen(line) - 1);
    }
    bsp_ui_fb_draw_text(5, 176, line, 1);
}

void render_overview(uint32_t now)
{
    bsp_ui_fb_fill_rect(0, 26, BSP_EPD_W, BSP_EPD_H - 26, false);
    const int count = usage_model_count();
    if (count == 0) {
        bsp_ui_fb_draw_text(5, 30, "USAGE", 1);
        bsp_ui_fb_draw_text(5, 48, "NO HOST", 1);
    } else {
        for (int slot = 0; slot < MAX_VISIBLE_PROVIDERS; slot++) {
            draw_provider_section(slot < count ? usage_model_at(slot) : nullptr, slot, now, false);
        }
    }
    bsp_ui_fb_fill_rect(0, 72, BSP_EPD_W, 1, true);
    bsp_ui_fb_fill_rect(0, 120, BSP_EPD_W, 2, true);
    draw_banner(now);
    const usage_snapshot_t* cc = nullptr;
    for (int slot = 0; slot < MAX_VISIBLE_PROVIDERS; slot++) {
        const usage_snapshot_t* snapshot = usage_model_at(slot);
        if (snapshot && snapshot->cc_state[0]) { cc = snapshot; break; }
    }
    if (cc) {
        char state[24];
        snprintf(state, sizeof(state), "cc: %s", cc->cc_state);
        bsp_ui_fb_draw_text(5, 142, state, 1);
        bsp_ui_fb_draw_text(5, 156, cc->cc_msg, 1);
    }
    draw_footer(now);
}

void render_focus(uint32_t now)
{
    bsp_ui_fb_fill_rect(0, 26, BSP_EPD_W, BSP_EPD_H - 26, false);
    const usage_snapshot_t* snapshot = usage_model_at(s_focus_slot);
    draw_provider_section(snapshot, 0, now, true);
    if (snapshot) {
        char line[32];
        snprintf(line, sizeof(line), "state %s %s", snapshot->limit_state, snapshot->ok ? "OK" : "HOST ?");
        bsp_ui_fb_draw_text(5, 132, line, 1);
        if (snapshot->cc_state[0]) {
            snprintf(line, sizeof(line), "cc: %s", snapshot->cc_state);
            bsp_ui_fb_draw_text(5, 150, line, 1);
            bsp_ui_fb_draw_text(5, 164, snapshot->cc_msg, 1);
        }
    }
    bsp_ui_fb_draw_text(5, 184, "LEFT/RIGHT focus  ESC back", 1);
}

void render(uint32_t now)
{
    if (s_view == VIEW_FOCUS) render_focus(now);
    else render_overview(now);
}

void flush_render(bool force_full)
{
    const uint32_t now = millis();
    render(now);
    bool ok;
    if (force_full) {
        ok = shell_full_refresh_current();
        if (ok) s_partial_streak = 0;
    } else {
        ok = bsp_ui_flush_partial();
        if (ok) s_partial_streak++;
    }
    capture_cache(now);
    s_last_seq = usage_model_seq();
    s_last_cd_poll = now;
    Serial.printf("[usage] mode=%s streak=%d busy=%lums %s\n",
                  force_full ? "full" : "partial", s_partial_streak,
                  (unsigned long)bsp_epd_last_busy_ms(), ok ? "ok" : "FAIL");
}

void maybe_redraw(bool force)
{
    const uint32_t now = millis();
    const bool seq_changed = usage_model_seq() != s_last_seq;
    const bool poll_due = now - s_last_cd_poll >= USAGE_CD_POLL_MS;
    const bool changed = cache_differs(now);
    if (!force && !(changed && (seq_changed || poll_due))) {
        if (seq_changed || poll_due) {
            capture_cache(now);
            s_last_seq = usage_model_seq();
            if (poll_due) s_last_cd_poll = now;
        }
        return;
    }
    const bool full = force || s_partial_streak >= USAGE_PARTIAL_MAX_STREAK;
    flush_render(full);
}

void handle_event(const shell_event_t& event)
{
    if (event.kind == SHELL_EV_HOME) { shell_show_launcher(); return; }
    if (event.kind == SHELL_EV_BACK) {
        if (s_view == VIEW_FOCUS) { s_view = VIEW_OVERVIEW; maybe_redraw(false); }
        else shell_show_launcher();
        return;
    }
    if (event.kind == SHELL_EV_TAP) {
        if (event.long_press) { maybe_redraw(true); return; }
        if (s_view == VIEW_FOCUS) s_view = VIEW_OVERVIEW;
        else {
            const int slot = event.y < BSP_EPD_H / 2 ? 0 : 1;
            if (slot < usage_model_count()) { s_view = VIEW_FOCUS; s_focus_slot = slot; }
        }
        maybe_redraw(false);
    } else if (event.kind == SHELL_EV_KEY) app_usage_on_key(event.key, event.event);
}
}

void app_usage_on_enter(void)
{
    s_view = VIEW_OVERVIEW;
    s_focus_slot = 0;
    s_partial_streak = 0;
    memset(s_cache, 0, sizeof(s_cache));
    flush_render(false);
}

void app_usage_on_exit(void) {}

void app_usage_tick(void)
{
    const app_entry_t* active = shell_active_app();
    handle_event(shell_wait_event(30000));
    if (shell_active_app() == active) maybe_redraw(false);
}

void app_usage_on_key(const char* key, const char* event)
{
    if (!key || !event || strcmp(event, "press") != 0) return;
    if (strcmp(key, "enter") == 0) { maybe_redraw(true); return; }
    if (strcmp(key, "back") == 0 || strcmp(key, "esc") == 0 || strcmp(key, "home") == 0) {
        if (s_view == VIEW_FOCUS) { s_view = VIEW_OVERVIEW; maybe_redraw(false); }
        else shell_show_launcher();
        return;
    }
    if (strcmp(key, "left") == 0 || strcmp(key, "right") == 0) {
        const int count = min(MAX_VISIBLE_PROVIDERS, usage_model_count());
        if (s_view == VIEW_OVERVIEW) {
            if (count > 0) { s_view = VIEW_FOCUS; s_focus_slot = strcmp(key, "right") == 0 ? 0 : count - 1; }
        } else if (strcmp(key, "right") == 0) {
            if (++s_focus_slot >= count) s_view = VIEW_OVERVIEW;
        } else if (--s_focus_slot < 0) {
            s_view = VIEW_OVERVIEW;
        }
        maybe_redraw(false);
    }
}
