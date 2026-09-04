#include "app_family_video.h"

#include <Arduino.h>
#include <string.h>

#include "frame_source.h"
#include "story_audio.h"
#if defined(APP_STORY_DEMO_ROM)
#include "story_demo_audio.h"
#endif
#include "family_video_assets.h"
#include "bsp.h"
#include "bsp_pins.h"
#include "../../shell/shell.h"

#ifndef APP_PARTIAL_MAX_STREAK
#define APP_PARTIAL_MAX_STREAK 4
#endif

namespace {

enum view_t { VIEW_LIBRARY, VIEW_PLAYER };
constexpr int LIBRARY_VISIBLE = 6;
constexpr int LIBRARY_ROW_Y = 34;
constexpr int LIBRARY_ROW_H = 24;
constexpr int LIBRARY_FOOTER_Y = 184;

view_t s_view = VIEW_PLAYER;
fvid_entry_t s_stories[SD_STORY_MAX] = {};
int s_story_count = 0;
int s_selected = 0;
int s_frame = 0;
int s_streak = 0;
bool s_from_library = false;
const frame_source_t* s_source = nullptr;

int library_page_count()
{
    return (s_story_count + LIBRARY_VISIBLE - 1) / LIBRARY_VISIBLE;
}

void draw_library_scrollbar()
{
    const int pages = library_page_count();
    if (pages <= 1) return;
    const int track_y = 31;
    const int track_h = 132;
    const int thumb_h = max(12, track_h / pages);
    const int thumb_y = track_y + (track_h - thumb_h) * (s_selected / LIBRARY_VISIBLE) / (pages - 1);
    bsp_ui_fb_fill_rect(193, track_y, 5, track_h, true);
    bsp_ui_fb_fill_rect(194, thumb_y + 1, 3, thumb_h - 2, false);
}

void draw_selection_box(int y)
{
    bsp_ui_fb_fill_rect(2, y - 10, BSP_EPD_W - 10, 1, true);
    bsp_ui_fb_fill_rect(2, y + 8, BSP_EPD_W - 10, 1, true);
    bsp_ui_fb_fill_rect(2, y - 10, 1, 19, true);
    bsp_ui_fb_fill_rect(BSP_EPD_W - 11, y - 10, 1, 19, true);
}

void render_library()
{
    bsp_ui_fb_clear(0xFF);
    bsp_ui_fb_draw_text(5, 8, "PHOTOS", 1);
    bsp_ui_fb_draw_text(132, 8, "STORIES", 1);

    const int page_start = (s_selected / LIBRARY_VISIBLE) * LIBRARY_VISIBLE;
    for (int row = 0; row < LIBRARY_VISIBLE; row++) {
        const int index = page_start + row;
        if (index >= s_story_count) break;
        const fvid_entry_t& story = s_stories[index];
        char line[34];
        snprintf(line, sizeof(line), "%c %-22s %3uF", index == s_selected ? '>' : ' ',
                 story.name, story.frames);
        const int y = LIBRARY_ROW_Y + row * LIBRARY_ROW_H;
        if (index == s_selected) draw_selection_box(y);
        bsp_ui_fb_draw_text(6, y, line, 1);
    }
    draw_library_scrollbar();
    bsp_ui_fb_draw_text(5, LIBRARY_FOOTER_Y, "ENTER play  UP/DN move", 1);
}

void sync_scene_audio(bool display_ok)
{
    if (!display_ok) {
        story_audio_stop();
        return;
    }
    if (s_from_library && s_source == frame_source_sd()) {
        story_audio_play_scene(s_stories[s_selected].path, s_frame);
        return;
    }
#if defined(APP_STORY_DEMO_ROM)
    if (s_source == frame_source_rom() && s_frame >= 0 &&
        s_frame < STORY_DEMO_AUDIO_SCENE_COUNT) {
        Serial.printf("[video] ROM scene=%d audio samples=%u\n", s_frame,
                      (unsigned)STORY_DEMO_AUDIO_SAMPLE_COUNTS[s_frame]);
        story_audio_play_rom_adpcm(STORY_DEMO_AUDIO_DATA[s_frame],
                                   STORY_DEMO_AUDIO_DATA_LENGTHS[s_frame],
                                   STORY_DEMO_AUDIO_SAMPLE_COUNTS[s_frame],
                                   STORY_DEMO_AUDIO_SAMPLE_RATE);
        return;
    }
#endif
    story_audio_stop();
}

bool show_library(bool full_refresh)
{
    story_audio_stop();
    if (s_story_count <= 0) {
        Serial.println("[video] library empty: NO STORIES ON SD");
        return false;
    }
    s_view = VIEW_LIBRARY;
    render_library();
    const bool ok = full_refresh ? shell_full_refresh_current() : bsp_ui_flush_partial();
    if (!ok) Serial.println("[video] library refresh FAIL");
    return ok;
}

bool show_frame(int target, bool force_full)
{
    if (!s_source) s_source = frame_source_init();
    int count = s_source->count();
    if (count <= 0) return false;
    if (target < 0) target = 0;
    if (target >= count) target = count - 1;
    if (target == s_frame && !force_full) return true;

    uint8_t* fb = bsp_ui_fb();
    if (!s_source->load(target, fb, bsp_ui_fb_len())) {
        Serial.printf("[video] %s load frame %d failed\n", s_source->name, target);
        if (s_source != frame_source_rom()) {
            frame_source_use_rom();
            s_source = frame_source_current();
            count = s_source->count();
            if (target >= count) target = count - 1;
            if (!s_source->load(target, fb, bsp_ui_fb_len())) {
                Serial.println("[video] ROM fallback load failed");
                if (s_from_library) show_library(true);
                else shell_show_launcher();
                return false;
            }
            Serial.println("[video] SD playback fell back to ROM");
        } else {
            if (s_from_library) show_library(true);
            else shell_show_launcher();
            return false;
        }
    }

    const bool backwards = target < s_frame;
    const bool final_frame = target == count - 1;
    const int hint = s_source->hint(target);
    const bool needs_new_base = !force_full && (final_frame || backwards ||
                          hint != APP_REFRESH_PARTIAL || s_streak >= APP_PARTIAL_MAX_STREAK ||
                          !bsp_ui_partial_active());
    s_frame = target;

    if (needs_new_base || force_full) {
        const bool ok = shell_full_refresh_current();
        if (ok) s_streak = 0;
        sync_scene_audio(ok);
        Serial.printf("[video] frame=%d/%d source=%s mode=full hint=%d busy=%lums %s\n",
                      s_frame + 1, count, s_source->name, hint,
                      (unsigned long)bsp_epd_last_busy_ms(), ok ? "ok" : "FAIL");
        return ok;
    }

    const bool ok = bsp_ui_flush_partial();
    if (ok) s_streak++;
    sync_scene_audio(ok);
    Serial.printf("[video] frame=%d/%d source=%s mode=partial streak=%d busy=%lums %s\n",
                  s_frame + 1, count, s_source->name, s_streak,
                  (unsigned long)bsp_epd_last_busy_ms(), ok ? "ok" : "FAIL");
    return ok;
}

void select_story(int index)
{
    if (index < 0) index = 0;
    if (index >= s_story_count) index = s_story_count - 1;
    s_selected = index;
    if (!frame_source_sd_open(s_stories[index].path)) {
        Serial.printf("[video] story open failed: %s\n", s_stories[index].path);
        show_library(false);
        return;
    }
    s_source = frame_source_sd();
    s_frame = 0;
    s_streak = 0;
    s_from_library = true;
    s_view = VIEW_PLAYER;
    if (!show_frame(0, true)) Serial.println("[video] story initial frame FAIL");
}

void move_selection(int delta)
{
    if (s_story_count <= 0) return;
    int next = s_selected + delta;
    if (next < 0) next = 0;
    if (next >= s_story_count) next = s_story_count - 1;
    if (next == s_selected) return;
    s_selected = next;
    render_library();
    if (!bsp_ui_flush_partial()) Serial.println("[video] library partial refresh FAIL");
}

void page_selection(int delta)
{
    move_selection(delta * LIBRARY_VISIBLE);
}

void handle_event(const shell_event_t& event)
{
    if (event.kind == SHELL_EV_HOME) {
        shell_show_launcher();
        return;
    }
    if (event.kind == SHELL_EV_BACK) {
        if (s_view == VIEW_PLAYER && s_from_library && s_story_count > 0) {
            show_library(true);
        } else {
            shell_show_launcher();
        }
        return;
    }
    if (event.kind == SHELL_EV_KEY) {
        app_family_video_on_key(event.key, event.event);
        return;
    }
    if (event.kind != SHELL_EV_TAP) return;

    if (s_view == VIEW_LIBRARY) {
        if (event.long_press) return;
        if (event.y < 28) page_selection(-1);
        else if (event.y >= 172) page_selection(1);
        else if (event.y >= LIBRARY_ROW_Y - 14 && event.y < LIBRARY_ROW_Y - 14 + LIBRARY_VISIBLE * LIBRARY_ROW_H) {
            const int row = (event.y - (LIBRARY_ROW_Y - 14)) / LIBRARY_ROW_H;
            const int index = (s_selected / LIBRARY_VISIBLE) * LIBRARY_VISIBLE + row;
            if (index >= 0 && index < s_story_count) select_story(index);
        }
        return;
    }

    if (!s_source) return;
    const bool left = event.x < BSP_EPD_W / 2;
    if (event.long_press && event.x >= BSP_EPD_W / 4 && event.x < BSP_EPD_W * 3 / 4) {
        story_audio_toggle_pause();
    } else if (event.long_press) {
        show_frame(left ? 0 : s_source->count() - 1, false);
    }
    else show_frame(s_frame + (left ? -1 : 1), false);
}

}

void app_family_video_on_enter(void)
{
    s_story_count = frame_source_sd_scan(s_stories, SD_STORY_MAX);
    s_selected = 0;
    s_frame = 0;
    s_streak = 0;
    if (s_story_count > 0) {
        s_from_library = true;
        s_source = nullptr;
        show_library(true);
        Serial.printf("[video] enter view=library stories=%d\n", s_story_count);
        return;
    }

    frame_source_use_rom();
    s_source = frame_source_current();
    s_from_library = false;
    s_view = VIEW_PLAYER;
    Serial.printf("[video] enter view=player source=%s frames=%d\n", s_source->name, s_source->count());
    if (!show_frame(0, true)) Serial.println("[video] initial frame FAIL");
}

void app_family_video_on_exit(void)
{
    story_audio_stop();
}

void app_family_video_tick(void)
{
    handle_event(shell_wait_event(60000));
}

void app_family_video_on_key(const char* key, const char* event)
{
    if (!key || !event || (strcmp(event, "press") != 0 && strcmp(event, "down") != 0)) return;

    if (s_view == VIEW_LIBRARY) {
        if (strcmp(key, "up") == 0) move_selection(-1);
        else if (strcmp(key, "down") == 0) move_selection(1);
        else if (strcmp(key, "pageup") == 0 || strcmp(key, "pgup") == 0) page_selection(-1);
        else if (strcmp(key, "pagedown") == 0 || strcmp(key, "pgdn") == 0) page_selection(1);
        else if (strcmp(key, "enter") == 0 && s_story_count > 0) select_story(s_selected);
        else if (strcmp(key, "back") == 0 || strcmp(key, "esc") == 0) shell_show_launcher();
        return;
    }

    if (!s_source) s_source = frame_source_init();
    if (strcmp(key, "right") == 0) show_frame(s_frame + 1, false);
    else if (strcmp(key, "left") == 0) show_frame(s_frame - 1, false);
    else if (strcmp(key, "home") == 0) shell_show_launcher();
    else if (strcmp(key, "space") == 0 || strcmp(key, "play") == 0 ||
             strcmp(key, "pause") == 0) story_audio_toggle_pause();
    else if ((strcmp(key, "back") == 0 || strcmp(key, "esc") == 0) &&
             s_from_library && s_story_count > 0) show_library(true);
    else if (strcmp(key, "back") == 0 || strcmp(key, "esc") == 0) shell_show_launcher();
}
