#pragma once

#include "file_storage.h"

enum class file_validator_kind_t : uint8_t {
    Generic = 0,
    StoryPackage,
    Audio,
    Fvid,
};

struct file_validation_report_t {
    bool ok = false;
    file_validator_kind_t kind = file_validator_kind_t::Generic;
    uint16_t files_checked = 0;
    uint16_t fvid_checked = 0;
    uint16_t audio_checked = 0;
    char detail[96] = {};
};

file_storage::Status file_validate_fvid(const char* path, file_validation_report_t* report);
file_storage::Status file_validate_audio(const char* path, file_validation_report_t* report);
file_storage::Status file_validate_story_package(const char* root,
                                                  file_validation_report_t* report);

