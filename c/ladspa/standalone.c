#include "ladspa.h"

#include <dlfcn.h>
#include <stdio.h>

typedef const LADSPA_Descriptor* (*ladspa_descriptor_fn)(unsigned long Index);

#define _cleanup_(f) __attribute__((cleanup(f)))

static void safe_dlclose(void* lib)
{
    if (lib) dlclose(lib);
}

int main()
{
    static const char name[] = "rnnoise_ladspa.so";

    _cleanup_(safe_dlclose) void* handle = dlopen(name, RTLD_LAZY);
    if (!handle) {
        printf("Failed to find ladspa lib '%s'\n", name);
        return 0;
    }

    ladspa_descriptor_fn load = (ladspa_descriptor_fn) dlsym(handle, "ladspa_descriptor");
    if (!load) {
        printf("Failed to load descriptor fn from library\n", name);
        return 0;
    }

    const LADSPA_Descriptor* desc = load(0);
    if (!desc) {
        printf("Failed to load descriptor fn\n");
        return 0;
    }

    LADSPA_Handle instance0 = desc->instantiate(desc, 48000);
    LADSPA_Handle instance1 = desc->instantiate(desc, 48000);
    if (!instance0 || !instance1) {
        printf("Failed to create handle\n");
        return 0;
    }

    float vad = 1;
    float intensity = 0.7f;
    desc->connect_port(instance0, 2, &vad);
    desc->connect_port(instance0, 3, &intensity);
    desc->connect_port(instance1, 2, &vad);
    desc->connect_port(instance1, 3, &intensity);

    desc->activate(instance0);
    desc->activate(instance1);
    for (int i = 0; i < 1000; i++)
    {
        float ins[10];
        float outs[10];
        
        desc->connect_port(instance0, 0, ins);
        desc->connect_port(instance0, 1, outs);
        desc->connect_port(instance1, 0, ins);
        desc->connect_port(instance1, 1, outs);
        desc->run(instance0, 10);
        desc->run(instance1, 10);
    }
    desc->deactivate(instance0);
    desc->deactivate(instance1);

    desc->cleanup(instance0);
    desc->cleanup(instance1);

    return 0;
}
