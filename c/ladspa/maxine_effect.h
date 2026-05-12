#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct maxine_sdk;  // forward decl from maxine_loader.h

struct maxine_effect_config {
    const char *effect_id;        // SDK selector, e.g. NVAFX_EFFECT_DENOISER
    const char *model_folder;     // path to folder with models
    uint32_t sample_rate;         // 48000 or 16000
    float intensity;              // 0.0-1.0
    bool enable_vad;
};

struct maxine_effect {
    struct maxine_sdk *sdk;       // shared SDK handle (not owned)
    void *fx_handle;              // NvAFX_Handle
    const char* effect_id;
};

struct maxine_effect maxine_effect_create(struct maxine_sdk *sdk, const struct maxine_effect_config *config);
void maxine_effect_destroy(struct maxine_effect *node);
void maxine_effect_process(struct maxine_effect* node, const float* src, float* dst, size_t n);
void maxine_effect_set_intensity(struct maxine_effect *node, float intensity);
void maxine_effect_set_vad(struct maxine_effect *node, bool vad);
