#pragma once

#include <stdint.h>

#include "usage_types.h"

#define USAGE_PROVIDER_MAX 3

#ifdef __cplusplus
extern "C" {
#endif

void usage_model_apply(const usage_snapshot_t* in);
const usage_snapshot_t* usage_model_get(const char* provider);
int usage_model_count(void);
const usage_snapshot_t* usage_model_at(int slot);
uint32_t usage_model_age_ms(const char* provider);
uint32_t usage_model_seq(void);

#ifdef __cplusplus
}
#endif
