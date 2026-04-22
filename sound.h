#ifndef SOUND_H
#define SOUND_H

/*
 * OpenAL audio wrapper (OpenAL 1.1 / OpenAL Soft, LGPL).
 * Works from Win98 (OpenAL Soft 1.9.563) up through modern Linux/Win10
 * (OpenAL Soft 1.25.x) with a single `-lopenal` / `-lOpenAL32` link.
 *
 * API:
 *   sndInit(&sys, sampleRate)  — open device, make context, allocate sources
 *   sndShutdown(&sys)          — tear down
 *   sndMakeBuffer(pcm, numSamples, sampleRate) — 16-bit signed mono PCM
 *   sndLoadWav(path)           — 16-bit PCM WAV (mono or stereo→mono mix)
 *   sndFreeBuffer(buffer)
 *   sndPlay(&sys, buffer)      — fire-and-forget on any free source
 *
 * Named registry (SoundLibrary): maps names to buffers so callers (and
 * soon Lua) can say sndLibFind(&lib, "fire") instead of carrying ALuints.
 *
 * Relies on SDL.h being included before this header (main.cpp does so).
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <AL/al.h>
#include <AL/alc.h>

#define SND_NUM_SOURCES 16

typedef ALuint SoundBuffer;

struct SoundSystem {
    ALCdevice  *device;
    ALCcontext *context;
    ALuint     sources[SND_NUM_SOURCES];
};

static int sndInit(SoundSystem *s, int /*sampleRate*/)
{
    s->device = alcOpenDevice(NULL);
    if (!s->device) {
        conLogf("OpenAL: alcOpenDevice failed\n");
        return 0;
    }
    s->context = alcCreateContext(s->device, NULL);
    if (!s->context) {
        conLogf("OpenAL: alcCreateContext failed\n");
        alcCloseDevice(s->device);
        return 0;
    }
    alcMakeContextCurrent(s->context);
    alGenSources(SND_NUM_SOURCES, s->sources);
    return 1;
}

static void sndShutdown(SoundSystem *s)
{
    alDeleteSources(SND_NUM_SOURCES, s->sources);
    alcMakeContextCurrent(NULL);
    alcDestroyContext(s->context);
    alcCloseDevice(s->device);
}

static SoundBuffer sndMakeBuffer(const short *pcm, int numSamples, int sampleRate)
{
    ALuint buf = 0;
    alGenBuffers(1, &buf);
    alBufferData(buf, AL_FORMAT_MONO16, pcm,
                 numSamples * (int)sizeof(short), sampleRate);
    return buf;
}

static void sndFreeBuffer(SoundBuffer buf)
{
    if (buf) alDeleteBuffers(1, &buf);
}

/* Load a mono or stereo 16-bit PCM WAV and upload it as a mono buffer.
   Stereo is downmixed by averaging L/R. Other formats (8-bit, float,
   ADPCM) are rejected with a stderr message and the call returns 0.
   Sample rate is passed through — OpenAL handles the resampling. */
static SoundBuffer sndLoadWav(const char *path)
{
    SDL_AudioSpec spec;
    Uint8 *data = NULL;
    Uint32 len  = 0;
    if (!SDL_LoadWAV(path, &spec, &data, &len)) {
        conLogf("sndLoadWav: %s: %s\n", path, SDL_GetError());
        return 0;
    }
    if (spec.format != AUDIO_S16LSB && spec.format != AUDIO_S16SYS) {
        conLogf("sndLoadWav: %s: unsupported format 0x%x (need 16-bit PCM)\n",
                path, (unsigned)spec.format);
        SDL_FreeWAV(data);
        return 0;
    }
    const int bytesPerFrame = 2 * spec.channels;
    const int frames = (int)(len / (Uint32)bytesPerFrame);
    SoundBuffer out = 0;
    if (spec.channels == 1) {
        out = sndMakeBuffer((const short *)data, frames, spec.freq);
    } else if (spec.channels == 2) {
        short *mono = (short *)malloc((size_t)frames * sizeof(short));
        const short *s = (const short *)data;
        for (int i = 0; i < frames; i++) {
            int l = s[i * 2 + 0];
            int r = s[i * 2 + 1];
            mono[i] = (short)((l + r) / 2);
        }
        out = sndMakeBuffer(mono, frames, spec.freq);
        free(mono);
    } else {
        conLogf("sndLoadWav: %s: unsupported channel count %d\n",
                path, (int)spec.channels);
    }
    SDL_FreeWAV(data);
    return out;
}

/* ---- Named registry ----
 * Small fixed-size map of name → SoundBuffer. Populated at startup so
 * gameplay code (and scripts) can play sounds by name. */
#define SND_MAX_NAMED 64

struct SoundLibrary {
    char        names[SND_MAX_NAMED][32];
    SoundBuffer bufs [SND_MAX_NAMED];
    int         count;
};

static void sndLibInit(SoundLibrary *lib)
{
    memset(lib, 0, sizeof(*lib));
}

static void sndLibRegister(SoundLibrary *lib, const char *name, SoundBuffer b)
{
    if (!b) return;  /* load failed upstream; don't register a zero handle */
    if (lib->count >= SND_MAX_NAMED) {
        conLogf("sndLibRegister: registry full, dropping '%s'\n", name);
        return;
    }
    int i = lib->count++;
    strncpy(lib->names[i], name, sizeof(lib->names[i]) - 1);
    lib->names[i][sizeof(lib->names[i]) - 1] = '\0';
    lib->bufs[i] = b;
}

static SoundBuffer sndLibFind(SoundLibrary *lib, const char *name)
{
    for (int i = 0; i < lib->count; i++) {
        if (strcmp(lib->names[i], name) == 0) return lib->bufs[i];
    }
    return 0;
}

static void sndLibShutdown(SoundLibrary *lib)
{
    for (int i = 0; i < lib->count; i++) sndFreeBuffer(lib->bufs[i]);
    lib->count = 0;
}

/* Find a non-playing source and start it on the given buffer. If all
   sources are busy, steal source[0]. */
static void sndPlay(SoundSystem *s, SoundBuffer buf)
{
    if (!buf) return;
    ALuint chosen = s->sources[0];
    for (int i = 0; i < SND_NUM_SOURCES; i++) {
        ALint state = AL_INITIAL;
        alGetSourcei(s->sources[i], AL_SOURCE_STATE, &state);
        if (state != AL_PLAYING && state != AL_PAUSED) {
            chosen = s->sources[i];
            break;
        }
    }
    alSourceStop(chosen);
    alSourcei(chosen, AL_BUFFER, (ALint)buf);
    alSourcePlay(chosen);
}

#endif /* SOUND_H */
