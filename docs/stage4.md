# Page 4 — Finish it: Stage (SMK-37 Elite) + Scribe (FM-1)

> These firmwares build on SLOOP: the SMK-37 Elite's buttons are the ones its HARDWARE CALIBRATION maps to SLOOP's FX, SEL, ENV, LFO, EDIT, GLO, HOME, SAVE, ARP, SEQ, PLAY, REC, OCT−/OCT+ (see `sloop/SMK37.md`); there is no SHIFT, so GLO is the modifier on both boards. Sounds are SLOOP's engines and presets (K1 engine, K2 preset on page 1). SEQ cycles the four pages; GLO + HOME held two seconds returns to plain SLOOP.


Page 4 (press SEQ until the header reads STAGE or SCRIBE). The loop you built
on pages 2 and 3 becomes a *section*; eight sections, queued with the hands,
make a song. Everything lives in RAM: sections, the song record and the
practice log are gone at power-off (dump the log over SysEx first).

## Sections

A section is a snapshot of the whole loop: chords, bass rule, drum lanes,
tempo, swing, fill, key, plus the melody track's cells (up to 64 steps) and
an energy level. Eight slots. Each slot has a type:

| Type | Colour | | Type | Colour |
| --- | --- | --- | --- | --- |
| Intro | grey | | Bridge | purple |
| Verse | blue | | Break | white |
| Pre | teal | | Outro | grey |
| Chorus | orange | | Free | green |

The type used by the next store is shown on screen ("STORE AS VERSE"); SCL or
K2 cycles it. A stored slot's type can be changed later (see below).

Recalling a slot never happens mid-loop: the queued section is written back
when the loop wraps (or at the next beat for a double-tap), so the music
never stutters. Recall copies the model back, re-renders drums, chords and
bass, restores the melody cells, tempo, swing and energy, and sets the key.

## Stage (SMK-37 Elite)

| Control | Action |
| --- | --- |
| Pads 1-8 | Queue slot 1-8: **tap** = at the next loop, **double-tap** = at the next beat, **hold** (over 0.6 s) = play while held, return to the previous section at the loop after release |
| Pad 1-8 held 2 s | Why-hint: type, energy, bar count and the first four chords |
| GLO + pads 9-16 | Store the current loop into slot 1-8 with the STORE AS type |
| Pads 9-16 (no GLO) | Cycle the type of an occupied slot 1-8 |
| SCL | Cycle the STORE AS type |
| SAVE | Store into the cursor slot (the last slot stored or played) |
| PLAY | Start / stop the loop |
| REC | Start the song record; after a lesson pass, alternate ghost / performed order |
| HOME | Stop the record and the loop; a second HOME within 2 s discards the record |
| ARP | Start / stop the picked lesson |
| ENV | Next transition (same as K5) |
| K1 | Energy 1-8 |
| K2 | STORE AS type |
| K3 | Pick lesson |
| K4 | Slow mode 50-100 % |
| K5 | Transition: None, Drop, Riser, Fill, Hand-off |
| K6 | Tempo nudge, clamped to +-10 % of the section's own tempo |
| K7 | Swing 50-75 % |
| Fader 1 | Energy (overrides the section's energy live) |
| Fader 2 | Drums master |
| Fader 3 | Music master (bass, chords, shadows) |
| Fader 4 | Melody master (melody track and live keys) |
| Keys | Live melody on the melody track |

Pad light language: a stored slot shows its type colour; the playing slot
pulses on the beat; a queued slot blinks; the follow-me slot is bright and
blinks through the last bar; empty slots are dark. Pads 9-16 glow dimly in
their slot's type and light fully while pressed.

## Scribe (FM-1)

The FM-1 has no pads, so the slot functions sit on the keys while GLO is held.

| Control | Action |
| --- | --- |
| GLO + keys 1-8 (F3..C4 at octave 0) | Queue slot 1-8 (tap / double-tap / hold as on the SMK) |
| GLO + keys 9-16 (C#4..G#4) | Store the loop into slot 1-8 with the STORE AS type |
| SCL | Cycle the STORE AS type |
| SAVE | Store into the cursor slot |
| PLAY / REC / HOME / ARP / ENV | As on the SMK |
| K1-K7 | As on the SMK (K1 is the energy "fader") |
| Keys (GLO up) | Live melody |

Why-hints work the same way: hold a GLO+key slot for 2 s. There are no
masters on the FM-1; levels stay where page 2 left them.

## Energy

Energy 1-8 is how many tracks are unmuted, added in this order: drums, bass,
chords, melody, shadow A, B, C (8 = everything). It is stored with each
section and applied on recall; K1 / fader 1 override it live. Energy 1 is
drums only.

## Transitions (K5)

Applied as one-bar edits when the last bar before a queued change begins,
and undone by the recall itself:

* **Drop** — drums and bass are muted for the last bar.
* **Riser** — the hat lane plays every 8th in the first half of the bar and
  every 16th in the second; the drum and chord reverb sends climb.
* **Fill** — the model's fill is forced on for the bar (snare and hat roll in
  the last beat).
* **Hand-off** — the melody's velocity fades across the bar and the incoming
  section's first chord enters on the last beat.
* **None** — a plain cut at the loop.

## Song record

REC starts the loop (if stopped) and logs every section change: slot, type,
bars played and energy, up to 32 entries. HOME finishes and logs a
`LOGK_SONG` entry (sections, tempo, seconds); a second HOME within 2 s
discards the record. The thin song bar under the clock shows the record as
coloured blocks with the play head in the current block.

## Lessons and follow-me

A lesson step of type `song` carries a target order in `data[]` (section
types 1..8 = Intro..Free) and a tempo. ARP starts the picked lesson (K3).
Store one slot of each type the lesson asks for; the slot of the next ghost
section lights two bars before it is due and blinks through the last bar.
Queue it while it is lit for a green mark, later for yellow. When as many
sections have been played as the ghost holds, the step is scored: order
match and energy match, averaged (energy targets per type: Intro 2, Verse
4, Pre 5, Chorus 8, Bridge 3, Break 1, Outro 2, Free 4). A pass needs the
step's target percentage and no late changes. Slow mode is allowed but a
slow pass never counts (shared lesson rule).

**Mistake replay:** after a scored pass, REC alternates the two strips at
the bottom of the screen: the ghost order and the performed one, mismatched
sections in red, late ones in yellow.

## SysEx (F0 7D 'L' 'N' 01 dev 04 cmd ... F7)

| Cmd | Payload | Sent when |
| --- | --- | --- |
| QUEUE (21) | slot, mode (0 bar, 1 beat, 2 hold) | a slot is queued; the partner queues the same slot |
| TRANSITION (22) | type | K5 / ENV or a transition fires |
| SECTION (23) | slot (127 = none), type, state (0 stored, 1 playing), bars left | store, recall, every bar |
| SONGPOS (24) | bar, beat, seconds low 7, seconds high 7 | every beat |
| PATH (4) | 0 pad / 2 clear, index | follow-me target changes |
| LESSON (25) | index | K3 |
| LOG (8) | streak, stage, attempts | a song step is scored |
| REC (12), REPLAY (6), SLOW (7) | as in earlier stages | |

A box with no sections of its own mirrors the partner's SECTION frames on
its big line.

## Spec deviations and approximations

* **16 pads, not 18.** The spec's overflow functions moved to buttons: SAVE
  stores into the cursor slot, REC is both song record and replay A/B, PLAY
  is transport, ENV steps the transition.
* **Store from pads 9-16 needs GLO**; without GLO those pads cycle the
  slot's type instead (the spec puts type cycling on SCL, which also works).
* **Sections hold the melody track only** (the spec says "the tracks'
  cells"). Shadows are re-rendered by Stage 3 from the restored model, and
  the drum, chord and bass tracks are rendered from the model, so only the
  melody needs raw cells. 8 x (model + 64 cells) is about 2.8 KB.
* **Transitions are fixed recipes**, not "regenerated from the two sections'
  differences": the Hand-off uses the incoming section's first chord and
  Drop/Riser/Fill are the same whichever sections are involved.
* **Hold-to-play** returns to the slot you came from at the loop after
  release; a hold shorter than 0.6 s is a plain tap.
* **Double-tap = next beat** recalls the whole section on the beat; the
  engine then runs the new loop from its current step (the loop phase is not
  reset).
* **Follow-me timing** uses the loop position: "two bars early" is the last
  two bars of a four-bar window from the previous change (shorter loops
  light the pad as soon as the previous section is in).
* **Energy fader 1** uses the same `groove_mix_toggle` as the per-section
  energy, so the section's stored energy is replaced, not layered.
* **Elapsed time** counts from PLAY or REC, whichever came last, and freezes
  on stop.
* No Flash writes anywhere; `groove_edit_checkpoint` is only called by the
  engine's own mute/edit paths.
