#ifndef SOUND_H
#define SOUND_H

/*
 * Audio abstraction. Default backend is FMOD 3 (the legacy Win98-friendly
 * choice). Define USE_OPENAL at compile time to switch to OpenAL — same
 * header API, different backend.
 *
 * Conventions:
 *   SoundBuffer — opaque handle to a loaded PCM clip
 *   SoundSystem — holds the audio device/context and source pool
 *
 *   sndInit(&sys, sampleRate)  — initialize audio subsystem
 *   sndShutdown(&sys)          — tear down audio subsystem
 *   sndMakeBuffer(pcm, numSamples, sampleRate) — create buffer from 16-bit mono PCM
 *   sndFreeBuffer(buffer)      — release a buffer
 *   sndPlay(&sys, buffer)      — fire-and-forget playback on any free source
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef USE_OPENAL

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
        fprintf(stderr, "OpenAL: alcOpenDevice failed\n");
        return 0;
    }
    s->context = alcCreateContext(s->device, NULL);
    if (!s->context) {
        fprintf(stderr, "OpenAL: alcCreateContext failed\n");
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

#else  /* ---- FMOD 3 backend ---- */

#ifdef _WIN32
#include <fmod/fmod.h>
#else
#include <fmod.h>
#endif

typedef FSOUND_SAMPLE *SoundBuffer;

struct SoundSystem {
    int _unused; /* FMOD 3 state is global */
};

static int sndInit(SoundSystem * /*s*/, int sampleRate)
{
    if (!FSOUND_Init(sampleRate, 32, 0)) {
        fprintf(stderr, "FMOD init failed\n");
        return 0;
    }
    return 1;
}

static void sndShutdown(SoundSystem * /*s*/)
{
    FSOUND_Close();
}

static SoundBuffer sndMakeBuffer(const short *pcm, int numSamples, int sampleRate)
{
    FSOUND_SAMPLE *sample = FSOUND_Sample_Alloc(FSOUND_FREE, numSamples,
        FSOUND_16BITS | FSOUND_SIGNED | FSOUND_MONO | FSOUND_LOOP_OFF,
        sampleRate, 255, 128, 128);
    if (!sample) return NULL;
    void *ptr1, *ptr2;
    unsigned int len1, len2;
    if (FSOUND_Sample_Lock(sample, 0, numSamples * (int)sizeof(short),
                            &ptr1, &ptr2, &len1, &len2)) {
        memcpy(ptr1, pcm, len1);
        if (ptr2 && len2) memcpy(ptr2, pcm + (len1 / sizeof(short)), len2);
        FSOUND_Sample_Unlock(sample, ptr1, ptr2, len1, len2);
    }
    return sample;
}

static void sndFreeBuffer(SoundBuffer buf)
{
    if (buf) FSOUND_Sample_Free(buf);
}

static void sndPlay(SoundSystem * /*s*/, SoundBuffer buf)
{
    if (buf) FSOUND_PlaySound(FSOUND_FREE, buf);
}

#endif

#endif /* SOUND_H */
