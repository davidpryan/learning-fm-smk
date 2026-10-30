# Stage 3 — Grow it: Shadow (SMK-37 Elite / Pro) + Tape (FM-1)

Page 3 (press SEQ until the header reads SHADOW or TAPE). Everything from
pages 1 and 2 still works on its own page; page 3 adds tracks that follow
the loop and a tape deck for the melody and those tracks.

A **shadow** is a new track described by its relationship to a track you
already have. You never program its notes: you pick a *source* (Chords,
Melody or Drums) and a *rule*, and the shadow is rendered from the source.
Change the chords, the melody or the hat lane on page 2 and the shadow
re-renders itself a moment later.

| Rule | Follows | Plays |
| --- | --- | --- |
| Hold | Chords | the chord tones, sustained for each beat |
| Arp | Chords | the chord tones in a pattern you tap (up to 8 steps) |
| Third above | Melody | the melody a diatonic third up |
| Sixth below | Melody | the melody a diatonic sixth down |
| Echo | Melody | the melody one beat late, an octave up, at 60 % velocity |
| Answer | Melody | in bars where the melody is silent: its last four notes, backwards |
| Top note | Chords | the highest chord tone as a slow line |
| Ghost | Drums | quiet closed-hat hits on the steps the Hat lane leaves empty |

Up to three shadows (A, B, C) can exist at once. Hold, Arp and Top note
read the song chords; the melody rules read the Melody track's cells;
Ghost reads the drum track.

**Obedience** is how strictly the shadow sticks to its rule: strict means
every rendered note plays at its written velocity; loose lowers the step
probability (the engine's per-step probability, 4/8 to 6/8) and adds a
velocity wobble of up to ±30.

## Screen (both boards)

* Header: page name, bar count, tempo, S50..S95 when slow mode is on, a
  green dot when the partner box is talking.
* Big line: the selected shadow (`A HOLD`) with its source, octave,
  density and obedience, or `CHORDS / PICK A RULE` while a source is armed.
* Four tape lanes: MEL, SH A, SH B, SH C. Each shows a note-density bar,
  the play head (orange, red while recording), the punched bars in red
  under the bar, the take number (T1..T4) and a level meter (yellow while
  that track is soloed). The selected lane has a white marker, red while
  it records.
* Prompt line: what REC/PLAY will do next, or the lesson prompt.
* Takes row: one dot per take of the selected track (green = loaded), and
  the selected send (REV/DLY) with its amount.
* Hint line (yellow) and the notice bar.

## SMK-37 Elite / Pro (Shadow)

Pads:

| Pad | Function |
| --- | --- |
| 1, 2, 3 | source: Chords (cyan), Melody (green), Drums (red). Press one, then a rule pad |
| 4, 5, 6 | shadow A, B, C: select for K1-K4 editing; hold 2 s to remove it |
| 7 | next tape track (Melody, A, B, C) |
| 8 | export the four tape tracks over SysEx |
| 9-16 | rules in the table order (Hold ... Ghost). A rule pad lights in its source's colour; dim means "not for the armed source" |

* After picking Arp, the rule pads turn orange for four seconds: tap up to
  eight of them to enter the arpeggio pattern (pad 9 = lowest chord tone,
  pad 10 the next, ... pads beyond the chord size jump an octave).
* Hold a rule pad for 2 s: the why-hint names the interval and, while the
  loop plays, you hear the source alone for two bars and then the shadow
  alone. The first time you use each rule the hint shows by itself.
* Aftertouch on pads 1-6 is a momentary solo of that track (source or
  shadow); it releases with the pad.
* Pad colours: a shadow pad is a dim version of its source's colour, full
  brightness while soloed. While you hold a rule pad its source pad flashes.

Knobs: K1 shadow octave (-2..+2), K2 density (1..7; for Arp also the
rate: 1-2 = quarter notes, 3-5 = eighths, 6-7 = sixteenths), K3 shadow
velocity (20..150 %), K4 shadow patch (bank/patch from the beginner
banks), K5 send pick (reverb/delay), K6 send amount for the selected tape
track, K7 slow mode 50..100 %.

Faders: bank 1 (the panel's own bank button) is the mixer, Drums, Bass,
Chords, Melody. Bank 2: faders 1-3 are obedience for shadows A, B, C,
fader 4 is the selected send on the selected tape track.

Buttons: REC/PLAY/HOME/SAVE/SCL/ENV/FX/EDIT/ARP/LFO as on the FM-1 below.

## FM-1 (Tape)

The FM-1 has no pads: hold GLO and press a key to make the pad choices.
Counting from the lowest key (F3 at octave 0):

| GLO + key | Function |
| --- | --- |
| 1, 2, 3 | source: Chords, Melody, Drums |
| 4, 5, 6 | select shadow A, B, C |
| 9-16 | rule Hold, Arp, Third above, Sixth below, Echo, Answer, Top note, Ghost (also the Arp pattern while it is being entered) |
| 17-20 | tape track Melody, A, B, C |

Without GLO the keys play the selected tape track live, and the engine's
recorder captures them while that track is taping.

Transport on the four right-hand buttons:

| Button | Function |
| --- | --- |
| PLAY | play / stop. While taping, ends the pass (saves the take) |
| REC + PLAY | tape the selected track now (a whole pass) |
| REC held while playing | punch: recording starts at the next bar and ends at the first bar boundary after you let go. Only the punched bars keep the new notes |
| REC tap (not recording) | A/B: alternates the previous and current take of the selected track, swapped in at the next bar |
| HOME | stop everything: recording, playback, solos, sounding notes |
| HOME + REC | export the four tape tracks as SysEx (LCMD_TRACK frames) |
| SAVE | undo: step back one take |
| GLO + SAVE | redo: step forward one take |
| SCL | next tape track |
| ENV | hints on/off |
| FX | remove the selected shadow |
| EDIT | select the next shadow slot |
| LFO | play the why-hint demo for the selected shadow (source alone, then shadow alone) |
| ARP | start the first stage-3 lesson / stop the lesson |

Knobs on the FM-1: K1 octave, **K2 obedience** (GLO + K2 = density), K3
velocity, K4 patch, K5 send pick, K6 send amount, K7 slow mode.

## Takes

Every finished pass is a take: up to four per track, kept in RAM (the
oldest is dropped when a fifth arrives). A take is a copy of the track's
cells, so undo and redo just swap cells back in. Takes are logged
(`LOGK_TAKE`, score = notes in the key / notes played); a take made under
100 % slow mode is logged `slow`. Slow mode only slows the tutor timings:
the recording is played back at full tempo.

Export sends, for each of the four tracks, one SysEx frame per eight
cells: `LCMD_TRACK` with `track, frame, take` then five 7-bit bytes per
cell (note, velocity, cell word low 7, mid 7, top 2). A DAW or the
partner box can capture the whole loop.

## Lessons

Step type 7 (`reftake`): the player tapes a pass on the selected track.
`data[0]` is the minimum number of notes (default 4), `target` the
percentage that must be in the key (default 80); a slow take never passes.

## SysEx

`SHADOW id, source, rule, obedience` (a created shadow is mirrored on the
partner), `SOLO track, on`, `TRANSPORT 0 stop / 1 play / 2 rec / 3 undo`,
`TRACK` export frames, `REPLAY side` on A/B, `PATH` follow-me for the rule
pad, `SLOW percent`.

## Deviations from the spec

* The spec's pads 17/18 do not exist: the two overflow functions are on
  **pad 7** (next tape track) and **pad 8** (export) on the SMK-37, and on
  SCL / HOME+REC on both boards. Shadow slot selection moved to pads 4-6
  so that the three sources, three slots and eight rules fit in 16 pads.
* "Hold a rule pad 2 s plays source then shadow alone" uses the engine's
  solo (`groove_mix_toggle`) for two bars each; it only runs while the loop
  is playing, otherwise just the hint text shows.
* Obedience maps to the engine's step probability in three coarse steps
  (always, 6/8, 5/8, 4/8) because the engine's probability is 1/8-grained;
  the velocity jitter is deterministic per step (a hash of the step) so
  that a re-render does not change a note's velocity at random.
* The Arp pattern is a per-page setting shared by every Arp shadow, not
  stored per shadow (RAM budget).
* Echo wraps across the loop end (the last beat's echo lands in beat 1).
* A/B replay swaps the cells in at the next bar while playing, or at once
  when stopped; there is no separate "ghost" playback, the tape track
  itself plays the other take until you tap REC again or undo/redo.
* Mixer faders set the track volume absolutely from the fader value (the
  stock pickup behaviour is not reproduced).
* On the FM-1 the obedience fader is K2; density, which K2 edits on the
  SMK, is GLO + K2.
