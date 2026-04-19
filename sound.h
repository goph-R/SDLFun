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
 *   sndFreeBuffer(buffer)
 *   sndPlay(&sys, buffer)      — fire-and-forget on any free source
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

#endif /* SOUND_H */
