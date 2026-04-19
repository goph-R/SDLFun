# Migrating from FMOD 3 to OpenAL

## Why

FMOD 3 is the legacy reason we can build for Win98 — the DLL and static libs predate modern licensing, and the `FSOUND_*` API is frozen. It works, but it's a dead end: no one ships with FMOD 3 anymore, no one will compile it from source on a modern system, and the API will stay FMOD's unless we rewrite against Firelight's modern API (which is licensed and definitely won't run on Win98).

OpenAL solves this:
- **LGPL / permissive** — OpenAL Soft is unencumbered and redistributable
- **Cross-platform** — same code runs on Linux, Windows, macOS, FreeBSD
- **Modern API with 3D positional audio built in** — good foundation for future work (doppler, attenuation, per-source position already in scope)
- **Available all the way back to Win98** via Creative's reference driver (see below)

Current FMOD surface in the codebase is tiny: PCM generation for three sounds, one init/shutdown, three `FSOUND_PlaySound` calls. ~120 lines. The migration is mostly mechanical.

## Win98 compatibility — the real question

OpenAL is actually the *original Win98-era* audio API: Creative shipped `OpenAL32.dll` for 9x/NT4/2000/XP starting around 2001. Two paths exist:

| Implementation | Win98? | Toolchain | Notes |
|---|---|---|---|
| **Creative reference OpenAL** (`oal_inst.exe` era) | **Yes** | Matches Dev-C++ 4 / MinGW 3.4 | Router + DSound backend. What we actually want. |
| **OpenAL Soft 1.15.x / early 1.17** | Probably (untested) | MinGW 3 or 4 | Pure-software; needs explicit backend config for DSound. |
| **OpenAL Soft 1.19+** | No (XP SP3 minimum) | Modern MinGW / MSVC | The one everyone links against today. |
| **OpenAL Soft master** | No (Vista+) | Modern | Uses WASAPI. |

Strategy: **use Creative's reference `OpenAL32.dll` on Win98**, and **OpenAL Soft 1.23.x on modern Windows / Linux**. Both expose the same AL 1.1 header surface, so our code stays identical. The DLL swap happens at build/install time, not in source.

The reference DLL is archived in several places (Internet Archive, `opentk` mirrors, Creative's old installer). We'd vendor whichever one we verify works on a real Win98 VM alongside the existing `vendor/lib/` FMOD artifacts.

**Risk**: if Creative's `OpenAL32.dll` on Win98 has a link-time API mismatch with the `AL/al.h` we use for OpenAL Soft (minor symbol/signature drift between AL 1.0 and 1.1), we'd need two headers or thin wrappers. Verified AL 1.1 compliance is the goal on the header side.

## Code surface — what actually changes

All FMOD usage is in `main.cpp`:

| FMOD concept | OpenAL equivalent |
|---|---|
| `FSOUND_Init(rate, maxchan, 0)` | `alcOpenDevice(NULL)` + `alcCreateContext` + `alcMakeContextCurrent` |
| `FSOUND_Sample_Alloc(...)` + Lock/Unlock | `alGenBuffers(1, &buf)` + `alBufferData(buf, FMT, data, size, rate)` |
| `FSOUND_PlaySound(FSOUND_FREE, sample)` | Pool of sources → `alSourcei(src, AL_BUFFER, buf)` + `alSourcePlay(src)` |
| `FSOUND_Sample_Free` | `alDeleteBuffers(1, &buf)` |
| `FSOUND_Close()` | `alcDestroyContext` + `alcCloseDevice` |

The PCM generation (`createTone`, `createGunshot`, `createFootstep`) stays identical — they just build a buffer of `short` samples, which OpenAL consumes via `alBufferData` with format `AL_FORMAT_MONO16`.

The only real design delta is **sources**. FMOD's `FSOUND_FREE` transparently picks a free channel. OpenAL wants explicit source handles. Clean solution: a small fixed source pool (say 16), and a "find free source or recycle oldest" helper.

### Proposed `sound.h` surface

Keep it header-only to match the rest of the codebase:

```c
struct SoundSystem {
    ALCdevice *device;
    ALCcontext *context;
    ALuint sources[16];
};

static int sndInit(SoundSystem *s);
static void sndShutdown(SoundSystem *s);

/* Build a buffer from in-memory PCM (16-bit signed mono). */
static ALuint sndMakeBuffer(const short *pcm, int numSamples, int sampleRate);
static void sndFreeBuffer(ALuint buffer);

/* Play a buffer on any free source. Returns the source so caller can adjust
   volume/pitch/position if they want, or ignore it for fire-and-forget. */
static ALuint sndPlay(SoundSystem *s, ALuint buffer);
```

Then `createGunshot()` etc. stay mostly as-is but return `ALuint` buffer IDs instead of `FSOUND_SAMPLE*`.

## Phased migration

### Phase 1 — parallel path, Linux first (2–3 hours)

1. Add `sound.h` with the wrapper above.
2. Conditional compilation: `#ifdef USE_OPENAL` around the new path, `#else` keeps FMOD. Lets us flip with a single define and fall back while testing.
3. Update `Makefile` (Linux) to link `-lopenal` instead of `-lfmod` when `USE_OPENAL=1`.
4. Verify three sounds play correctly: jump, gunshot, footsteps.
5. Check latency against FMOD — OpenAL Soft defaults are usually fine but worth a listen.

### Phase 2 — Windows 10 port (1–2 hours)

1. Download OpenAL Soft 1.23.x prebuilt Win32 MinGW binaries.
2. Drop into `vendor_win10/lib/` (replacing `libfmod.a`) and `vendor_win10/include/AL/` (headers).
3. Update `build_win10.bat`: swap `-lfmod` for `-lopenal32`. Copy `OpenAL32.dll` next to `SDLFun_w10.exe`.
4. Test on a modern Windows box.

### Phase 3 — Win98 path (the risky one, 3–5 hours + a VM)

1. Spin up a Win98 VM (VirtualBox or 86Box) with the existing build chain.
2. Find and install Creative's OpenAL reference driver (installer places `OpenAL32.dll` in `System32`). If the old installer is hard to source, drop a known-good DLL directly into the SDLFun folder — OpenAL32 search order looks in the exe's directory first.
3. Check that `AL/al.h` from OpenAL Soft links cleanly against Creative's DLL at runtime. Symbols we need are all AL 1.0 core: `alGenBuffers`, `alBufferData`, `alGenSources`, `alSourcei`, `alSourcePlay`, `alSourceStop`, `alGetSourcei` (for source reuse). These have been stable since day one.
4. If linking fails, we need the AL 1.0 headers Creative shipped; they differ from modern OpenAL Soft headers only in minor `const` / extension tokens. Keep a `vendor/include/AL/` specific to the Win98 build if necessary.
5. Build with Dev-C++, verify sounds play. Dev-C++'s MinGW 3.4 should have no trouble with the C API.

### Phase 4 — delete FMOD (30 min)

Once all three platforms work:
- Remove `FSOUND_*` code from `main.cpp`
- Remove `#ifdef USE_OPENAL` guards
- Remove `vendor/lib/libfmod*` and `vendor_win10/lib/libfmod*`
- Remove `fmod.dll` from repo root
- Update CLAUDE.md build notes

## Risks & open questions

- **Win98 verification** is the gate. If we can't get a Win98 VM playing sound via Creative's OpenAL, we either keep FMOD conditionally (`#ifdef WIN98 use FMOD, #else use OpenAL`) or drop Win98 audio (silent on Win98 but still runs).
- **Latency**: OpenAL Soft defaults to ~40 ms buffering; acceptable for a game. FMOD 3 with its `FSOUND_Init(..., 32, 0)` flags is roughly similar. Shouldn't be a regression.
- **Max simultaneous sounds**: we allocated 32 channels in FMOD. 16 OpenAL sources is plenty for the current game (gunshot + footsteps + jump, overlap of ~3). Easy to raise.
- **Source recycling strategy**: simplest is "find first stopped source; if none, steal the oldest". For an FPS prototype this won't be noticed. Ambient/music would need tagged sources.
- **3D audio (future)**: OpenAL natively supports `alSource3f(src, AL_POSITION, ...)` and listener position. This unlocks positional gunshots, 3D footsteps, etc. Not in the migration scope but "free" once we're on OpenAL.

## Non-goals for this migration

- No EFX / reverb effects.
- No streaming audio (music/ambient loops from disk). Still all procedural short clips.
- No multi-device selection.
- No HRTF.

These are all things OpenAL can do later, but they'd complicate the migration — keep this change surgical.
