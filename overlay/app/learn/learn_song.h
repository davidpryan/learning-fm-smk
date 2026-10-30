/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
 * Shared song model for Stages 2-4. One place describes the loop (chords
 * per beat, bass rule, drum lanes, tempo); the song_write_* functions
 * render it into the stock engine's tracks, so every stage edits the same
 * thing and the engine does the playing.
 *
 * Engine mapping (see instrument.h in the vendor tree):
 *   LT_CHORDS (track 1) chord steps, one per beat          16 steps per bar
 *   LT_BASS   (track 2) derived bass notes                  "
 *   LT_MELODY (track 3) the player's recorded melody        "
 *   LT_SHADOW_A..C (4..6) Stage 3 shadows                   "
 *   LT_DRUMS  (track 7) 808 lanes, 16 steps per bar         "
 * Loop length is bars * 16 steps (16, 32 or 64). */
#ifndef LEARN_SONG_H
#define LEARN_SONG_H
#include "learn.h"

enum { SONG_MAX_BARS = 4, SONG_BEATS = SONG_MAX_BARS * 4, SONG_STEPS = SONG_MAX_BARS * 16 };
enum { LANE_KICK, LANE_SNARE, LANE_HAT, LANE_PERC, LANE_COUNT };
enum { BASS_KICK_LOCK, BASS_OFFBEAT, BASS_WALK, BASS_RULES };

struct song_chord { uint8_t degree, seventh; };   /* degree 0 = rest */
struct song_lane { uint16_t mask; uint8_t hits, shift, manual; };
struct song_model {
    struct song_chord chord[SONG_BEATS];
    unsigned bars;                 /* 1, 2 or 4 */
    unsigned root, minor;          /* key, mirrors lctx */
    unsigned tempo;                /* BPM 60..180 */
    unsigned bass_rule, bass_octave; /* octave 0..2 (36 + 12 * octave) */
    struct song_lane lane[LANE_COUNT];
    unsigned swing;                /* 50..75 % */
    unsigned fill_every;           /* 0 off, 2, 4, 8 bars */
    unsigned chord_octave;         /* MIDI note of the chord root octave, e.g. 60 */
};
extern struct song_model song;

void song_init(void);                                /* defaults: 1 bar, C major, 100 BPM, kick 4/4 */
void song_apply_key(unsigned root, unsigned minor);
void song_set_bars(unsigned bars);
void song_set_tempo(unsigned bpm);
void song_set_swing(unsigned percent);
uint16_t song_euclid(unsigned hits, unsigned steps, unsigned shift);
void song_lane_generate(unsigned lane);              /* mask from hits/shift unless manual */
void song_lane_toggle(unsigned lane, unsigned step); /* sets manual */
/* Render the model into the engine. Each call holds the engine's edit
 * transaction for one track only; call the ones whose data changed. */
void song_write_chords(void);
void song_write_bass(void);
void song_write_drums(void);
void song_write_all(void);
/* Transport and position, read from the chord track's view. */
void song_play(unsigned on);
unsigned song_playing(void);
unsigned song_step(void);                            /* 0 .. bars*16-1 */
unsigned song_bar(void);
unsigned song_beat(void);                            /* 0..3 */
int song_degree_at_step(unsigned step, unsigned *seventh); /* chord sounding at a 16th step; 0 = rest */
void song_chord_notes(unsigned degree, unsigned seventh, uint8_t notes[4], unsigned *count);
/* Melody capture through the engine's own recorder on LT_MELODY. */
void song_record(unsigned on);
unsigned song_recording(void);
void song_quantise_track(unsigned track, unsigned strength_percent); /* 0..100: moves notes toward the grid */
#endif
