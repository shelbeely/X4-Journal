#pragma once
#include "esp_err.h"
#include <stddef.h>

#define MAX_PROMPTS_PER_PACK 64
#define MAX_PACKS            16

typedef struct {
    char id[32];
    char name[64];
    char prompts[MAX_PROMPTS_PER_PACK][256];
    int  count;
} prompt_pack_t;

esp_err_t         prompt_engine_init(void);
const char       *prompt_get_daily(void);
const char       *prompt_get_random(const char *pack_id);
int               prompt_get_pack_count(void);
const prompt_pack_t *prompt_get_pack(int idx);
esp_err_t         prompt_load_pack(const char *path);
