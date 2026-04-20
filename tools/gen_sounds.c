/*
 * One-off tool: regenerates the placeholder procedural sounds as WAV files.
 *
 * The three original sounds (gunshot, footstep, jump tone) used to be
 * synthesised in main.cpp at startup. They're now shipped as WAV assets;
 * this tool exists so the defaults can be regenerated if the .wav files
 * are lost or the DSP is tweaked.
 *
 * Build + run from the repo root:
 *     gcc tools/gen_sounds.c -o tools/gen_sounds -lm
 *     ./tools/gen_sounds
 *
 * Writes: assets/sounds/{fire,step,jump}.wav  (mono 16-bit PCM, 44100 Hz).
 *
 * Endianness: WAV stores integers little-endian. Direct fwrite of native
 * types is correct on x86/x86_64 — the only targets this engine runs on.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define SAMPLE_RATE 44100

static void write_wav_mono16(const char *path, const short *pcm, int n, int rate)
{
    FILE *f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "gen_sounds: cannot open %s\n", path); exit(1); }

    unsigned int   dataSize      = (unsigned int)(n * 2);
    unsigned int   chunkSize     = 36 + dataSize;
    unsigned short audioFormat   = 1;   /* PCM */
    unsigned short channels      = 1;
    unsigned int   sampleRate    = (unsigned int)rate;
    unsigned int   byteRate      = (unsigned int)(rate * 2);
    unsigned short blockAlign    = 2;
    unsigned short bitsPerSample = 16;
    unsigned int   fmtChunkSize  = 16;

    fwrite("RIFF", 1, 4, f);
    fwrite(&chunkSize, 4, 1, f);
    fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f);
    fwrite(&fmtChunkSize,  4, 1, f);
    fwrite(&audioFormat,   2, 1, f);
    fwrite(&channels,      2, 1, f);
    fwrite(&sampleRate,    4, 1, f);
    fwrite(&byteRate,      4, 1, f);
    fwrite(&blockAlign,    2, 1, f);
    fwrite(&bitsPerSample, 2, 1, f);
    fwrite("data", 1, 4, f);
    fwrite(&dataSize, 4, 1, f);
    fwrite(pcm, 2, (size_t)n, f);
    fclose(f);

    printf("wrote %s (%d samples, %d Hz, mono 16-bit)\n", path, n, rate);
}

static void gen_gunshot(short *buf, int length)
{
    for (int i = 0; i < length; i++) {
        float envelope = 1.0f - (float)i / length;
        envelope *= envelope;
        float noise = (float)(rand() % 65536 - 32768) / 32768.0f;
        buf[i] = (short)(noise * 28000.0f * envelope);
    }
}

static void gen_footstep(short *buf, int length)
{
    for (int i = 0; i < length; i++) {
        float envelope = 1.0f - (float)i / length;
        float thud  = sinf(2.0f * (float)M_PI * 80.0f * (float)i / SAMPLE_RATE);
        float noise = (float)(rand() % 65536 - 32768) / 32768.0f;
        buf[i] = (short)((thud * 0.7f + noise * 0.3f) * 20000.0f * envelope);
    }
}

static void gen_tone(short *buf, int length, float freq)
{
    for (int i = 0; i < length; i++) {
        float t = (float)i / SAMPLE_RATE;
        float envelope = 1.0f - (float)i / length;
        buf[i] = (short)(sinf(2.0f * (float)M_PI * freq * t) * 32000.0f * envelope);
    }
}

int main(void)
{
    /* Deterministic noise so re-running produces identical .wav files. */
    srand(0);

    short *pcm;
    int n;

    n = 4410;
    pcm = (short*)malloc(n * sizeof(short));
    gen_gunshot(pcm, n);
    write_wav_mono16("assets/sounds/fire.wav", pcm, n, SAMPLE_RATE);
    free(pcm);

    n = 2205;
    pcm = (short*)malloc(n * sizeof(short));
    gen_footstep(pcm, n);
    write_wav_mono16("assets/sounds/step.wav", pcm, n, SAMPLE_RATE);
    free(pcm);

    n = 4410;
    pcm = (short*)malloc(n * sizeof(short));
    gen_tone(pcm, n, 220.0f);
    write_wav_mono16("assets/sounds/jump.wav", pcm, n, SAMPLE_RATE);
    free(pcm);

    return 0;
}
