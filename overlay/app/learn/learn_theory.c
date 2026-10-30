/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
 * Music theory helpers for the learn layer. Pure functions, host-testable. */
#include "learn.h"
#include <string.h>

static const char *const names[12] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
static const uint16_t masks[SCALE_COUNT] = {
    0x0AB5, /* major:  C D E F G A B  = 0,2,4,5,7,9,11 */
    0x05AD, /* minor:  C D Eb F G Ab Bb = 0,2,3,5,7,8,10 */
    0x0295, /* pentatonic major: 0,2,4,7,9 */
    0x04A9, /* pentatonic minor: 0,3,5,7,10 */
    0x04E9  /* blues: 0,3,5,6,7,10 */
};
static const char *const scale_names[SCALE_COUNT] = {"MAJOR","MINOR","PENTA MAJ","PENTA MIN","BLUES"};

const char *theory_note_name(unsigned note, char out[4])
{
    const char *n = names[note % 12];
    out[0] = n[0]; out[1] = n[1]; out[2] = 0; out[3] = 0;
    return out;
}
const char *theory_scale_name(unsigned scale) { return scale < SCALE_COUNT ? scale_names[scale] : "?"; }
unsigned theory_scale_mask(unsigned scale) { return scale < SCALE_COUNT ? masks[scale] : masks[0]; }

int theory_in_scale(unsigned root, unsigned scale, unsigned note)
{
    unsigned pc = (note + 12 - root % 12) % 12;
    return (theory_scale_mask(scale) >> pc) & 1U;
}

unsigned theory_nudge(unsigned root, unsigned scale, unsigned note)
{
    if (theory_in_scale(root, scale, note)) return note;
    /* Prefer moving down one semitone, then up. */
    if (note > 0 && theory_in_scale(root, scale, note - 1)) return note - 1;
    if (note < 127 && theory_in_scale(root, scale, note + 1)) return note + 1;
    return note > 1 ? note - 2 : note;
}

int theory_degree(unsigned root, unsigned scale, unsigned note)
{
    unsigned pc = (note + 12 - root % 12) % 12, mask = theory_scale_mask(scale), degree = 0;
    for (unsigned i = 0; i <= pc; ++i) if ((mask >> i) & 1U) ++degree;
    return ((mask >> pc) & 1U) ? (int)degree : 0;
}

/* Triad qualities per degree. Major: I ii iii IV V vi vii°. Natural minor: i ii° III iv v VI VII. */
static const uint8_t major_quality[7] = {CHORD_MAJ, CHORD_MIN, CHORD_MIN, CHORD_MAJ, CHORD_MAJ, CHORD_MIN, CHORD_DIM};
static const uint8_t minor_quality[7] = {CHORD_MIN, CHORD_DIM, CHORD_MAJ, CHORD_MIN, CHORD_MIN, CHORD_MAJ, CHORD_MAJ};
static const uint8_t major_offsets[7] = {0, 2, 4, 5, 7, 9, 11};
static const uint8_t minor_offsets[7] = {0, 2, 3, 5, 7, 8, 10};

unsigned theory_degree_chord(unsigned root, unsigned minor, unsigned degree, unsigned seventh,
                             unsigned octave_note, uint8_t notes[4])
{
    if (degree < 1 || degree > 7) return 0;
    const uint8_t *off = minor ? minor_offsets : major_offsets;
    unsigned q = (minor ? minor_quality : major_quality)[degree - 1];
    unsigned base = octave_note + (root % 12) + off[degree - 1];
    unsigned third = q == CHORD_MIN || q == CHORD_DIM ? 3 : 4;
    unsigned fifth = q == CHORD_DIM ? 6 : 7;
    notes[0] = (uint8_t)base; notes[1] = (uint8_t)(base + third); notes[2] = (uint8_t)(base + fifth);
    if (!seventh) return 3;
    /* Diatonic sevenths: major 7 on I and IV (major key), III and VI (minor); dominant on V (major) and VII (minor); minor 7 otherwise; half-dim on dim. */
    unsigned sev = 10;
    if (!minor && (degree == 1 || degree == 4)) sev = 11;
    if (minor && (degree == 3 || degree == 6)) sev = 11;
    notes[3] = (uint8_t)(base + sev);
    return 4;
}

static const char *const roman_major[7] = {"I","ii","iii","IV","V","vi","vii°"};
static const char *const roman_minor[7] = {"i","ii°","III","iv","v","VI","VII"};
static const char *const roman_major7[7] = {"Imaj7","ii7","iii7","IVmaj7","V7","vi7","viiø7"};
static const char *const roman_minor7[7] = {"i7","iiø7","IIImaj7","iv7","v7","VImaj7","VII7"};
const char *theory_roman(unsigned minor, unsigned degree, unsigned seventh)
{
    if (degree < 1 || degree > 7) return "?";
    if (seventh) return (minor ? roman_minor7 : roman_major7)[degree - 1];
    return (minor ? roman_minor : roman_major)[degree - 1];
}

unsigned theory_degree_colour(unsigned minor, unsigned degree)
{
    /* Shared colour language: I green, V orange, IV cyan, minor blue, dim red. */
    if (degree == 1) return 0;
    if (degree == 5) return 1;
    if (degree == 4) return 2;
    unsigned q = (minor ? minor_quality : major_quality)[(degree - 1) % 7];
    if (q == CHORD_DIM) return 4;
    if (q == CHORD_MAJ) return 5;
    return 3;
}

unsigned theory_identify(const uint8_t *notes, unsigned count, unsigned *root_pc)
{
    unsigned mask = 0;
    for (unsigned i = 0; i < count; ++i) mask |= 1U << (notes[i] % 12);
    if (!mask) { *root_pc = 0; return CHORD_NONE; }
    /* Templates relative to the root: triads and sevenths, any inversion. */
    static const struct { uint16_t mask; uint8_t kind; } templates[] = {
        {0x091, CHORD_MAJ}, {0x089, CHORD_MIN}, {0x049, CHORD_DIM}, {0x085, CHORD_SUS2}, {0x0A1, CHORD_SUS4},
        {0x891, CHORD_MAJ7}, {0x489, CHORD_MIN7}, {0x491, CHORD_DOM7}
    };
    unsigned lowest = notes[0] % 12;
    for (unsigned i = 1; i < count; ++i) if (notes[i] < notes[0]) lowest = notes[i] % 12;
    /* Try the bass note as root first, then every other pitch class. */
    for (unsigned pass = 0; pass < 12; ++pass) {
        unsigned r = (lowest + pass) % 12;
        unsigned rel = ((mask >> r) | (mask << (12 - r))) & 0xFFF;
        for (unsigned t = 0; t < sizeof templates / sizeof templates[0]; ++t)
            if (rel == templates[t].mask) { *root_pc = r; return templates[t].kind; }
    }
    *root_pc = lowest;
    return CHORD_NONE;
}

const char *theory_chord_suffix(unsigned kind)
{
    static const char *const s[] = {"", "m", "dim", "sus2", "sus4", "maj7", "m7", "7", ""};
    return kind <= CHORD_NONE ? s[kind] : "";
}

const char *theory_interval_name(int semitones)
{
    static const char *const up[13] = {"UNISON","MIN 2ND","MAJ 2ND","MIN 3RD","MAJ 3RD","4TH","TRITONE","5TH","MIN 6TH","MAJ 6TH","MIN 7TH","MAJ 7TH","OCTAVE"};
    int s = semitones < 0 ? -semitones : semitones;
    if (s > 12) s %= 12;
    return up[s];
}

const char *theory_why_hint(unsigned minor, unsigned degree)
{
    static const char *const major[7] = {
        "I IS HOME. EVERYTHING RESTS HERE",
        "ii LEANS TOWARD V",
        "iii IS SOFT, SHARES NOTES WITH I",
        "IV LIFTS AWAY FROM HOME",
        "V PULLS BACK TO I",
        "vi SHARES TWO NOTES WITH I",
        "vii IS TENSE, WANTS I NEXT"};
    static const char *const mnr[7] = {
        "i IS HOME, THE SAD ONE",
        "ii DIM WANTS V OR i",
        "III IS THE BRIGHT RELATIVE",
        "iv LEANS DOWN TO i",
        "v IS GENTLE, V7 IS STRONGER",
        "VI OPENS THE DOOR TO VII",
        "VII PULLS UP TO i OR III"};
    if (degree < 1 || degree > 7) return "";
    return (minor ? mnr : major)[degree - 1];
}
