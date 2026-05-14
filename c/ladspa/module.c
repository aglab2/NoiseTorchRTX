/*
  (c) Copyright 2021 github.com/lawl GPL3+
  Free software by Richard W.E. Furse. Do with as you will. No
  warranty.
*/

#include <math.h>
#include <threads.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#include "ladspa.h"
#include "utils.h"

#include "../c-ringbuf/ringbuf.h"
#include "maxine_loader.h"
#include "maxine_effect.h"

enum SFInputs
{
  SF_INPUT,
  SF_OUTPUT,
  SF_VAD,
  SF_INTENSITY,

  SF_Count,
};

#define FRAMESIZE_NSAMPLES 480
#define FRAMESIZE_BYTES (480 * sizeof(float))

#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

typedef struct {
  int sdk_ok;
  struct maxine_sdk sdk;
  struct maxine_effect* effect;

  thrd_t maxine_loading_thread;
  _Atomic(struct maxine_effect*) future_effect;

  bool vad;
  float intensity;

  ringbuf_t in_buf;
  ringbuf_t out_buf;

  LADSPA_Data *m_pfVAD;
  LADSPA_Data *m_pfIntensity;
  LADSPA_Data *m_pfInput;
  LADSPA_Data *m_pfOutput;

} rnnoiseFilter;

static LADSPA_Handle
instantiateSimpleFilter(const LADSPA_Descriptor *Descriptor,
                        unsigned long SampleRate) {
  rnnoiseFilter *psFilter;

  psFilter = (rnnoiseFilter *)calloc(1, sizeof(rnnoiseFilter));
  psFilter->in_buf = ringbuf_new(FRAMESIZE_BYTES * 100);
  psFilter->out_buf = ringbuf_new(FRAMESIZE_BYTES * 100);
  psFilter->sdk_ok = maxine_sdk_load(&psFilter->sdk, NULL);

  return psFilter;
}

static bool to_boolean(const LADSPA_Data* data)
{
  if (!data)
    return false;

  return *data > 0.f;
}

static float to_percentage(const LADSPA_Data* data)
{
  if (!data)
    return 1.f;

  return *data / 100.f;
}

static int futureSimpleFilter(void* Instance) {
  rnnoiseFilter *psFilter = (rnnoiseFilter *)Instance;

  struct maxine_effect_config config = {
    .effect_id = NVAFX_EFFECT_DEREVERB_DENOISER,
    .model_folder = "/opt/maxine-afx/features/dereverb_denoiser/models/sm_89",
    .sample_rate = 48000,
    .intensity = psFilter->intensity,
    .enable_vad = psFilter->vad,
  };

  struct maxine_effect* new_effect = malloc(sizeof(struct maxine_effect));
  *new_effect = maxine_effect_create(&psFilter->sdk, &config);
  atomic_store(&psFilter->future_effect, new_effect);

  return 0;
}

static void activateSimpleFilter(LADSPA_Handle Instance) {
  rnnoiseFilter *psFilter;

  psFilter = (rnnoiseFilter *)Instance;

  psFilter->vad       = to_boolean   (psFilter->m_pfVAD); 
  psFilter->intensity = to_percentage(psFilter->m_pfIntensity);

  thrd_create(&psFilter->maxine_loading_thread, futureSimpleFilter, Instance);
  psFilter->effect = NULL;
}

static void deactivateSimpleFilter(LADSPA_Handle Instance) {
  rnnoiseFilter *psFilter;

  psFilter = (rnnoiseFilter *)Instance;

  int _res;
  thrd_join(psFilter->maxine_loading_thread, &_res);

  struct maxine_effect* effect = psFilter->effect;
  if (!effect) effect = atomic_load(&psFilter->future_effect);

  maxine_effect_destroy(effect);
  free(effect);
}

static void connectPortToSimpleFilter(LADSPA_Handle Instance,
                                      unsigned long Port,
                                      LADSPA_Data *DataLocation) {

  rnnoiseFilter *psFilter;

  psFilter = (rnnoiseFilter *)Instance;

  switch (Port) {
  case SF_INTENSITY:
    psFilter->m_pfIntensity = DataLocation;
    break;
  case SF_VAD:
    psFilter->m_pfVAD = DataLocation;
    break;
  case SF_INPUT:
    psFilter->m_pfInput = DataLocation;
    break;
  case SF_OUTPUT:
    psFilter->m_pfOutput = DataLocation;
    break;
  }
}

static void runFilter(LADSPA_Handle Instance, unsigned long n_samples) {

  rnnoiseFilter *psFilter;

  psFilter = (rnnoiseFilter *)Instance;

  ringbuf_t in_buf = psFilter->in_buf;
  ringbuf_t out_buf = psFilter->out_buf;

  if (unlikely(!psFilter->effect)) {
    psFilter->effect = atomic_load(&psFilter->future_effect);
  }

  struct maxine_effect* effect = psFilter->effect;
  if (likely(effect)) {
    bool vad = to_boolean(psFilter->m_pfVAD);
    if (unlikely(vad != psFilter->vad))
    {
      psFilter->vad = vad;
      maxine_effect_set_vad(effect, psFilter->vad);
    }

    float intensity = to_percentage(psFilter->m_pfIntensity);
    if (unlikely(intensity != psFilter->intensity))
    {
      psFilter->intensity = intensity;
      maxine_effect_set_intensity(effect, psFilter->intensity);
    }
  }

  float *in, *out;

  in = psFilter->m_pfInput;
  out = psFilter->m_pfOutput;

  ringbuf_memcpy_into(in_buf, in, n_samples * sizeof(float));

  const size_t n_frames = ringbuf_bytes_used(in_buf) / FRAMESIZE_BYTES;
  float tmpin[n_frames * FRAMESIZE_NSAMPLES];
  ringbuf_memcpy_from(tmpin, in_buf, FRAMESIZE_BYTES * n_frames);

  for (int i = 0; i < n_frames; i++) {
    float tmp[FRAMESIZE_NSAMPLES];
    if (likely(effect)) {
      maxine_effect_process(effect, tmpin + (i * FRAMESIZE_NSAMPLES), tmp, FRAMESIZE_NSAMPLES);
    } else {
      memcpy(tmp, tmpin + (i * FRAMESIZE_NSAMPLES), FRAMESIZE_NSAMPLES * sizeof(float));
    }
    ringbuf_memcpy_into(out_buf, tmp, FRAMESIZE_BYTES);
  }

  int frames_avail = ringbuf_bytes_used(out_buf) / FRAMESIZE_BYTES;
  int samples_avail = frames_avail * FRAMESIZE_NSAMPLES;

  if (samples_avail < n_samples) {
    int skip = n_samples - samples_avail;
    for (int i = 0; i < skip; i++) {
      out[i] = 0.f;
    }
    ringbuf_memcpy_from(out + skip, out_buf, samples_avail * sizeof(float));
  } else {
    ringbuf_memcpy_from(out, out_buf, n_samples * sizeof(float));
  }
}

static void cleanupFilter(LADSPA_Handle Instance) {
  rnnoiseFilter *psFilter = (rnnoiseFilter *)Instance;
  maxine_sdk_unload(&psFilter->sdk);
  ringbuf_free(&(psFilter->in_buf));
  ringbuf_free(&(psFilter->out_buf));
  free(Instance);
}

static LADSPA_Descriptor *g_psDescriptor = NULL;

ON_LOAD_ROUTINE {

  char **pcPortNames;
  LADSPA_PortDescriptor *piPortDescriptors;
  LADSPA_PortRangeHint *psPortRangeHints;

  g_psDescriptor = (LADSPA_Descriptor *)malloc(sizeof(LADSPA_Descriptor));

  if (g_psDescriptor != NULL) {

    g_psDescriptor->UniqueID = 16682994;
    g_psDescriptor->Label = strdup("nt-filter");
    g_psDescriptor->Properties = LADSPA_PROPERTY_HARD_RT_CAPABLE;
    g_psDescriptor->Name = strdup("nt-filter rnnoise ladspa module");
    g_psDescriptor->Maker = strdup("nt-org");
    g_psDescriptor->Copyright = strdup("GPL3+");
    g_psDescriptor->PortCount = SF_Count;
    piPortDescriptors =
        (LADSPA_PortDescriptor *)calloc(SF_Count, sizeof(LADSPA_PortDescriptor));
    g_psDescriptor->PortDescriptors =
        (const LADSPA_PortDescriptor *)piPortDescriptors;
    piPortDescriptors[SF_INTENSITY] = LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL;
    piPortDescriptors[SF_VAD] = LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL;
    piPortDescriptors[SF_INPUT] = LADSPA_PORT_INPUT | LADSPA_PORT_AUDIO;
    piPortDescriptors[SF_OUTPUT] = LADSPA_PORT_OUTPUT | LADSPA_PORT_AUDIO;
    pcPortNames = (char **)calloc(SF_Count, sizeof(char *));
    g_psDescriptor->PortNames = (const char **)pcPortNames;
    pcPortNames[SF_INTENSITY] = strdup("Intensity %%");
    pcPortNames[SF_VAD] = strdup("VAD");
    pcPortNames[SF_INPUT] = strdup("Input");
    pcPortNames[SF_OUTPUT] = strdup("Output");
    psPortRangeHints =
        ((LADSPA_PortRangeHint *)calloc(SF_Count, sizeof(LADSPA_PortRangeHint)));
    g_psDescriptor->PortRangeHints =
        (const LADSPA_PortRangeHint *)psPortRangeHints;
    psPortRangeHints[SF_INTENSITY].HintDescriptor = LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE;
    psPortRangeHints[SF_INTENSITY].LowerBound = 0;
    psPortRangeHints[SF_INTENSITY].UpperBound = 100;
    psPortRangeHints[SF_VAD].HintDescriptor = LADSPA_HINT_TOGGLED | LADSPA_HINT_DEFAULT_0;
    psPortRangeHints[SF_INPUT].HintDescriptor = 0;
    psPortRangeHints[SF_OUTPUT].HintDescriptor = 0;
    g_psDescriptor->instantiate = instantiateSimpleFilter;
    g_psDescriptor->connect_port = connectPortToSimpleFilter;
    g_psDescriptor->activate = activateSimpleFilter;
    g_psDescriptor->run = runFilter;
    g_psDescriptor->run_adding = NULL;
    g_psDescriptor->set_run_adding_gain = NULL;
    g_psDescriptor->deactivate = deactivateSimpleFilter;
    g_psDescriptor->cleanup = cleanupFilter;
  }
}

static void deleteDescriptor(LADSPA_Descriptor *psDescriptor) {
  unsigned long lIndex;
  if (psDescriptor) {
    free((char *)psDescriptor->Label);
    free((char *)psDescriptor->Name);
    free((char *)psDescriptor->Maker);
    free((char *)psDescriptor->Copyright);
    free((LADSPA_PortDescriptor *)psDescriptor->PortDescriptors);
    for (lIndex = 0; lIndex < psDescriptor->PortCount; lIndex++)
      free((char *)(psDescriptor->PortNames[lIndex]));
    free((char **)psDescriptor->PortNames);
    free((LADSPA_PortRangeHint *)psDescriptor->PortRangeHints);
    free(psDescriptor);
  }
}

ON_UNLOAD_ROUTINE { deleteDescriptor(g_psDescriptor); }

const LADSPA_Descriptor *ladspa_descriptor(unsigned long Index) {
  /* Return the requested descriptor or null if the index is out of
     range. */
  switch (Index) {
  case 0:
    return g_psDescriptor;
  default:
    return NULL;
  }
}
