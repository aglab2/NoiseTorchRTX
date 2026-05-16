/*
  Free software by Richard W.E. Furse. Do with as you will. No
  warranty.
*/

#define _GNU_SOURCE

#include <math.h>
#include <threads.h>
#include <poll.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/inotify.h>
#include <unistd.h>

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

// MARK: Downstream from ladspa

typedef struct {
  uint64_t in_cursor;
  ringbuf_t out_buf;

  LADSPA_Data *m_pfVAD;
  LADSPA_Data *m_pfIntensity;
  LADSPA_Data *m_pfInput;
  LADSPA_Data *m_pfOutput;
} filter_t;

// MARK: Maxine operations

#define MAX_DOWNSTREAMS 8

typedef struct {
  bool vad;
  float intensity;
} maxine_config_t;

typedef struct {
  int refcount;

  bool sdk_ok;
  bool async_started;

  struct maxine_sdk sdk;
  struct maxine_effect* effect;

  thrd_t maxine_loading_thread;
  _Atomic(struct maxine_effect*) future_effect;

  maxine_config_t current_config;

  ringbuf_t in_buf;
  uint64_t in_cursor;

  ringbuf_t downstreams[MAX_DOWNSTREAMS];
  int downstreams_count;

  _Atomic(int) incoming_intensity;
  int shutdown_fd;
} maxine_t;

static maxine_t* Maxine = NULL;

static int maxineAsyncLoad(void* Instance) {
  maxine_t *psFilter = (maxine_t *)Instance;
  if (!psFilter->sdk_ok)
    return 0;

  struct maxine_effect_config config = {
    .effect_id = NVAFX_EFFECT_DEREVERB_DENOISER,
    .model_folder = "/opt/maxine-afx/features/dereverb_denoiser/models/sm_89",
    .sample_rate = 48000,
    .intensity = psFilter->current_config.intensity,
    .enable_vad = psFilter->current_config.vad,
  };

  struct maxine_effect* new_effect = malloc(sizeof(struct maxine_effect));
  *new_effect = maxine_effect_create(&psFilter->sdk, &config);
  atomic_store(&psFilter->future_effect, new_effect);

  char configPath[1024];
  const char* xdgCfg = getenv("XDG_CONFIG_HOME");
  if (xdgCfg)
  {
    sprintf(configPath, "%s/noisetorch/config.toml", xdgCfg);
  }
  else
  {
    sprintf(configPath, "%s/.config/noisetorch/config.toml", getenv("HOME"));
  }

  int inot = inotify_init1(IN_NONBLOCK);
  int wd = inotify_add_watch(inot, configPath, IN_MODIFY);
  int shutdown = psFilter->shutdown_fd;

  while (true)
  {
    struct pollfd fds[] = {
      { .fd = shutdown, .events = POLLIN },
      { .fd = inot    , .events = POLLIN },
    };

    int err = poll(fds, sizeof(fds) / sizeof(*fds), -1);
    if (err < 0)
      continue;

    if (fds[0].revents & POLLIN)
    {
      break;
    }

    if (fds[1].revents & POLLIN)
    {
      char line[256];
      while (read(inot, line, sizeof(line)) > 0)
      {
        FILE* f = fopen(configPath, "r");
        while (fgets(line, sizeof(line), f))
        {
          char* at = strstr(line, " = ");
          if (!at) continue;

          char* value = at + 3;

          if (0 == memcmp(line, "Intensity", sizeof("Intensity") - 1))
          {
            int percentage = atoi(value);
            atomic_store_explicit(&psFilter->incoming_intensity, percentage, __ATOMIC_RELAXED);
          }
        }
        fclose(f);
      }
    }
  }

  close(inot);

  return 0;
}

static void maxineInit()
{
  if (!Maxine)
  {
    Maxine = (maxine_t*) calloc(1, sizeof(maxine_t));
    Maxine->sdk_ok = maxine_sdk_load(&Maxine->sdk, NULL);
    Maxine->in_buf = ringbuf_new(FRAMESIZE_BYTES * 100);
    Maxine->shutdown_fd = eventfd(0, EFD_NONBLOCK);
  }

  Maxine->refcount++;
}

static void maxineDeinit()
{
  Maxine->refcount--;
  if (0 != Maxine->refcount)
    return;

  if (Maxine->async_started)
  {
    uint64_t val = 1;
    write(Maxine->shutdown_fd, &val, sizeof(val));

    int _res;
    thrd_join(Maxine->maxine_loading_thread, &_res);

    struct maxine_effect* effect = Maxine->effect;
    if (!effect) effect = atomic_load(&Maxine->future_effect);
    Maxine->effect = NULL;

    if (effect) {
      maxine_effect_destroy(effect);
      free(effect);
    }

    Maxine->async_started = false;
  }

  if (Maxine->sdk_ok)
    maxine_sdk_unload(&Maxine->sdk);

  ringbuf_free(&Maxine->in_buf);
  close(Maxine->shutdown_fd);
  free(Maxine);
  Maxine = NULL;
}

static void maxineLoad(maxine_config_t init_cfg)
{
  if (!Maxine->async_started)
  {
    Maxine->current_config = init_cfg;
    Maxine->incoming_intensity = init_cfg.intensity * 100;
    thrd_create(&Maxine->maxine_loading_thread, maxineAsyncLoad, Maxine);
    Maxine->async_started = true;
  }
}

static void maxineRegister(filter_t* filter, maxine_config_t init)
{
  int next = Maxine->downstreams_count++;
  Maxine->downstreams[next] = filter->out_buf;

  if (0 == next) maxineLoad(init);
}

static void maxineDeregister(filter_t* stream)
{
  for (int i = 0; i < Maxine->downstreams_count; i++)
  {
    if (Maxine->downstreams[i] == stream->out_buf)
    {
      int unnext = --Maxine->downstreams_count;
      Maxine->downstreams[i] = Maxine->downstreams[unnext];
      Maxine->downstreams[unnext] = NULL;
    }
  }
}

static void maxinePrepareFilter(void)
{
  if (unlikely(!Maxine->effect)) {
    Maxine->effect = atomic_load(&Maxine->future_effect);
  }

  struct maxine_effect* effect = Maxine->effect;
  if (likely(effect)) {
    float intensity = atomic_load_explicit(&Maxine->incoming_intensity, __ATOMIC_RELAXED) / 100.f;
    if (unlikely(intensity != Maxine->current_config.intensity))
    {
      Maxine->current_config.intensity = intensity;
      maxine_effect_set_intensity(effect, intensity);
    }
  }
}

static void maxineDoRun(float* in, unsigned long n_samples)
{
  ringbuf_t in_buf = Maxine->in_buf;
  ringbuf_t* out_bufs = Maxine->downstreams;
  int out_bufs_count = Maxine->downstreams_count;

  ringbuf_memcpy_into(in_buf, in, n_samples * sizeof(float));

  const size_t n_frames = ringbuf_bytes_used(in_buf) / FRAMESIZE_BYTES;
  float tmpin[n_frames * FRAMESIZE_NSAMPLES];
  ringbuf_memcpy_from(tmpin, in_buf, FRAMESIZE_BYTES * n_frames);

  struct maxine_effect* effect = Maxine->effect;
  for (int i = 0; i < n_frames; i++) {
    float tmp[FRAMESIZE_NSAMPLES];
    if (likely(effect)) {
      maxine_effect_process(effect, tmpin + (i * FRAMESIZE_NSAMPLES), tmp, FRAMESIZE_NSAMPLES);
    } else {
      memcpy(tmp, tmpin + (i * FRAMESIZE_NSAMPLES), FRAMESIZE_NSAMPLES * sizeof(float));
    }

    for (int i = 0; i < out_bufs_count; i++)
      ringbuf_memcpy_into(out_bufs[i], tmp, FRAMESIZE_BYTES);
  }
}

static void maxineRun(float* in, unsigned long n_samples, uint64_t cursor)
{
  maxinePrepareFilter();

  uint64_t start = cursor;
  uint64_t end = cursor + n_samples;

  if (end <= Maxine->in_cursor)
    return;

  float* maxine_in = in + (Maxine->in_cursor - start);
  unsigned long maxine_samples = (unsigned long) (end - Maxine->in_cursor);

  maxineDoRun(maxine_in, maxine_samples);

  Maxine->in_cursor = end;
}

// MARK: Downstream mono merge

static LADSPA_Handle instantiateSimpleFilter(const LADSPA_Descriptor *Descriptor, unsigned long SampleRate) {
  maxineInit();

  filter_t *psFilter;

  psFilter = (filter_t *)calloc(1, sizeof(filter_t));
  psFilter->out_buf = ringbuf_new(FRAMESIZE_BYTES * 100);

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

static void activateSimpleFilter(LADSPA_Handle Instance) {
  filter_t *psFilter = (filter_t *)Instance;

  maxine_config_t cfg;
  cfg.vad       = to_boolean   (psFilter->m_pfVAD); 
  cfg.intensity = to_percentage(psFilter->m_pfIntensity);

  maxineRegister(psFilter, cfg);
}

static void deactivateSimpleFilter(LADSPA_Handle Instance) {
  filter_t *psFilter = (filter_t *)Instance;

  maxineDeregister(psFilter);
}

static void connectPortToSimpleFilter(LADSPA_Handle Instance,
                                      unsigned long Port,
                                      LADSPA_Data *DataLocation) {

  filter_t *psFilter = (filter_t *)Instance;

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
  filter_t *psFilter = (filter_t *)Instance;

  // Drive input
  {
    maxineRun(psFilter->m_pfInput, n_samples, psFilter->in_cursor);
    psFilter->in_cursor += n_samples;
  }

  // Drive output
  {
    ringbuf_t out_buf = psFilter->out_buf;
    float *out = psFilter->m_pfOutput;
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
}

static void cleanupFilter(LADSPA_Handle Instance) {
  maxineDeinit();
  filter_t *psFilter = (filter_t *)Instance;
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
