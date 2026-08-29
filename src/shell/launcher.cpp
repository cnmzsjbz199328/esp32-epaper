#include "launcher.h"

#include <Arduino.h>
#include <string.h>

#include "app_registry.h"
#include "bsp.h"
#include "bsp_pins.h"
#include "shell.h"
#include "status_bar.h"

namespace {
int s_selected = 0;
constexpr int GRID_TOP = 34;
constexpr int CELL_W = 88;
constexpr int CELL_H = 82;

void draw_selection(int slot, bool black)
{
    const int col = slot % 2;
    const int row = slot / 2;
    const app_entry_t* entries = app_registry_entries();
    const char* title = slot < app_registry_count() ? entries[slot].title : "";
    const int title_width = (int)strlen(title) * 6;
    const int x = col * CELL_W + (CELL_W - title_width) / 2;
    const int y = GRID_TOP + row * CELL_H + 83;
    bsp_ui_fb_fill_rect(x, y, title_width, 1, black);
}

void draw_slot(int slot, const app_entry_t* app)
{
    const int col = slot % 2;
    const int row = slot / 2;
    const int cell_x = col * CELL_W;
    const int cell_y = GRID_TOP + row * CELL_H;
    const int icon_x = cell_x + (CELL_W - SHELL_ICON_W) / 2;
    const int icon_y = cell_y;
    if (app) {
        bsp_ui_draw_bitmap(icon_x, icon_y, SHELL_ICON_W, SHELL_ICON_H,
                           app->icon, nullptr, false);
        const int title_width = (int)strlen(app->title) * 6;
        const int title_x = cell_x + (CELL_W - title_width) / 2;
        bsp_ui_fb_draw_text(title_x, cell_y + 75, app->title, 1);
    } else {
        bsp_ui_fb_fill_rect(cell_x + 32, cell_y + 30, 24, 2, false);
        bsp_ui_fb_fill_rect(cell_x + 43, cell_y + 19, 2, 24, false);
    }
}

void move_selection(int delta)
{
    const int old = s_selected;
    s_selected = (s_selected + delta + 4) % 4;
    if (old == s_selected) return;
    draw_selection(old, false);
    draw_selection(s_selected, true);
    if (!bsp_ui_flush_partial()) Serial.println("[launcher] selection partial refresh failed");
}
}

void launcher_render_into_fb(void)
{
    bsp_ui_fb_fill_rect(0, 26, BSP_EPD_W, BSP_EPD_H - 26, false);
    const app_entry_t* entries = app_registry_entries();
    const int count = app_registry_count();
    for (int i = 0; i < 4; i++) draw_slot(i, i < count ? &entries[i] : nullptr);
    draw_selection(s_selected, true);
}

void launcher_handle_tap(uint16_t x, uint16_t y, bool long_press)
{
    (void)long_press;
    if (y < GRID_TOP || y >= GRID_TOP + 2 * CELL_H || x >= 2 * CELL_W) return;
    const int slot = (x / CELL_W) + 2 * ((y - GRID_TOP) / CELL_H);
    if (slot < 0 || slot >= app_registry_count()) return;
    s_selected = slot;
    shell_open_app(&app_registry_entries()[slot]);
}

void launcher_handle_key(const char* key, const char* event)
{
    if (!key || !event || (strcmp(event, "press") != 0 && strcmp(event, "down") != 0 && strcmp(event, "repeat") != 0)) return;
    if (strcmp(key, "left") == 0) move_selection(-1);
    else if (strcmp(key, "right") == 0) move_selection(1);
    else if (strcmp(key, "up") == 0) move_selection(-2);
    else if (strcmp(key, "down") == 0) move_selection(2);
    else if (strcmp(key, "enter") == 0 && s_selected < app_registry_count()) shell_open_app(&app_registry_entries()[s_selected]);
}

void launcher_tick(void)
{
    shell_event_t event = shell_wait_event(1000);
    if (event.kind == SHELL_EV_TAP) launcher_handle_tap(event.x, event.y, event.long_press);
    else if (event.kind == SHELL_EV_KEY) launcher_handle_key(event.key, event.event);
    else if (event.kind == SHELL_EV_TIMEOUT && status_bar_refresh_if_due(millis())) {
        if (!bsp_ui_flush_partial()) Serial.println("[launcher] status partial refresh failed");
    }
}
