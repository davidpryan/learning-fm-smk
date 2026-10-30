# Page 2 — Pulse (SMK-37 Elite) / Root (FM-1)

> These firmwares build on SLOOP. **On the SMK-37 Elite** the names below are SLOOP's; the keyboard prints them differently: PLAY = SEQ PLAY, REC = SEQ REC, HOME = PATCH, GLO = GLOBE, SCL = SCALE, ENV = CHORD, LFO = NOTE REPEAT, EDIT = PARA, SAVE = BT (there is no SHIFT: GLO is the modifier on both boards), and because the SMK-37's K1 is the volume, **the knobs K1…K7 in these tables are the printed K2…K8**. On the FM-1 the names match. SEQ cycles the four pages; GLO + SEQ shows the page's help on the screen; GLO + ARP shows the results log; GLO + HOME held two seconds returns to plain SLOOP.


Page 2 (press SEQ once from page 1) is the loop: four drum lanes, a chord
per beat, a generated bass line and the engine's melody recorder. Both
boards run the same page; the song model (`learn_song.h`) is shared, so a
grid edit on one box and a chord on the other end up in the same loop over
SysEx.

Screen: header (tempo, bars, key, swing/fill, slow %), big value line, four
16-step drum strips, a BASS strip and a MELODY strip read back from the
engine, the four chord cells of the bar in view (Roman numerals), a status
line (pad/key mode, bass rule, recorder state), prompt, trainer stats, lesson
line, why-hint line and the notice bar. The play head is the brighter cell.

## SMK-37 Elite (Pulse)

| Control | Plain | With EDIT held |
|---|---|---|
| Pads (GRID mode) | 16 steps of the selected lane; tap toggles (sets *manual*). GLO + pad 1-4 selects Kick / Snare / Hat / Perc | |
| Pads (CHORDS mode) | 1-7 triads of degree I-VII, 9-15 sevenths, 8 and 16 = rest. A tap writes that beat of the loop while it runs or while REC is armed | |
| FX | Pads GRID <-> CHORDS | |
| SCL | Next lane (Kick, Snare, Hat, Perc) | |
| K1 | Hits 1-16 (Euclidean, clears *manual*) | Patch of the selected track |
| K2 | Shift 0-15 (clears *manual*) | Selected track (Drums, Bass, Chords, Melody) |
| K3 | Swing 50-75 % (**locked** until an on-beat trainer pass) | Slow mode 50-100 % |
| K4 | Fill off/2/4/8 bars (**locked** until a pattern trainer pass) | Lesson pick |
| K5 | Bass rule: Kick-lock, Offbeat, Walk | 808 Decay of the selected lane's part |
| K6 | Tempo 60-180 (steps of 2) | Loop length 1 / 2 / 4 bars |
| K7 | Snap to feel 0-100 % (re-applies to the last take) | Bass octave 1-3 |
| Faders 1-4 | Drums, Bass, Chords, Melody level | |
| PLAY | Loop start / stop (stops the trainer first) | |
| REC (tap) | Arm chord recording: click on, loop runs, chord pads write beat after beat. After a melody take in GRID mode: alternate RAW / SNAPPED replay | |
| REC + PLAY | Arm the melody recorder; the pass ends by itself when the head comes round (or press REC + PLAY again) | |
| REC + SAVE | Undo the last engine edit (`groove_edit_undo`) | |
| SAVE | Re-roll the selected lane from Hits/Shift (clears *manual*) | |
| ENV | Rhythm trainer start / stop (pad 1 is the tap pad) | |
| ARP | Lesson start / stop | |
| HOME | Stop everything: loop, trainer, recorders, held chords | |
| OCT -/+, SEQ | Octave, page (shared) | |

Pad colours: steps in the lane colour (Kick red, Snare yellow, Hat white,
Perc purple); beat starts dim white; the play head is bright. In CHORDS
mode pads use the Stage 1 function colours and the chord sounding now is
bright. During the trainer only pad 1 is lit, bright while a tap is due.

## FM-1 (Root)

| Control | Plain | With GLO held | With EDIT held |
|---|---|---|---|
| Keys (CHORDS mode) | A white key plays and records its degree's triad (chord keys, as in Stage 1) | Keys 1-16 (F3..G#4) = steps of the selected lane, tap toggles; keys 17-19 (A4, A#4, B4) = bass rule Kick-lock / Offbeat / Walk | |
| Keys (MELODY mode) | Play on the melody track (what REC + PLAY records) | same as above | |
| FX | Keys CHORDS <-> MELODY | | |
| SCL | Next lane | | |
| K1..K7 | Hits, Shift, Swing*, Fill*, Bass rule, Tempo, Snap | | Patch, Track, Slow %, Lesson, Decay, Bars, Bass octave |
| PLAY | Loop start / stop | | |
| REC (tap) | Arm chord recording (or RAW / SNAPPED replay after a take, in MELODY mode) | | |
| REC + PLAY | Melody recorder arm / finish | | |
| REC + SAVE | Undo | | |
| SAVE | Re-roll the selected lane | | |
| ENV | Rhythm trainer; any key is the tap | | |
| ARP | Lesson start / stop | | |
| HOME | Stop everything | | |

(* locked until the rhythm trainer is passed.)

## Teaching layer

* **Rhythm trainer (ENV).** Round 1: a one-bar count-in, then four bars of
  click; tap on every beat. Round 2: the Kick lane's pattern plays on a hat;
  tap the pattern. Each tap shows EARLY / LATE in ms and the stats line
  shows a running accuracy. A tap counts within ±120 ms. 80 % on round 1
  unlocks Swing, 80 % on a pattern that has off-beat hits unlocks Fill
  (`log_add(LOGK_RHYTHM, ...)`, score bit 0 = off-beat round). Rounds under
  slow mode are logged slow and never unlock.
* **Slow mode.** EDIT + K3. Scales the trainer's click and the hint timer.
* **Mistake replay.** After a melody pass the raw cells are kept in RAM;
  the take is snapped with Snap (K7). REC alternates the raw take and the
  snapped version in the engine, so the loop plays whichever is selected.
  Turning Snap re-snaps from the raw cells.
* **Why-hints.** Hold a chord pad or chord key for 2 s while the loop runs:
  "IV -> BASS F, KICK-LOCK".
* **Follow-me.** The tap pad lights when a tap is due (`LCMD_PATH`).
* **Lessons.** Step type `rhythm` (5): `data[0..2]` = 16-bit pattern (7 bits
  per byte), `tempo`, `count` = bars; the step runs as one pattern round
  (PLAY or ARP starts it). Type `groove` (6): `data[2*lane]` = hits,
  `data[2*lane+1]` = shift per lane; the step passes when every listed lane's
  mask matches E(hits, 16, shift). Steps of Stage 1 types prompt the player
  back to page 1.
* **Graduation.** Logged as `LOGK_SONG` when, within five minutes of entering
  the page, there is a melody take at full tempo, chords in the loop, a kick
  lane and a passed rhythm round.

## SysEx (both directions)

GRID lane, mask (3 x 7-bit) · CHORD bar, beat, degree, seventh, minor ·
REC arm · TAP timestamp (3 x 7-bit ms) · SCORE error+64, accuracy · UNLOCK
knob (2 = swing, 3 = fill) · LOG streak, unlocked level, attempts ·
TRANSPORT 0 stop / 1 play / 2 rec / 3 undo · HINT degree, seventh, minor ·
REPLAY which · PATH follow-me · SLOW percent.

## Deviations from the spec

* **16 pads, not 18.** The spec's pad 17/18 functions sit on buttons: REC
  = replay A/B, PLAY = transport, SAVE = lane re-roll (REC + SAVE = undo).
* **One pad set, two modes.** The SMK pads are either the 16-step grid or
  the chord palette; FX switches. Lane select is GLO + pad 1-4 or SCL.
* **Second knob row on EDIT.** Spec knobs K1-K7 are Hits, Shift, Swing,
  Fill, Decay, Tempo, Quantise, but Root also needs bass rule, bars, slow,
  lesson and patch. Plain K5 is the bass rule and Decay moved to EDIT + K5;
  EDIT + K1 is the patch of the selected track (EDIT + K2 selects it).
* **Undo is REC + SAVE** (not REC + PLAY, which arms the recorder).
* **Melody recorder length.** A pass ends when the head comes round to the
  step it started on; the raw take is read back with `groove_sequence_read`
  before `song_quantise_track` runs, so RAW / SNAPPED replay swaps whole
  cell arrays in the engine.
* **Snap below 50 %** keeps the take as played (`song_quantise_track` does
  nothing under 50; the model's behaviour, not changed here).
* **Trainer windows.** ±120 ms counts as a hit, |error| <= 15 ms shows
  ON TIME. The on-beat round is scored against beats of the count-in tempo;
  a lesson `rhythm` step runs only the pattern round.
* **Chord taps while stopped** write at a cursor that advances one beat per
  tap (REC armed); while the loop runs they replace the beat under the head
  (the next beat when the tap is late in the beat).
* **Sound defaults.** The engine is set up by `learn_main.c` (KEYS/BASS/
  PLUCK banks); the page does not re-select banks on entry, so Stage 1's
  bank choice survives a page switch.
* **The partner's SCORE** is shown on the big line only when this box is
  idle on page 2.
* **MIDI Start / Stop** go through `song_play()`; whether they leave the
  box over USB depends on the stock SEQ OUT preference, which the learn
  layer does not change (no Flash writes).
