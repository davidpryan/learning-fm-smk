# Page 1 — Lantern (SMK-37 Elite) / Ember (FM-1)

> These firmwares build on SLOOP. **On the SMK-37 Elite** the names below are SLOOP's; the keyboard prints them differently: PLAY = SEQ PLAY, REC = SEQ REC, HOME = PATCH, GLO = GLOBE, SCL = SCALE, ENV = CHORD, LFO = NOTE REPEAT, EDIT = PARA, SAVE = BT (there is no SHIFT: GLO is the modifier on both boards), and because the SMK-37's K1 is the volume, **the knobs K1…K7 in these tables are the printed K2…K8**. On the FM-1 the names match. SEQ cycles the four pages; GLO + SEQ shows the page's help on the screen; GLO + ARP shows the results log; GLO + HOME held two seconds returns to plain SLOOP.


Page 1 is the tutor: the screen names every note and chord you play, the
pads are a chord palette coloured by function, and PLAY runs a drill
(echo, chord match, progression, scale). Both boards run the same page.
With the two boxes linked, what you play on one shows on the other's
screen, drills light the pad or key to press next, and a lesson started on
either box runs on both.

Screen: header (key, scale, octave, slow %, green dot = partner present),
big line (note name and octave, or the chord name when three or more keys
are held), sub line (scale degree and interval, or the Roman numeral), a
keyboard strip with held keys lit and the follow-me key bright, the last
four chords as a progression, the pad palette (SMK) or the staff (FM-1),
drill/lesson prompt, lesson title and step, replay dot rows (reference above,
your attempt below, misses red), why-hint line, notice bar.

Out-of-key notes: with hints on they are nudged to the nearest scale note;
with hints off they are muted and the screen says NOT IN THE KEY.

## SMK-37 Elite (Lantern)

| Control | Function |
|---|---|
| Keys | Play. C3 is the lowest key at octave 0 |
| GLO + key | Set the key's root to that note |
| Pads 1-7 | Triads of degrees I-VII (green I, orange V, cyan IV, blue minor, red dim) |
| Pads 9-15 | The same degrees as sevenths (dimmer) |
| Pad 8 / 16 | sus4 / sus2 of the last chord |
| Pad hold 2 s | Why-hint: what the chord wants to do next |
| SCL | Cycle Major / Minor / Pentatonic major / Pentatonic minor / Blues |
| ENV | Hints and nudge on/off |
| FX | Staff hint on/off (FM-1 screen) |
| EDIT / SAVE | Next sound bank / next patch |
| K1 / K2 | Sound bank (Piano, Keys, Bells, Pad, Bass, Pluck) / patch 1-8 |
| K3 / K4 | Reverb / delay send |
| K5 | Slow mode 50-100 % (drills and replay) |
| K6 | Pick a lesson (1-20) |
| K7 | Drill kind: Echo, Chord match, Progression, Scale |
| PLAY | Start / stop the chosen drill |
| REC | Mistake replay: alternates the reference and your attempt |
| ARP | Start the picked lesson / stop the running one |
| HOME | Stop everything, release all notes |
| OCT -/+ | Octave |
| GLO + HOME (hold 2 s) | Switch between LEARN and plain SLOOP |

Faders and wheels pass through to the stock engine (pitch and mod work on
the keys track). The pads use the colour language of every stage; the
follow-me pad is shown at full brightness.

## FM-1 (Ember)

| Control | Function |
|---|---|
| Keys | Play. F3 is the lowest key at octave 0 |
| GLO + key | Set the key's root to that note |
| GLO + SCL | CHORD KEYS on/off: each white key plays its degree's triad (the FM-1's pad palette) |
| SCL | Cycle scale |
| ENV / FX / EDIT / SAVE / K1-K7 / PLAY / REC / ARP / HOME / OCT | As on the SMK-37 |
| GLO + HOME (hold 2 s) | Switch between LEARN and the stock FM1 Open UI |

The FM-1 screen adds the mini staff (treble above middle C, bass below)
showing the held note, or the drill's target note while it waits for you.

## Drills

* **Echo**: a note sounds; play it (any octave counts, the screen says so).
* **Chord match**: a chord sounds; find it on the pads or with CHORD KEYS.
* **Progression**: four chords in order from the lesson (default I-V-vi-IV); play them back one by one.
* **Scale**: follow the lit key up the scale.

Ten rounds per drill; 80 % passes and is logged. A drill that waits more
than six seconds (scaled by slow mode) counts a miss and moves on. Lessons
chain drills automatically; a TEXT step continues on any key.

## Deviations from the design

* 16 pads, so the design's pads 17/18 became REC (replay A/B) and the
  octave buttons.
* Tone / Attack / Release macros are not exposed; K1/K2 choose patches from
  six beginner banks instead and K3/K4 are the engine's reverb and delay
  sends. The DX7 editor stays in the stock UI.
* Pad aftertouch does not add a seventh (the sevenths have their own pads).
* The practice log and unlocks are RAM only in this release; `LCMD_LOGDUMP`
  exports them over SysEx.
