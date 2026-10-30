/* SPDX-License-Identifier: GPL-3.0-only
 * Practice log. RAM only in this version (no Flash writes: see learn.h).
 * Export with the LOGDUMP SysEx command; the companion page or a DAW can
 * keep it. Unlocks are recomputed from the log on each entry. */

enum { LOG_MAX = 96 };
static struct log_entry lg_entries[LOG_MAX];
static unsigned lg_count, lg_head; /* ring; lg_head = index of the next write */
static unsigned lg_unlocks, lg_streak;

static void log_init(void) { lg_count = lg_head = lg_unlocks = lg_streak = 0; memset(lg_entries, 0, sizeof lg_entries); }

static unsigned passed(const struct log_entry *e)
{
    unsigned total = e->hits + e->misses;
    return total && e->hits * 100U >= 80U * total && !e->slow;
}

static void log_add(unsigned kind, unsigned hits, unsigned misses, unsigned tempo, unsigned slow, unsigned lesson, unsigned score)
{
    struct log_entry *e = &lg_entries[lg_head];
    e->kind = (uint8_t)kind; e->hits = (uint8_t)(hits > 255 ? 255 : hits); e->misses = (uint8_t)(misses > 255 ? 255 : misses);
    e->tempo = (uint16_t)tempo; e->slow = (uint8_t)(slow != 0); e->at_ms = fm1_ms;
    e->lesson = (uint8_t)lesson; e->score = (uint8_t)(score > 100 ? 100 : score);
    lg_head = (lg_head + 1) % LOG_MAX;
    if (lg_count < LOG_MAX) ++lg_count;
    if (passed(e)) ++lg_streak; else lg_streak = 0;
    /* Rhythm lg_unlocks (Stage 2): Swing after a pass on the beat, Fill after a pass on an offbeat pattern (score byte 1). */
    if (kind == LOGK_RHYTHM && passed(e)) {
        if (score & 1) lg_unlocks |= UNLOCK_FILL; else lg_unlocks |= UNLOCK_SWING;
    }
}
static unsigned log_count(void) { return lg_count; }
static const struct log_entry *log_get(unsigned i)
{
    if (i >= lg_count) return NULL;
    return &lg_entries[(lg_head + LOG_MAX - 1 - i) % LOG_MAX];
}
static unsigned log_streak(void) { return lg_streak; }
static unsigned log_attempts(void) { return lg_count; }
static unsigned log_unlocked(unsigned feature) { return (lg_unlocks & feature) == feature; }
static void log_unlock(unsigned feature) { lg_unlocks |= feature; }

/* 7-bit encoding: each entry is 10 bytes, all values split into 7-bit halves where needed. */
static int log_dump(uint8_t *out, unsigned capacity)
{
    unsigned n = 0;
    for (unsigned i = 0; i < lg_count && n + 10 <= capacity; ++i) {
        const struct log_entry *e = log_get(i);
        out[n++] = e->kind; out[n++] = e->hits & 127; out[n++] = e->misses & 127;
        out[n++] = e->slow; out[n++] = (uint8_t)(e->tempo & 127); out[n++] = (uint8_t)((e->tempo >> 7) & 127);
        out[n++] = e->lesson; out[n++] = e->score;
        out[n++] = (uint8_t)((e->at_ms / 1000U) & 127); out[n++] = (uint8_t)(((e->at_ms / 1000U) >> 7) & 127);
    }
    return (int)n;
}
