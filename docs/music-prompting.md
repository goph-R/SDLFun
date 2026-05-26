# Music prompting for SOOB-Engine

Reference prompts for the opening train-ride sequence — calm arrival
into disaster — and the reasoning behind their phrasings. Aimed at
music-generation AIs in the Suno family (musicgeneratorai.com and
similar).

## General principles

These hold across genres:

- **Name instruments concretely**, not categories. "Pizzicato strings"
  beats "strings". "French horn" beats "brass section". Vague prompts
  produce generic generic-sounding output.
- **Pick a BPM.** Even when the music isn't strictly metric, giving a
  tempo range steers the model away from its defaults (which are
  usually too fast). Always state it: `~60 BPM`, `~90 BPM`, etc.
- **Mood-via-setting beats mood-via-adjective.** "Panic in a vast
  industrial space" gives the model more to work with than "scary".
- **Use exclusion clauses.** `No drums, no synths, no vocals` is one of
  the highest-leverage tricks — most generators reach for those by
  default.
- **Avoid the cliché words.** `Epic`, `powerful`, `intense`, `dramatic`
  pull the model toward modern Zimmer-brand bombast (sustained low
  brass braam, pounding sub-bass drums). For classic-cinematic
  orchestral that's the opposite of what you want. Replace with
  `restrained`, `patient`, `sparse`, `panic`, `disaster`, `alarm`,
  `disciplined` — words that describe a *specific feeling*.
- **Don't name composers/artists** — most generators block them or
  generate poorly. Reference the **era and style** instead:
  "late-70s / early-80s cinematic sci-fi".
- **Generate 3–4 variants per prompt.** These tools are stochastic;
  expect one usable take per several tries.

## Calm — "arriving at the facility"

This is the prompt that worked on the first try and made the user get
goosebumps. Captured here for reuse and as a template for the
disaster cue.

```
Slow, ambient orchestral score in the style of late-70s / early-80s
cinematic sci-fi. Sustained low strings (cellos and double basses)
holding a quiet drone, distant French horn swells in modal harmony,
sparse harp arpeggios, occasional shimmering tremolo violin. ~60 BPM.
Vast, contemplative, faintly mysterious — the feeling of arriving
somewhere significant and slightly uncanny. Open intervals, lots of
space between phrases, slow build. No drums, no electronic synths,
no vocals.
```

### Why this works

- **"Late-70s / early-80s cinematic sci-fi"** — the Goldsmith / Williams
  / Horner era. Era-as-style without naming names.
- **"Sustained low strings holding a quiet drone"** — the single most
  important phrase. Without it the model defaults to fast string runs.
- **"Modal harmony"** + **"open intervals"** — stops the AI from
  leaning on standard tonal cadences, which feel too "resolved" for a
  setup scene.
- **"~60 BPM"** — breathing pace. Faster loses the contemplative feel;
  slower starts to feel like a funeral.
- **"Mysterious but not threatening"** — for the *opening*, before
  anything's gone wrong. The dread comes later.

### Variants for different beats of the ride

**Early — wonder (clean landscapes outside):**
```
add: ascending Lydian-mode horn phrase, distant celesta sparkle, sense
of awe and discovery
```

**Mid — military / institutional (approaching the facility):**
```
swap French horns for: low trombone pedal tones with muted trumpet
fanfare echoes in the distance, march-tempo bass pulse but no drums,
disciplined and procedural
```

**Late — uneasy (something is wrong):**
```
add: minor-second clusters in the high strings, sustained dissonance
unresolved, harp drops a single low note, breath of unease
```

## Disaster — "the sirens turn on"

Same palette, now flipped:

```
Urgent orchestral disaster cue — same late-70s / early-80s sci-fi
cinematic palette as before, but now everything has gone wrong.
Opens with a sudden stabbing brass cluster shattering the calm.
Driving low-string ostinato at ~90 BPM underneath, frantic high
violin tremolo shrieking above. Wailing horn and trombone glissandi
shaped like sirens (played orchestrally, not literal alarm sounds).
Rolling timpani builds; crash-cymbal swells and orchestral hits
punctuate. Dissonant — tritones, minor-second clusters, descending
chromatic lines, unresolved tension. Panic in a vast industrial
space. No drum kit, no electronic synths, no vocals. Acoustic
orchestral recording, raw and immediate.
```

### Why this works

- **"Same … palette as before"** — telling the model explicitly to keep
  continuity. Instruments and recording character should still feel
  like the same composer wrote both.
- **"Sudden stabbing brass cluster shattering the calm"** — a hard
  opening attack so the transition from the calm cue lands with weight.
- **"~90 BPM"** — driving but not unhinged. Faster gets cartoonish.
- **"Glissandi shaped like sirens (played orchestrally, not literal
  alarm sounds)"** — important because the in-game siren SFX will be
  playing on top. The music should mirror the gesture in brass, not
  duplicate it. The "not literal" hint steers the model away from
  sample-based siren sounds it might otherwise reach for.
- **"Tritones, minor-second clusters, descending chromatic lines"** —
  these are the exact intervals classic disaster scores use. Naming
  them concretely beats generic words like "scary" or "intense".
- **"No drum kit"** — important at higher tempos because the default is
  rock snare. Timpani and cymbals only.

### Variants for the three beats of the disaster sequence

**Beat 1 — dread, before chaos hits** (something feels wrong, sirens
haven't started):
```
swap to: ~70 BPM, no ostinato yet, just sustained dissonant chords
in low brass and cellos, sparse high-string clusters, single timpani
hits like distant impacts, breath held, growing wrong
```

**Beat 2 — peak chaos** (the disaster prompt above).

**Beat 3 — escape / aftermath running** (still bad, but moving):
```
shift to: ~110 BPM, relentless low-string ostinato locked in, sparse
high brass calls, less dissonant but still unresolved, exhausted
forward momentum, no triumph
```

Chained back-to-back: wonder → dread → catastrophe → flight. Each beat
is a separate generation, crossfade at the cuts.

## Climactic — "the hardest fight in the game"

The finale boss cue. Scales *up* from the disaster — bigger forces,
melodic content (not just texture), heroic intervals, and a clear sense
that this is *the* fight. The trap to avoid is "epic" (which pulls
modern Zimmer braam) — better target words: **climactic**, **decisive**,
**final stand**, **everything-on-the-line**.

```
Climactic orchestral battle cue — same late-70s / early-80s sci-fi
cinematic palette established earlier, now at its biggest. Full
orchestra at the limit: heroic brass calls in perfect fifths and
octaves, driving low-string ostinato at ~120 BPM, urgent woodwind
runs, sweeping melodic violins in mid-to-high register — sweeping,
never shrieking. Timpani and bass drum hits anchor each phrase,
crash cymbals on the peaks. Modal harmony with a memorable recurring
phrase in the horns — the score's main theme stated through gritted
teeth. Sense of "this is the one we either win or die." Dynamic peaks
with brief moments of breath before re-engaging. 2-3 minute piece.
No drum kit, no electronic synths, no vocals. Acoustic orchestral
recording with weight and reverb.
```

### Why this works

- **"Now at its biggest"** — directly tells the model this is the
  loudest cue in the family, scaling up from disaster.
- **"Heroic brass in perfect fifths and octaves"** — these are the
  *heroic* intervals (open horn fanfares). The disaster cue used
  dissonance; the finale uses these as triumphant declarations.
- **"~120 BPM"** — faster than the disaster (90), more forward thrust
  without going cartoonish.
- **"Sweeping … never shrieking"** — explicit guard against the harsh
  high-string problem the disaster cue had.
- **"The score's main theme stated through gritted teeth"** — primes
  the AI to write something that *feels* like it's referencing earlier
  material, even though there's no actual established theme yet. Gives
  that "everything coming together" feeling for free.
- **"Dynamic peaks with brief moments of breath"** — hints at structure.
  Generators don't really build long-form arcs reliably, but this
  phrasing nudges them to vary intensity rather than running flat-out
  for 3 minutes.

### Variants

**Add a wordless choir** (the traditional climactic move — Star Trek
TMP, ROTJ, Conan):
```
add: wordless orchestral choir holding open chords behind the brass,
wide stereo placement, used sparingly on the biggest peaks only,
"aah" syllable not lyrics
```
This breaks the no-vocals streak from earlier cues, but for the climax
specifically it's the established cinematic language. Listening next
to the calm/disaster cues, the choir says "this one matters more".

**More desperate, less heroic** (if the boss is a cosmic horror you're
barely surviving):
```
swap: heroic brass calls in perfect fifths → anguished brass calls in
minor and tritones; replace "main theme stated through gritted teeth"
with "fragmented theme — the hero's motif breaking and being
re-attacked between explosions"
```

**Two-phase structure** (recommended for actual in-game use, since
music gen rarely sustains a 5-min arc):
- **Phase 1** — the main prompt above, "we can win this".
- **Phase 2** — desperate middle: `same palette but pulled back —
  sparse, just low strings and timpani heartbeat, single brass call
  distant, sense of regrouping under fire, ~80 BPM`.
- **Phase 3** — same as Phase 1 but with `final-push intensity, last
  reserves, breakthrough cadence at the end`.

Crossfade between them based on boss health stages. Most "hardest
fight" music in games works this way — the music tracks the player's
progress, not real time.

### Iteration notes

- If it comes out **too generic-blockbuster**, replace `Full orchestra
  at the limit` with `Disciplined full orchestra — every voice present
  but never crowded`. Keeps the size while preserving the
  late-70s/80s clarity (vs. modern wall-of-sound).
- If it comes out **too thin**, add `with section doublings — first
  and second violins in octaves, horns paired with cellos`. Concrete
  instrumentation tricks beat asking for "thicker".
- **"Triumph"** is dangerous as a word — generators interpret it as the
  final fanfare, which is wrong for *during* the fight. Use
  **"defiance"** instead if you want that energy mid-piece.
- **Length:** this is the one place where asking for ~3 minutes is
  worth it. Climactic music needs room. If the tool caps you shorter,
  generate two pieces and stitch.

## Iteration notes

- **Transition between cues:** if you generate at compatible tempos
  (60 BPM calm + 90 BPM disaster), they crossfade cleanly. If the
  disaster cue's opening stab lands right on the cut, even better —
  hard cut on that beat.
- **Avoid "epic", "action", "powerful"** — modern Zimmer braam, doesn't
  match late-70s/80s orchestral. Use **"disaster"**, **"panic"**,
  **"alarm"** instead.
- **Avoid "horror"** — pulls toward static sustained dissonance with
  no momentum. Too inert for an action scene.
- **More military character** (research facility under crisis): add
  `disciplined brass calls fighting against the chaos, fragments of a
  martial fanfare attempting to organise the panic`.
- **Length:** cinematic openings benefit from 90s–3min generations so
  slow builds have room. 30s feels cramped.

## When this file is wrong

If a generated prompt stops working well, the model probably updated.
These prompts target current Suno-family behavior (early 2026). When
output starts feeling generic or won't respect exclusions, the first
thing to try is making the prompt **shorter** — newer models tend to
weight earlier tokens more, so the lead descriptor matters most.
