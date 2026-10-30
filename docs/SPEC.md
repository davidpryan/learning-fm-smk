# Specification (player-facing design, stages 2-4 and shared rules)

This is the player-facing design the pages were written from (the code is
`learn/stage1..4.c`; the control references as built are `docs/stage1..4.md`,
which win where the two differ). Hardware note: the SMK-37 Elite has **16**
pads; where the design says pads 17/18, use buttons (REC = replay A/B,
PLAY = transport, SAVE = secondary function) and say so in `docs/stageN.md`.

## Stage 2 — Loop it: Pulse (SMK-37 Elite / Pro) + Root (FM-1)

Goal: a groove that repeats, built from the chords the player already knows.
Everything in Stage 1 still works on page 1; page 2 is the loop.

### Root (song model owner)

* **Chord loop.** REC then tap chord pads (SMK) or degree keys (FM-1,
  CHORD KEYS mode as in Stage 1) in time with the click. Root stores one
  chord per beat for 1, 2 or 4 bars (K6 sets length), quantised to the
  beat. PLAY loops it. Tapping a pad while the loop runs replaces that
  beat's chord live. Render with `song_write_chords()`.
* **Bass track.** Generated: Kick-lock, Offbeat, Walk (`song.bass_rule`,
  chosen with the three lowest white keys on the FM-1 while GLO is held, or
  K5 on either board). K5 push/alt: octave. Render with `song_write_bass()`
  whenever chords or the kick lane change.
* **Melody track.** Keys play on `LT_MELODY` while the loop runs; REC+PLAY
  arms the engine recorder (`song_record(1)`). "Snap to feel": K4 sets
  quantise strength 0-100 % and applies `song_quantise_track(LT_MELODY, strength)`
  after a pass. Undo = `groove_edit_undo()` (hold REC, press PLAY).
* **Screen.** Four lanes: Drums (16-step strip per lane), Bass, Chords
  (Roman numerals per beat), Melody (dots). A play head crosses all four
  (`song_step()`). Tempo and bar count in the header.
* **Sounds.** Chords default to KEYS bank, Bass to BASS, Melody to PLUCK
  (`learn_bank_select`). Hold EDIT and turn K1 to change the selected lane's patch.

### Pulse (drum grid)

* **Pads = 16 steps of the current lane.** SHIFT/GLO+pad or the SCL button
  cycles the lane (Kick, Snare, Hat, Perc). Tap a step to toggle it
  (`song_lane_toggle`). Steps light in the lane colour (Kick red, Snare
  yellow, Hat white, Perc purple); the play head is the brighter step.
* **Generators.** K1 Hits (1-16 Euclidean), K2 Shift (0-15), K3 Swing
  (50-75 %), K4 Fill (off/2/4/8 bars), K5 Decay (808 tone edit on the lane's
  part, `groove_tone_edit`), K6 Tempo (60-180), K7 Quantise. Turning Hits or
  Shift clears `manual` and regenerates; tapping a pad sets `manual`.
  **Swing and Fill start locked** (`log_unlocked(UNLOCK_SWING / UNLOCK_FILL)`)
  and show "LOCKED - PASS THE RHYTHM TRAINER" until earned.
* **Rhythm trainer.** ENV starts a tap-along: the click plays (metronome
  setting 1 = on), the player taps pad 1 (SMK) or any key (FM-1) on the
  beat for 4 bars, then on the pattern Hits has drawn. Show each tap as
  early/late in ms and a running accuracy. 80 % on the beat unlocks Swing;
  80 % on an offbeat pattern unlocks Fill (`log_add(LOGK_RHYTHM, hits,
  misses, tempo, slow, lesson, score)` with score bit 0 = offbeat round).
  Slow mode applies; slow rounds never unlock.
* **Mistake replay.** After a melody pass, REC alternates the quantised
  version and the raw take (keep the raw cells in RAM before quantising).
* **Why hints.** Hold a chord pad 2 s while the loop runs: name the bass
  note the rule will play under it ("IV -> BASS F, KICK-LOCK").
* **Faders (SMK).** 1-4 = Drums, Bass, Chords, Melody levels via
  `groove_control_edit(track, 0, delta, 0)`.
* **Lessons.** Step types `rhythm` (pattern in data[0..2] as a 16-bit mask,
  tempo, count = bars) and `groove` (target hits/shift per lane in data).
* **SysEx.** GRID lane,mask; CHORD bar,beat,degree,seventh,minor; REC arm;
  TAP timestamp; SCORE ms,accuracy; UNLOCK knob; LOG. The FM-1 is the clock
  master (it already sends MIDI clock when SEQ OUT is on).

Graduation (logged): under 5 minutes from blank loop to drums + bass +
chords + melody at full tempo, rhythm accuracy >= 80 %.

## Stage 3 — Grow it: Shadow (SMK-37 Elite / Pro) + Tape (FM-1)

Goal: more tracks and a real recording without more effort. A new track is
described by its relationship to a track that already exists.

### Shadow

* Up to three shadow tracks (`LT_SHADOW_A..C`; the hardware has 8 tracks and
  the drums take one). Create one from the pads in two presses: a *source*
  pad (1 Chords, 2 Melody, 3 Drums) then a *rule* pad (9-16). The shadow
  follows its source: re-render whenever chords, melody or drums change
  (hook: compare `groove_edit_revision()` each poll).

| Rule | Follows | Plays |
| --- | --- | --- |
| Hold | Chords | chord tones sustained for the beat |
| Arp | Chords | chord tones in a pattern tapped on pads 9-16 (up to 8 steps) |
| Third above | Melody | the melody a diatonic third up |
| Sixth below | Melody | the melody a diatonic sixth down |
| Echo | Melody | the melody one beat late, one octave up, velocity 60 % |
| Answer | Melody | only in bars where the melody is silent: its last four notes reversed |
| Top note | Chords | the highest chord tone as a slow line |
| Ghost | Drums | quiet hits on the off-steps of the Hat lane |

* **Obedience fader** (bank 2 on the SMK; K2 on the FM-1): strict..loose
  maps to step probability (setting 2) and velocity jitter.
* **Pad colours:** a dim version of the source's colour; hold flashes the
  source. **Why hints:** hold a rule pad 2 s: name the interval/rule and
  play source then shadow alone. First use of each rule shows the hint unasked.
* K1-K4 edit the selected shadow: Octave, Density, Velocity, Patch.
  Fader bank 1 is the mixer (Drums, Bass, Chords, Melody); aftertouch on a
  track pad is a momentary solo (`groove_mix_toggle(track, 1)` on/off).

### Tape

* Four tracks of takes in RAM (`LT_MELODY` plus the three shadows): a take
  is a copy of the track's cells (`groove_sequence_read`), up to 4 takes
  per track. REC/PLAY/STOP/Undo on the FM-1's four right-hand buttons
  (REC, PLAY, HOME = stop, SAVE = undo); mirrored over SysEx TRANSPORT.
* **Punch by bar:** hold REC while playing: the next bar boundary starts
  the punch (`song_record(1)`), releasing REC ends it on a bar; only that
  bar's cells are replaced (restore the others from the take copy).
* **Undo and takes:** every pass is a take; SAVE steps back, GLO+SAVE forward.
* **Mistake replay (A/B):** REC (not recording) alternates previous and
  current take of the selected track, bar-synced (swap the cells in).
* **Slow mode:** recording under 100 % is allowed, logged slow, played back
  at full tempo (the engine already does this: tempo is not changed, only
  the metronome/tutor timing).
* **Effects:** one send per track (reverb/delay via `groove_sends_edit`),
  K5 picks, K6 amount. **Screen:** four lanes with note-density bars, play
  head, punch region in red, take number per lane, level meters.
* **Export:** hold HOME and press REC: dump the four tracks' cells as
  SysEx (LCMD_TRACK frames, 7-bit) so a DAW can capture them.

Graduation (logged): Hold + Arp + Third-above shadows, all tracks taped,
one bar punched, A/B compared, balanced, under 10 minutes, no slow takes.

## Stage 4 — Finish it: Stage (SMK-37 Elite / Pro) + Scribe (FM-1)

Goal: a whole song performed with the hands and captured in one pass.

### Scribe

* **Sections.** The current loop (song model + takes) can be stored as a
  section in RAM with one of eight types: Intro, Verse, Pre, Chorus,
  Bridge, Break, Outro, Free. Up to 8 section slots (RAM budget). Storing =
  copying `song` plus the tracks' cells; recalling = writing them back at
  a loop boundary (`groove_sequence_rewound()` / `song_step()` wrap).
* **Autofill transitions** (K5): Drop (mute drums and bass for the last
  bar), Riser (hat doubles each half bar, reverb send climbs), Fill (Fill
  at maximum for one bar), Hand-off (melody velocity fades, incoming chords
  enter a beat early), None. Regenerated from the two sections' differences.
* **Energy** 1-8 per section: how many tracks are unmuted, added in the
  order drums, bass, chords, melody, shadows (`groove_mix_toggle(track, 0)`).
* **Song record:** REC captures the section order with bar counts and
  energy as a performance log in RAM (the audio and MIDI leave on USB as
  usual); STOP finishes, a second STOP within 2 s discards.
* **Lessons:** `song` steps carry a target order in data[] (section types
  1..8), tempo; score = order match + energy match percent.

### Stage

* **Pads 1-8 are the slots**, coloured by type (Intro grey, Verse blue, Pre
  teal, Chorus orange, Bridge purple, Break white, Outro grey, Free green).
  Playing slot pulses, queued slot blinks. Pads 9-16: store current loop
  into slot (9-16 = slot 1-8) while GLO/SHIFT is held, type cycles on SCL.
* **Queueing:** tap = next bar; double-tap = next beat; hold = play while
  held then return.
* **Follow-me:** with a lesson loaded, the next section's pad lights two
  bars early and blinks the last bar; in-time press turns green, late yellow.
* **Mistake replay:** REC after a pass alternates the ghost order and the
  performed one (screen strips, mismatches red).
* **Energy fader** (fader 1 / K1 on the FM-1) overrides energy live;
  faders 2-4 are Drums, Music, Melody masters.
* **Keys** still play live melody. K5 transition, K6 tempo nudge +-10 %,
  K7 swing.
* **Screen:** current section and bars played / length; next section and
  beats until it starts; elapsed time; a thin song bar with the play head.

Graduation (logged): five sections, performed with pads and energy, melody
over at least one, recorded in one pass of 2-3 minutes; with the shipped
lesson, match >= 80 % and no late changes.

## Shared conventions

* Colours: Green = I / playing / pass; Orange = V / Chorus / queued; Cyan =
  IV / Chords; Blue = minor / Verse; Red = kick / armed / recording;
  Yellow = snare / muted; White = hat / sus / empty; Purple = perc / bVII /
  Bridge; dim = "follows".
* MIDI channels follow the engine: track n is channel n+1 (drums track 8 =
  channel 10). The FM-1 is the clock master.
* SysEx: `F0 7D 'L' 'N' 01 <device> <stage> <cmd> payload F7`, commands in
  `learn.h`. A firmware ignores commands it does not know.
* Teaching layer in every stage: practice log, slow mode, mistake replay,
  why-hints on a 2 s hold, follow-me, lessons.
* No Flash writes in this version; sections, takes and the log are RAM and
  can be dumped over SysEx.
