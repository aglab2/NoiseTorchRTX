#include "maxine_effect.h"
#include "maxine_loader.h"
#include "nv_types.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

void maxine_effect_process(struct maxine_effect* node, const float* src, float* dst, size_t n)
{
    assert(n == 480);

    const float *in_ptr[1] = { src };
    float *out_ptr[1] = { dst };
    NvAFX_Status st = node->sdk->Run(node->fx_handle, in_ptr, out_ptr, n, 1);

    if (st != NVAFX_STATUS_SUCCESS) {
        memcpy(dst, src, n * sizeof(float));
        return;
    }
}

static int init_effect(struct maxine_effect *node, const struct maxine_effect_config *config)
{
    fprintf(stderr, "maxine: init_effect(%p)\n", node);
    NvAFX_Status st;
    char model[4096];

    st = node->sdk->CreateEffect(config->effect_id, &node->fx_handle);
    if (st != NVAFX_STATUS_SUCCESS) {
        fprintf(stderr, "maxine: CreateEffect(%s) failed: %s\n",
                config->effect_id, nvafx_status_str(st));
        return -1;
    }

    // Build model file path: <path>/<effect>_<rate>k.trtpkg
    {
        const char *rate_suffix = (config->sample_rate == 48000) ? "48k" : "16k";
        snprintf(model, sizeof(model), "%s/%s_%s.trtpkg", config->model_folder, config->effect_id, rate_suffix);
    }

    // SDK 2.1.0 requires SetStringList for model_path
    if (node->sdk->SetStringList) {
        const char *model_list[1] = { model };
        st = node->sdk->SetStringList(node->fx_handle, NVAFX_PARAM_MODEL_PATH,
                                       model_list, 1);
    } else {
        st = node->sdk->SetString(node->fx_handle, NVAFX_PARAM_MODEL_PATH, model);
    }
    if (st != NVAFX_STATUS_SUCCESS) {
        fprintf(stderr, "maxine: SetModel(%s) failed: %s\n", model, nvafx_status_str(st));
        goto fail;
    }

    st = node->sdk->SetU32(node->fx_handle, NVAFX_PARAM_INPUT_SAMPLE_RATE,
                           config->sample_rate);
    if (st != NVAFX_STATUS_SUCCESS) {
        fprintf(stderr, "maxine: SetU32(input_sample_rate, %u) failed: %s\n", config->sample_rate, nvafx_status_str(st));
        goto fail;
    }

    st = node->sdk->SetU32(node->fx_handle, NVAFX_PARAM_OUTPUT_SAMPLE_RATE,
                           config->sample_rate);
    if (st != NVAFX_STATUS_SUCCESS) {
        // Some effects don't support output_sample_rate, that's OK
        fprintf(stderr, "maxine: SetU32(output_sample_rate) failed (may be OK): %s\n", nvafx_status_str(st));
    }

    st = node->sdk->SetU32(node->fx_handle, NVAFX_PARAM_NUM_STREAMS, 1);
    if (st != NVAFX_STATUS_SUCCESS)
        fprintf(stderr, "maxine: SetU32(num_streams) failed (may be OK): %s\n",  nvafx_status_str(st));

    st = node->sdk->SetU32(node->fx_handle, NVAFX_PARAM_NUM_INPUT_CHANNELS, 1);
    if (st != NVAFX_STATUS_SUCCESS)
        fprintf(stderr, "maxine: SetU32(num_input_channels) failed (may be OK): %s\n",  nvafx_status_str(st));

    st = node->sdk->SetU32(node->fx_handle, NVAFX_PARAM_NUM_OUTPUT_CHANNELS, 1);
    if (st != NVAFX_STATUS_SUCCESS)
        fprintf(stderr, "maxine: SetU32(num_output_channels) failed (may be OK): %s\n", nvafx_status_str(st));

    st = node->sdk->SetFloat(node->fx_handle, NVAFX_PARAM_INTENSITY_RATIO, config->intensity);
    if (st != NVAFX_STATUS_SUCCESS)
        fprintf(stderr, "maxine: SetFloat(intensity) failed (may be OK): %s\n", nvafx_status_str(st));

    if (config->enable_vad) {
        st = node->sdk->SetU32(node->fx_handle, NVAFX_PARAM_ENABLE_VAD, 1);
        if (st != NVAFX_STATUS_SUCCESS)
            fprintf(stderr, "maxine: SetU32(enable_vad) failed (may be OK): %s\n", nvafx_status_str(st));
    }

    fprintf(stderr, "maxine: loading model from %s ...\n", model);

    st = node->sdk->Load(node->fx_handle);
    if (st != NVAFX_STATUS_SUCCESS) {
        fprintf(stderr, "maxine: Load() failed: %s\n", nvafx_status_str(st));
        goto fail;
    }

    // Query frame size
    unsigned int fs = 0;
    unsigned int expected_fs = config->sample_rate / 100;
    st = node->sdk->GetU32(node->fx_handle, NVAFX_PARAM_NUM_SAMPLES_PER_INPUT_FRAME, &fs);
    if (st == NVAFX_STATUS_SUCCESS && fs > 0) {
        if (fs != expected_fs)
        {
            fprintf(stderr, "maxine: unexpected fs %d != %d\n", fs, expected_fs);
        }
    } else {
        fprintf(stderr, "maxine: failed to find expected fs, assuming ok\n");
        fs = expected_fs;
    }

    fprintf(stderr, "maxine: '%s' loaded, frame_size=%u (%.1f ms)\n",
            node->effect_id, fs,
            (float)expected_fs / config->sample_rate * 1000.0f);

    return 0;

fail:
    node->sdk->DestroyEffect(node->fx_handle);
    node->fx_handle = NULL;
    return -1;
}

struct maxine_effect maxine_effect_create(struct maxine_sdk *sdk, const struct maxine_effect_config *config)
{
    struct maxine_effect node = (struct maxine_effect){};

    node.sdk = sdk;
    node.effect_id = config->effect_id;

    if (init_effect(&node, config) < 0) {
        return node;
    }

    fprintf(stderr, "maxine: audio node '%s' created and connected\n", node.effect_id);
    return node;
}

void maxine_effect_destroy(struct maxine_effect *node)
{
    if (node->fx_handle && node->sdk) {
        node->sdk->DestroyEffect(node->fx_handle);
        node->fx_handle = NULL;
    }
}

void maxine_effect_set_intensity(struct maxine_effect *node, float intensity)
{
    if (intensity < 0.0f) intensity = 0.0f;
    if (intensity > 1.0f) intensity = 1.0f;

    if (node->fx_handle && node->sdk) {
        NvAFX_Status st = node->sdk->SetFloat(
            node->fx_handle, NVAFX_PARAM_INTENSITY_RATIO, intensity);
        if (st != NVAFX_STATUS_SUCCESS)
            fprintf(stderr, "maxine: SetFloat(intensity) '%s' failed: %s\n", node->effect_id, nvafx_status_str(st));
    }

    fprintf(stderr, "maxine: intensity '%s' set to %.2f\n", node->effect_id, intensity);
}

void maxine_effect_set_vad(struct maxine_effect *node, bool vad)
{
    if (node->fx_handle && node->sdk) {
        NvAFX_Status st = node->sdk->SetU32(node->fx_handle, NVAFX_PARAM_ENABLE_VAD, vad);
        if (st != NVAFX_STATUS_SUCCESS)
            fprintf(stderr, "maxine: SetU32(enable_vad) failed (may be OK): %s\n", nvafx_status_str(st));
    }

    fprintf(stderr, "maxine: vad '%s' set to %d\n", node->effect_id, vad);
}
