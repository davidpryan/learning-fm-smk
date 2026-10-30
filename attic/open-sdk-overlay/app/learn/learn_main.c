/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
 * Learn layer glue: mode switch, input dispatch for both boards, the
 * lesson runner, notices and beginner sound banks. Stage modules do the
 * teaching; this file only routes events to them. */
#include "learn.h"
#include <string.h>
#ifdef LEARN_BOARD_SMK37
#include "smk37_controls.h"
#endif

static unsigned active = 1;        /* LEARN mode on by default */
static unsigned page = 1;          /* which stage page is showing */
static uint32_t combo_since;
static unsigned combo_armed;
static struct lctx ctx;
static uint32_t previous_keys, previous_buttons;
#ifdef LEARN_BOARD_SMK37
static int16_t encoder_acc[7];
#endif
static uint32_t last_frame;
static char notice_text[32];
static uint32_t notice_until;
static uint32_t key_notes[LEARN_KEYS > 32 ? 64 : 32]; /* note sounding per physical key, 0 = none */
static unsigned stock_entered;

unsigned learn_active(void) { return active; }
unsigned learn_page(void) { return page; }
void learn_set_page(unsigned p) { if (p >= 1 && p <= LEARN_STAGE) { page = p; ctx.redraw = 1; } }
unsigned learn_is_fm1(void)
{
#ifdef LEARN_BOARD_FM1
    return 1;
#else
    return 0;
#endif
}
void learn_notice(const char *text, unsigned ms)
{
    strncpy(notice_text, text, sizeof notice_text - 1); notice_text[sizeof notice_text - 1] = 0;
    notice_until = fm1_millis() + ms;
}
const char *learn_notice_text(void)
{
    if (!notice_text[0]) return NULL;
    if ((int32_t)(fm1_millis() - notice_until) >= 0) { notice_text[0] = 0; return NULL; }
    return notice_text;
}

/* ---- beginner banks ---- */
static const uint8_t bank_programs[BANK_COUNT][8] = {
    {0, 4, 8, 12, 16, 20, 24, 28},           /* PIANO 1..8 */
    {1, 5, 9, 13, 17, 21, 25, 29},           /* ORGAN 1..8 (keys) */
    {120, 112, 100, 117, 118, 113, 101, 102}, /* bells and mallets */
    {3, 7, 11, 15, 19, 23, 27, 31},          /* SYN PAD 1..8 */
    {34, 38, 42, 46, 35, 39, 43, 47},        /* BASS / SYN BASS */
    {32, 36, 40, 44, 48, 52, 56, 60}         /* GUITAR 1..8 (pluck) */
};
static const char *const bank_names[BANK_COUNT] = {"PIANO", "KEYS", "BELLS", "PAD", "BASS", "PLUCK"};
const char *learn_bank_name(unsigned bank) { return bank < BANK_COUNT ? bank_names[bank] : "?"; }
void learn_bank_select(unsigned track, unsigned bank, unsigned patch)
{
    if (bank >= BANK_COUNT || track >= GROOVE_TRACKS) return;
    if (groove_source_type(track) != GROOVE_DX7) groove_source_set(track, GROOVE_DX7);
    struct groove_view v; groove_get_view(track, 0, &v);
    int target = bank_programs[bank][patch & 7];
    if ((int)v.program != target) groove_control_edit(track, target - (int)v.program, 0, 0);
}
unsigned learn_track_channel(unsigned track) { return groove_channel(track); }

/* The partner box hears this one over USB/BLE only if live key output is
 * on. The FM-1 ships with it off (it has no DIN out), so the learn layer
 * turns it on in RAM every time it finds it off. Stored preferences are not
 * written: this is never a Flash write. */
static void ensure_live_out(void)
{
    if (groove_midi_setting(0, 4)) return;              /* LIVE_OUT already on */
    struct groove_midi_config mc; groove_midi_config_read(&mc);
    mc.flags = (uint8_t)((mc.flags & ~7U) | (fm1_midi_output_ports() & 7U) | 32U);
    groove_midi_config_apply(&mc);
}

/* ---- lesson runner ---- */
static unsigned lesson_index, lesson_on, step_index, step_reps, step_hits, step_misses;
unsigned lesson_running(void) { return lesson_on ? lesson_index + 1 : 0; }
unsigned lesson_progress(void) { return step_index; }
const struct lesson_step *lesson_step(void)
{
    if (!lesson_on || step_index >= lessons[lesson_index].step_count) return NULL;
    return &lessons[lesson_index].steps[step_index];
}
void lesson_start(unsigned index)
{
    if (index >= lesson_count) return;
    lesson_index = index; lesson_on = 1; step_index = step_reps = step_hits = step_misses = 0;
    ctx.root = lessons[index].steps[0].root; ctx.scale = lessons[index].steps[0].scale; ctx.minor = lessons[index].steps[0].minor;
    ctx.redraw = 1;
    uint8_t p = (uint8_t)index; lsyx_send(LCMD_SETLESSON, &p, 1);
    learn_notice(lessons[index].title, 1500);
}
void lesson_stop(void)
{
    if (lesson_on) { uint8_t p = 0; lsyx_send(LCMD_SETLESSON, &p, 1); }
    lesson_on = 0; ctx.redraw = 1;
}
void lesson_step_done(unsigned passed, unsigned score)
{
    const struct lesson_step *s = lesson_step();
    if (!s) return;
    if (passed) ++step_hits; else ++step_misses;
    ++step_reps;
    (void)score;
    unsigned total = s->count ? s->count : 1;
    if (step_reps < total) return;
    unsigned pct = step_hits * 100U / (step_hits + step_misses);
    unsigned ok = pct >= (s->target ? s->target : 80) && ctx.slow_percent >= 100;
    log_add(LOGK_LESSON, step_hits, step_misses, 0, ctx.slow_percent < 100, lesson_index + 1, pct);
    if (!ok) { step_reps = step_hits = step_misses = 0; learn_notice("AGAIN - ALMOST THERE", 1500); return; }
    step_reps = step_hits = step_misses = 0;
    ++step_index;
    if (step_index >= lessons[lesson_index].step_count) {
        learn_notice("LESSON DONE!", 2500);
        if (lessons[lesson_index].unlocks) log_unlock(lessons[lesson_index].unlocks);
        lesson_on = 0;
    } else learn_notice("NEXT STEP", 900);
    ctx.redraw = 1;
}

/* ---- dispatch helpers ---- */
static void dispatch_key(unsigned note, unsigned velocity)
{
    switch (page) {
#if LEARN_STAGE >= 4
    case 4: stage4_key(&ctx, note, velocity); break;
#endif
#if LEARN_STAGE >= 3
    case 3: stage3_key(&ctx, note, velocity); break;
#endif
#if LEARN_STAGE >= 2
    case 2: stage2_key(&ctx, note, velocity); break;
#endif
    default: stage1_key(&ctx, note, velocity); break;
    }
}
static void dispatch_pad(unsigned pad, unsigned velocity, unsigned pressure)
{
    switch (page) {
#if LEARN_STAGE >= 4
    case 4: stage4_pad(&ctx, pad, velocity, pressure); break;
#endif
#if LEARN_STAGE >= 3
    case 3: stage3_pad(&ctx, pad, velocity, pressure); break;
#endif
#if LEARN_STAGE >= 2
    case 2: stage2_pad(&ctx, pad, velocity, pressure); break;
#endif
    default: stage1_pad(&ctx, pad, velocity, pressure); break;
    }
}
static void dispatch_encoder(unsigned id, int delta)
{
    switch (page) {
#if LEARN_STAGE >= 4
    case 4: stage4_encoder(&ctx, id, delta); break;
#endif
#if LEARN_STAGE >= 3
    case 3: stage3_encoder(&ctx, id, delta); break;
#endif
#if LEARN_STAGE >= 2
    case 2: stage2_encoder(&ctx, id, delta); break;
#endif
    default: stage1_encoder(&ctx, id, delta); break;
    }
}
static void dispatch_sysex(unsigned cmd, const uint8_t *p, unsigned n)
{
    if (cmd == LCMD_SETLESSON && n >= 1) { if (p[0] && p[0] - 1 < lesson_count) { if (!lesson_on || lesson_index != p[0] - 1) lesson_start(p[0] - 1); } else lesson_stop(); return; }
    if (cmd == LCMD_SLOW && n >= 1 && p[0] >= 50 && p[0] <= 100) { ctx.slow_percent = p[0]; ctx.redraw = 1; return; }
    if (cmd == LCMD_KEY && n >= 2) { ctx.root = p[0] % 12; ctx.scale = p[1] % SCALE_COUNT; ctx.minor = ctx.scale == SCALE_MINOR || ctx.scale == SCALE_PENT_MIN || ctx.scale == SCALE_BLUES; ctx.redraw = 1; }
    if (cmd == LCMD_LOGDUMP && n >= 1 && p[0] == 0) { uint8_t out[LSYX_MAX - 10]; int len = log_dump(out, sizeof out); if (len > 0) lsyx_send(LCMD_LOGDUMP, out, (unsigned)len); return; }
    stage1_sysex(&ctx, cmd, p, n);
#if LEARN_STAGE >= 2
    stage2_sysex(&ctx, cmd, p, n);
#endif
#if LEARN_STAGE >= 3
    stage3_sysex(&ctx, cmd, p, n);
#endif
#if LEARN_STAGE >= 4
    stage4_sysex(&ctx, cmd, p, n);
#endif
}
static void dispatch_draw(void)
{
    switch (page) {
#if LEARN_STAGE >= 4
    case 4: stage4_draw(&ctx); break;
#endif
#if LEARN_STAGE >= 3
    case 3: stage3_draw(&ctx); break;
#endif
#if LEARN_STAGE >= 2
    case 2: stage2_draw(&ctx); break;
#endif
    default: stage1_draw(&ctx); break;
    }
}
static void dispatch_control(void)
{
    switch (page) {
#if LEARN_STAGE >= 4
    case 4: stage4_control(&ctx); break;
#endif
#if LEARN_STAGE >= 3
    case 3: stage3_control(&ctx); break;
#endif
#if LEARN_STAGE >= 2
    case 2: stage2_control(&ctx); break;
#endif
    default: stage1_control(&ctx); break;
    }
}

void learn_init(void)
{
    memset(&ctx, 0, sizeof ctx);
    ctx.slow_percent = 100; ctx.root = 0; ctx.scale = SCALE_MAJOR; ctx.minor = 0; ctx.octave = 0; ctx.redraw = 1;
    memset(key_notes, 0, sizeof key_notes);
    log_init();
    /* Make the engine a predictable beginner instrument: DX7 on the melodic
     * tracks, 808 on the drum track, normal roles, default channels. */
    for (unsigned t = 0; t < GROOVE_TRACKS; ++t) {
        if (groove_track_role(t) != GROOVE_TRACK_NORMAL) groove_track_role_set(t, GROOVE_TRACK_NORMAL);
        if (groove_midi_input_channel(t) != groove_channel(t)) groove_midi_input_set(t, groove_channel(t));
    }
    ensure_live_out();
    learn_bank_select(LT_KEYS, BANK_PIANO, 0);
    learn_bank_select(LT_CHORDS, BANK_EP, 0);
    learn_bank_select(LT_BASS, BANK_BASS, 0);
    learn_bank_select(LT_MELODY, BANK_PLUCK, 0);
    if (groove_source_type(LT_DRUMS) != GROOVE_808) groove_source_set(LT_DRUMS, GROOVE_808);
    stage1_init();
#if LEARN_STAGE >= 2
    stage2_init();
#endif
#if LEARN_STAGE >= 3
    stage3_init();
#endif
#if LEARN_STAGE >= 4
    stage4_init();
#endif
    active = 1; page = 1;
    draw_clear();
}

/* Mode-switch combo: FM-1 GLO+HOME, SMK-37 SHIFT+HOME, held two seconds. */
static void mode_combo(const struct fm1_controls *c, uint32_t now)
{
    unsigned down;
#ifdef LEARN_BOARD_SMK37
    down = smk37_shift() && (c->buttons & LB(LB_HOME));
#else
    down = (c->buttons & (LB(LB_GLO) | LB(LB_HOME))) == (LB(LB_GLO) | LB(LB_HOME));
#endif
    if (!down) { combo_since = 0; combo_armed = 0; return; }
    if (!combo_since) { combo_since = now; return; }
    if (!combo_armed && now - combo_since >= 2000U) {
        combo_armed = 1;
        active = !active;
        lmidi_all_off();
        if (active) { ctx.redraw = 1; draw_clear(); learn_notice("LEARN MODE", 1200); }
        else { stock_entered = 1; }
    }
}

#ifdef LEARN_BOARD_SMK37
static uint8_t phys_buttons[18];
int learn_smk37_event(unsigned kind, unsigned id, int value)
{
    if (kind == SMK37_BUTTON && id < 18) phys_buttons[id] = (uint8_t)(value != 0);
    if (!active) return 0;
    switch (kind) {
    case SMK37_KEY:
        if (id >= 37) return 0;
        if (value) {
            unsigned note = (unsigned)(48 + (int)id + 12 * ctx.octave);
            if (note > 127) return 1;
            key_notes[id] = note + 1;
            dispatch_key(note, (unsigned)value);
        } else if (key_notes[id]) {
            dispatch_key(key_notes[id] - 1, 0);
            key_notes[id] = 0;
        }
        return 1;
    case SMK37_PAD:
        if (id >= 16) return 0;
        dispatch_pad(id, value > 0 ? (unsigned)value : 0, 0);
        return 1;
    case SMK37_PRESSURE:
        if (id >= 16) return 0;
        dispatch_pad(id, 0xFFFF, (unsigned)value); /* pressure update */
        return 1;
    case SMK37_ENCODER:
        if (id >= 7) return 0;          /* 7 = master: stock handles it */
        {   /* Physical K1..K7 -> logical 0..6 in panel order. */
            static const uint8_t map[7] = {2, 3, 4, 5, 0, 1, 6};
            int sum = encoder_acc[map[id]] + value;
            encoder_acc[map[id]] = (int16_t)(sum > 32767 ? 32767 : sum < -32768 ? -32768 : sum);
        }
        return 1;
    case SMK37_FADER:
        if (id >= 4) return 0;          /* wheels pass through */
#if LEARN_STAGE >= 2
        switch (page) {
#if LEARN_STAGE >= 4
        case 4: stage4_fader(&ctx, id, (unsigned)value); break;
#endif
#if LEARN_STAGE >= 3
        case 3: stage3_fader(&ctx, id, (unsigned)value); break;
#endif
        case 2: stage2_fader(&ctx, id, (unsigned)value); break;
        default: break;
        }
#endif
        return 1;
    default:
        return 0;                       /* buttons, pedal: stock keeps its state */
    }
}
int learn_pad_color(unsigned pad, uint32_t *rgb)
{
    if (!active) return 0;
    switch (page) {
#if LEARN_STAGE >= 4
    case 4: *rgb = stage4_pad_colour(pad); break;
#endif
#if LEARN_STAGE >= 3
    case 3: *rgb = stage3_pad_colour(pad); break;
#endif
#if LEARN_STAGE >= 2
    case 2: *rgb = stage2_pad_colour(pad); break;
#endif
    default: *rgb = stage1_pad_colour(pad); break;
    }
    return 1;
}
#endif

int learn_control(void)
{
    struct fm1_controls c;
    fm1_controls_read(&c);
    uint32_t now = fm1_millis();
    mode_combo(&c, now);
    if (!active) {
        /* Hand the controls back untouched: the stock loop reads them again.
         * On the SMK the snapshot clears encoder deltas, so merge ours back. */
#ifdef LEARN_BOARD_SMK37
        (void)c;
#endif
        previous_keys = c.keys; previous_buttons = c.buttons;
        return 0;
    }
    stock_entered = 0;
    ensure_live_out();
    groove_master_update(c.knob);
    ctx.c = c; ctx.now = now;
    ctx.keys_down = c.keys & ~previous_keys; ctx.keys_up = previous_keys & ~c.keys;
    ctx.btn_down = c.buttons & ~previous_buttons; ctx.btn_up = previous_buttons & ~c.buttons;
    previous_keys = c.keys; previous_buttons = c.buttons;
#ifdef LEARN_BOARD_FM1
    /* FM-1 keys: bitmask edges become note events (fixed velocity). */
    for (unsigned k = 0; k < LEARN_KEYS; ++k) {
        if (ctx.keys_down & (1U << k)) {
            unsigned note = (unsigned)(LEARN_FIRST_NOTE + (int)k + 12 * ctx.octave);
            if (note <= 127) { key_notes[k] = note + 1; dispatch_key(note, 100); }
        } else if ((ctx.keys_up & (1U << k)) && key_notes[k]) { dispatch_key(key_notes[k] - 1, 0); key_notes[k] = 0; }
    }
    for (unsigned e = 0; e < 7; ++e) if (c.encoders[e]) dispatch_encoder(e, c.encoders[e]);
#else
    for (unsigned e = 0; e < 7; ++e) if (encoder_acc[e]) { dispatch_encoder(e, encoder_acc[e]); encoder_acc[e] = 0; }
#endif
    /* Octave buttons are shared by every stage. */
    if (ctx.btn_down & LB(LB_OCT_DOWN)) { if (ctx.octave > -2) --ctx.octave; ctx.redraw = 1; }
    if (ctx.btn_down & LB(LB_OCT_UP)) { if (ctx.octave < 2) ++ctx.octave; ctx.redraw = 1; }
    /* Page switch between stages: SEQ cycles pages that the build includes. */
#if LEARN_STAGE >= 2
    if (ctx.btn_down & LB(LB_SEQ)) { lmidi_all_off(); page = page % LEARN_STAGE + 1; ctx.redraw = 1; draw_clear(); }
#endif
    /* Partner messages. */
    uint8_t cmd, payload[LSYX_MAX]; unsigned bytes;
    while (lsyx_pull(&cmd, payload, &bytes)) dispatch_sysex(cmd, payload, bytes);
    lsyx_poll(now);
    dispatch_control();
    if (ctx.redraw || now - last_frame >= 40U) {
        last_frame = now;
        dispatch_draw();
        ctx.redraw = 0;
    }
    return 1;
}
