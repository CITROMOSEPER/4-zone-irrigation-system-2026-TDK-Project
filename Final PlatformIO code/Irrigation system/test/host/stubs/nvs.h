#pragma once
#include <stddef.h>
typedef int esp_err_t;
#define ESP_OK 0
typedef struct { size_t used_entries, free_entries, total_entries, namespace_count; } nvs_stats_t;
inline esp_err_t nvs_get_stats(const char*, nvs_stats_t* s) { s->used_entries = 120; s->free_entries = 384; s->total_entries = 504; s->namespace_count = 4; return ESP_OK; }
