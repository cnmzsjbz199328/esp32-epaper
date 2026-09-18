#include "app_registry.h"

#include "../apps/family_video/app_family_video.h"
#include "../apps/file_sync/app_file_sync.h"
#include "../apps/settings/app_settings.h"
#include "../apps/usage/app_usage.h"
#include "icons.h"

static const app_entry_t s_entries[] = {
    { "family_video", "PHOTOS", SHELL_ICON_FRAMES, true,
      app_family_video_on_enter, app_family_video_on_exit,
      app_family_video_tick, app_family_video_on_key },
    { "file_sync", "FILE SYNC", SHELL_ICON_FILE_SYNC, false,
      app_file_sync_on_enter, app_file_sync_on_exit,
      app_file_sync_tick, app_file_sync_on_key },
    { "settings", "SETTINGS", SHELL_ICON_SETTINGS, false,
      app_settings_on_enter, app_settings_on_exit,
      app_settings_tick, app_settings_on_key },
    { "usage", "USAGE", SHELL_ICON_USAGE, false,
      app_usage_on_enter, app_usage_on_exit,
      app_usage_tick, app_usage_on_key },
};

const app_entry_t* app_registry_entries(void) { return s_entries; }
int app_registry_count(void) { return (int)(sizeof(s_entries) / sizeof(s_entries[0])); }
