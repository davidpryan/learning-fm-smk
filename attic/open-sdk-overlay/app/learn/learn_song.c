/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
 * Shared song model: renders the beginner's loop into the stock engine. */
#include "learn_song.h"
#include <string.h>

struct song_model song;
static const uint8_t lane_drum[LANE_COUNT] = {0, 1, 10, 6}; /* kick, snare, closed hat, clap */

uint16_t song_euclid(unsigned hits, unsigned steps, unsigned shift)
{
    if (!steps || steps > 16) return 0;
    if (hits > steps) hits = steps;
    uint16_t mask = 0;
    /* Bresenham-style spread: hit when the accumulated error crosses a step. */
    unsigned bucket = 0;
    for (unsigned i = 0; i < steps; ++i) {
        bucket += hits;
        if (bucket >= steps) { bucket -= steps; mask |= 1U << i; }
    }
    /* Rotate so the first hit lands on step 0, then apply the user's shift. */
    if (mask) while (!(mask & 1U)) mask = (uint16_t)((mask >> 1) | ((mask & 1U) << (steps - 1)));
    for (unsigned i = 0; i < shift % steps; ++i) mask = (uint16_t)(((mask << 1) | (mask >> (steps - 1))) & ((1U << steps) - 1));
    return mask;
}

void song_init(void)
{
    memset(&song, 0, sizeof song);
    song.bars = 1; song.tempo = 100; song.root = 0; song.minor = 0; song.chord_octave = 60;
    song.bass_rule = BASS_KICK_LOCK; song.bass_octave = 0; song.swing = 50; song.fill_every = 0;
    song.lane[LANE_KICK].hits = 4; song.lane[LANE_SNARE].hits = 2; song.lane[LANE_SNARE].shift = 4; song.lane[LANE_HAT].hits = 8;
    for (unsigned l = 0; l < LANE_COUNT; ++l) song_lane_generate(l);
    /* A gentle default progression so Pulse has something to lock to: I - V - vi - IV over one bar. */
    static const uint8_t prog[4] = {1, 5, 6, 4};
    for (unsigned b = 0; b < SONG_BEATS; ++b) { song.chord[b].degree = prog[b % 4]; song.chord[b].seventh = 0; }
}
void song_apply_key(unsigned root, unsigned minor) { song.root = root % 12; song.minor = minor; }
void song_set_bars(unsigned bars) { song.bars = bars == 2 ? 2 : bars == 4 ? 4 : 1; }
void song_set_tempo(unsigned bpm)
{
    if (bpm < 60) bpm = 60; if (bpm > 180) bpm = 180;
    song.tempo = bpm;
    int delta = (int)bpm - (int)groove_tempo_saved();
    if (delta) groove_tempo_edit(delta);
}
void song_set_swing(unsigned percent)
{
    if (percent < 50) percent = 50; if (percent > 75) percent = 75;
    song.swing = percent;
    int delta = (int)(percent - 50) - (int)groove_sequence_setting(0);
    if (delta) groove_sequence_setting_edit(0, delta);
}
void song_lane_generate(unsigned lane)
{
    if (lane >= LANE_COUNT) return;
    if (song.lane[lane].manual) return;
    song.lane[lane].mask = song_euclid(song.lane[lane].hits, 16, song.lane[lane].shift);
}
void song_lane_toggle(unsigned lane, unsigned step)
{
    if (lane >= LANE_COUNT || step >= 16) return;
    song.lane[lane].mask ^= (uint16_t)(1U << step);
    song.lane[lane].manual = 1;
}

void song_chord_notes(unsigned degree, unsigned seventh, uint8_t notes[4], unsigned *count)
{
    *count = degree ? theory_degree_chord(song.root, song.minor, degree, seventh, song.chord_octave, notes) : 0;
}
int song_degree_at_step(unsigned step, unsigned *seventh)
{
    unsigned beat = (step / 4) % (song.bars * 4);
    if (seventh) *seventh = song.chord[beat].seventh;
    return song.chord[beat].degree;
}

static void clear_track(unsigned track)
{
    groove_edit_clear(track, 0, 2);
}
static void set_step_setting(unsigned track, unsigned step, unsigned setting, unsigned value)
{
    int delta = (int)value - (int)groove_step_setting(track, step, setting);
    if (delta) groove_step_setting_edit(track, step, setting, delta);
}

void song_write_chords(void)
{
    unsigned t = LT_CHORDS, steps = song.bars * 16;
    if (groove_source_type(t) != GROOVE_DX7) groove_source_set(t, GROOVE_DX7);
    groove_sequence_length_set(t, steps);
    clear_track(t);
    for (unsigned beat = 0; beat < song.bars * 4; ++beat) {
        uint8_t notes[4]; unsigned n;
        song_chord_notes(song.chord[beat].degree, song.chord[beat].seventh, notes, &n);
        if (!n) continue;
        unsigned s = beat * 4;
        groove_sequence_chord(t, s, notes, n, 96);
        set_step_setting(t, s, 1, 15);                     /* full gate */
        for (unsigned k = 1; k < 4; ++k) {                 /* hold through the beat with ties */
            groove_sequence_chord(t, s + k, notes, n, 96);
            set_step_setting(t, s + k, 1, 15);
            set_step_setting(t, s + k, 4, 1);
        }
    }
}

void song_write_bass(void)
{
    unsigned t = LT_BASS, steps = song.bars * 16;
    if (groove_source_type(t) != GROOVE_DX7) groove_source_set(t, GROOVE_DX7);
    groove_sequence_length_set(t, steps);
    clear_track(t);
    uint16_t kick = song.lane[LANE_KICK].mask;
    for (unsigned s = 0; s < steps; ++s) {
        unsigned seventh; int degree = song_degree_at_step(s, &seventh);
        if (!degree) continue;
        uint8_t notes[4]; unsigned n = theory_degree_chord(song.root, song.minor, (unsigned)degree, 0, 36 + 12 * song.bass_octave, notes);
        if (!n) continue;
        unsigned in_bar = s % 16, play = 0, note = notes[0];
        switch (song.bass_rule) {
        case BASS_KICK_LOCK: play = (kick >> in_bar) & 1U; break;
        case BASS_OFFBEAT:   play = ((kick >> ((in_bar + 14) % 16)) & 1U); break; /* two 16ths after each kick */
        default:             play = (in_bar % 4) == 0 || (in_bar % 4) == 2;
                             if ((in_bar % 4) == 2) note = notes[2]; /* fifth */
                             if ((in_bar / 4) % 2 == 1 && (in_bar % 4) == 0) note = (uint8_t)(notes[0] + 12);
                             break;
        }
        if (play) { groove_sequence_edit(t, s, note, 100); set_step_setting(t, s, 1, song.bass_rule == BASS_WALK ? 12 : 8); }
    }
}

void song_write_drums(void)
{
    unsigned t = LT_DRUMS, steps = song.bars * 16;
    if (groove_source_type(t) != GROOVE_808) groove_source_set(t, GROOVE_808);
    groove_sequence_length_set(t, steps);
    clear_track(t);
    for (unsigned l = 0; l < LANE_COUNT; ++l) {
        unsigned note = groove_drum_note(lane_drum[l]);
        for (unsigned s = 0; s < steps; ++s) {
            unsigned bar = s / 16, in_bar = s % 16, on = (song.lane[l].mask >> in_bar) & 1U;
            /* Fill: on the last bar of each fill period, the hat and snare double up in the last beat. */
            if (song.fill_every && ((bar + 1) % song.fill_every) == 0 && in_bar >= 12 && (l == LANE_HAT || l == LANE_SNARE)) on = 1;
            if (on) groove_sequence_edit(t, s, note, l == LANE_HAT ? 80 : 110);
        }
    }
}
void song_write_all(void) { song_write_drums(); song_write_chords(); song_write_bass(); }

void song_play(unsigned on)
{
    const uint8_t m = on ? 0xFA : 0xFC;
    (void)groove_midi_local(&m, 1);
}
unsigned song_playing(void) { struct groove_view v; groove_get_view(LT_CHORDS, 0, &v); return v.playing; }
unsigned song_step(void) { struct groove_view v; groove_get_view(LT_CHORDS, 0, &v); return v.play_step; }
unsigned song_bar(void) { return song_step() / 16; }
unsigned song_beat(void) { return (song_step() / 4) % 4; }

void song_record(unsigned on) { groove_edit_record(on, LT_MELODY, 0); }
unsigned song_recording(void) { return groove_edit_recording(); }

/* Snap-to-feel: with strength 100 every note sits on its step; the engine
 * already stores notes per 16th, so "quantise" here means moving a note
 * from an odd 16th toward the nearest 8th when strength is high enough.
 * Strength below 50 keeps the recorded placement. */
void song_quantise_track(unsigned track, unsigned strength_percent)
{
    if (strength_percent < 50 || track >= GROOVE_TRACKS) return;
    unsigned steps = groove_sequence_length(track);
    struct groove_step cells[SONG_STEPS];
    if (steps > SONG_STEPS) steps = SONG_STEPS;
    groove_sequence_read(track, 0, cells, steps);
    for (unsigned s = 0; s < steps; ++s) {
        if (!cells[s].velocity || (s % 2) == 0) continue;
        unsigned target = (s % 4 == 1) ? s - 1 : (s + 1 < steps ? s + 1 : s - 1);
        if (strength_percent < 75 && (s % 4) == 3) continue; /* medium: only pull late notes */
        if (!cells[target].velocity) { cells[target] = cells[s]; cells[s].velocity = 0; cells[s].note = 0; }
    }
    groove_sequence_replace(track, 0, cells, steps);
}
