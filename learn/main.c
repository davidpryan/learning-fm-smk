/* SPDX-License-Identifier: GPL-3.0-only
 * LEARN mode: the switch, the input dispatch, the screen, the lights, notices and the lesson runner.
 * Called from SLOOP's ui_input.c / ui_draw.c on the main loop (see LEARN-PATCH.md). */

static struct lctx lx;                            /* the stage context, kept between polls */
static uint8_t learn_inited, lpage = 1;
static uint32_t lsw_t0;                           /* GLO + HOME held since (ms | 1), bit 1 = fired */
static uint32_t ld_last_ms;
static uint8_t ld_dirty = 1;
static char lnotice[32];
static uint32_t lnotice_until;
#if BOARD_SMK37
static uint8_t lfader_last[4] = {0xFF, 0xFF, 0xFF, 0xFF};
#endif
static unsigned les_idx;                          /* 1 + lesson index, 0 none */
static unsigned les_step;
#define LEARN_HOLD_MS 1500u

/* ---- notices ---- */
static void learn_notice(const char *text, unsigned ms)
{
    str_cpy(lnotice, text, sizeof lnotice);
    lnotice_until = fm1_ms + ms;
    ld_dirty = 1;
}
static const char *learn_notice_text(void) { return lnotice[0] && (int32_t)(fm1_ms - lnotice_until) < 0 ? lnotice : 0; }

/* ---- pages ---- */
static unsigned learn_page(void) { return lpage; }
static uint8_t lpage_inited;                      /* bit per page: set up once per LEARN session (a page keeps its work) */
static void stage_init_page(unsigned p)
{
    if (lpage_inited & (1u << p))
        return;
    lpage_inited |= (uint8_t)(1u << p);
    switch (p) {
    default: stage1_init(); break;
#if LEARN_STAGE >= 2
    case 2: stage2_init(); break;
#endif
#if LEARN_STAGE >= 3
    case 3: stage3_init(); break;
#endif
#if LEARN_STAGE >= 4
    case 4: stage4_init(); break;
#endif
    }
}
static void learn_set_page(unsigned page)
{
    if (page < 1u || page > LEARN_STAGE)
        page = 1;
    lmidi_all_off();
    lpage = (uint8_t)page;
    stage_init_page(page);
    {
        static const char *const NM[4] = {LEARN_PADS ? "PAGE 1 LANTERN: HEAR IT" : "PAGE 1 EMBER: HEAR IT",
                                          LEARN_PADS ? "PAGE 2 PULSE: LOOP IT" : "PAGE 2 ROOT: LOOP IT",
                                          LEARN_PADS ? "PAGE 3 SHADOW: GROW IT" : "PAGE 3 TAPE: GROW IT",
                                          LEARN_PADS ? "PAGE 4 STAGE: FINISH IT" : "PAGE 4 SCRIBE: FINISH IT"};
        learn_notice(NM[(page - 1u) % 4u], 1500);
    }
    lx.redraw = 1;
    ld_dirty = 1;
}

/* ---- lesson runner ---- */
static unsigned lesson_running(void) { return les_idx; }
static unsigned lesson_progress(void) { return les_step; }
static const struct lesson_step *lesson_step(void)
{
    if (!les_idx)
        return 0;
    {
        const struct lesson *l = &lessons[(les_idx - 1u) % lesson_count];
        return les_step < l->step_count ? &l->steps[les_step] : 0;
    }
}
static void lesson_stop(void) { les_idx = 0; les_step = 0; ld_dirty = 1; }
static void lesson_start(unsigned index)
{
    const struct lesson *l;
    if (index >= lesson_count)
        return;
    l = &lessons[index];
    les_idx = index + 1u;
    les_step = 0;
    if (l->stage > lpage && l->stage <= LEARN_STAGE)
        learn_set_page(l->stage);
    {
        const struct lesson_step *s = lesson_step();
        if (s) {                                  /* the lesson's key */
            lx.root = s->root; lx.scale = s->scale; lx.minor = s->minor;
        }
    }
    learn_notice(l->title, 1500);
    {
        uint8_t p = (uint8_t)(index + 1u);
        lsyx_send(LCMD_LESSON, &p, 1);
    }
}
static void lesson_step_done(unsigned passed, unsigned score)
{
    const struct lesson *l;
    if (!les_idx)
        return;
    l = &lessons[(les_idx - 1u) % lesson_count];
    (void)passed;
    les_step++;
    if (les_step >= l->step_count) {
        log_add(LOGK_LESSON, score / 10u, 10u - (score / 10u > 10u ? 10u : score / 10u), 0, lx.slow_percent < 100u,
                les_idx, score);
        if (l->unlocks)
            log_unlock(l->unlocks);
        learn_notice(l->unlocks & UNLOCK_STAGE2 && lpage == 1u && LEARN_STAGE > 1 ? "LESSON DONE! SEQ: PAGE 2" : "LESSON DONE!", 2500);
        les_idx = 0;
        les_step = 0;
    } else {
        const struct lesson_step *s = lesson_step();
        if (s) { lx.root = s->root; lx.scale = s->scale; lx.minor = s->minor; }
    }
    ld_dirty = 1;
}

static unsigned learn_bpm(void) { return (unsigned)song.g[G_BPM]; }
static void learn_set_bpm(unsigned bpm)
{
    if (bpm < 40u) bpm = 40u;
    if (bpm > 240u) bpm = 240u;
    song.g[G_BPM] = (int16_t)bpm;
}

/* ---- the mode ---- */
static void learn_set(int on)
{
    uint32_t t;
    if (on) {
        if (song.playing)
            transport_req = 2;                    /* the sequencer stops: the controls are the layer's now */
        song.rec = 0;
        layer_unlock();
#if BOARD_SMK37
        pick_close();
        fb_close();
#endif
        vis_on = 0;
        if (ui.menu)
            menu_close();
        ui.hold_kind = 0;
        if (!learn_inited) {
            log_init();
            memset(&lx, 0, sizeof lx);
            lx.slow_percent = 100;
            lx.root = 0; lx.scale = SCALE_MAJOR; lx.minor = 0; lx.octave = 0;
            learn_inited = 1;
        }
        for (t = 0; t < NPART; t++) {             /* plain notes on every synth track */
            trk[t].p[P_AMODE] = 0;
            trk[t].p[P_AHOLD] = 0;
        }
        lpage = 1;
        lpage_inited = 0;
        lov_log = lov_help = 0;
        lov_welcome = 1;
        lresult.valid = 0;
        stage_init_page(1);
        learn_notice(LBL_GLO "+SEQ HELP, " LBL_GLO "+ARP LOG", 3000);
        lx.redraw = 1;
        ld_dirty = 1;
        learn_on = 1;
    } else {
        lmidi_all_off();
        lesson_stop();
        learn_on = 0;
        lnotice[0] = 0;
    }
    ui.force = 1;
}

/* GLO + HOME held LEARN_HOLD_MS: the mode, both ways. Their edges are eaten while both are down. */
static void learn_switch(uint32_t *pressed)
{
    uint32_t g = 1u << panel.btn[B_GLO], h = 1u << panel.btn[B_HOME], both = g | h;
    if ((fm1_in.buttons & both) != both) {
        if (!(fm1_in.buttons & both))
            lsw_t0 = 0;
        return;
    }
    *pressed &= ~both;
    if (!lsw_t0)
        lsw_t0 = (fm1_ms | 1u) & ~2u;
    else if (!(lsw_t0 & 2u) && fm1_ms - (lsw_t0 & ~3u) >= LEARN_HOLD_MS) {
        lsw_t0 |= 2u;
        learn_set(!learn_on);
    }
}

/* ---- dispatch to the page ---- */
static void page_control(struct lctx *x)
{
    switch (lpage) {
    default: stage1_control(x); break;
#if LEARN_STAGE >= 2
    case 2: stage2_control(x); break;
#endif
#if LEARN_STAGE >= 3
    case 3: stage3_control(x); break;
#endif
#if LEARN_STAGE >= 4
    case 4: stage4_control(x); break;
#endif
    }
}
static void page_key(struct lctx *x, unsigned note, unsigned vel)
{
    switch (lpage) {
    default: stage1_key(x, note, vel); break;
#if LEARN_STAGE >= 2
    case 2: stage2_key(x, note, vel); break;
#endif
#if LEARN_STAGE >= 3
    case 3: stage3_key(x, note, vel); break;
#endif
#if LEARN_STAGE >= 4
    case 4: stage4_key(x, note, vel); break;
#endif
    }
}
static void page_pad(struct lctx *x, unsigned pad, unsigned vel)
{
    switch (lpage) {
    default: stage1_pad(x, pad, vel, 0); break;
#if LEARN_STAGE >= 2
    case 2: stage2_pad(x, pad, vel, 0); break;
#endif
#if LEARN_STAGE >= 3
    case 3: stage3_pad(x, pad, vel, 0); break;
#endif
#if LEARN_STAGE >= 4
    case 4: stage4_pad(x, pad, vel, 0); break;
#endif
    }
}
static void page_encoder(struct lctx *x, unsigned id, int d)
{
    switch (lpage) {
    default: stage1_encoder(x, id, d); break;
#if LEARN_STAGE >= 2
    case 2: stage2_encoder(x, id, d); break;
#endif
#if LEARN_STAGE >= 3
    case 3: stage3_encoder(x, id, d); break;
#endif
#if LEARN_STAGE >= 4
    case 4: stage4_encoder(x, id, d); break;
#endif
    }
}
static void page_fader(struct lctx *x, unsigned id, unsigned v)
{
    switch (lpage) {
    default: stage1_fader(x, id, v); break;
#if LEARN_STAGE >= 2
    case 2: stage2_fader(x, id, v); break;
#endif
#if LEARN_STAGE >= 3
    case 3: stage3_fader(x, id, v); break;
#endif
#if LEARN_STAGE >= 4
    case 4: stage4_fader(x, id, v); break;
#endif
    }
}
static void page_sysex(struct lctx *x, unsigned cmd, const uint8_t *p, unsigned n)
{
    stage1_sysex(x, cmd, p, n);                   /* every page sees every command (each ignores what is not its own) */
#if LEARN_STAGE >= 2
    stage2_sysex(x, cmd, p, n);
#endif
#if LEARN_STAGE >= 3
    stage3_sysex(x, cmd, p, n);
#endif
#if LEARN_STAGE >= 4
    stage4_sysex(x, cmd, p, n);
#endif
}
static void page_draw(struct lctx *x)
{
    switch (lpage) {
    default: stage1_draw(x); break;
#if LEARN_STAGE >= 2
    case 2: stage2_draw(x); break;
#endif
#if LEARN_STAGE >= 3
    case 3: stage3_draw(x); break;
#endif
#if LEARN_STAGE >= 4
    case 4: stage4_draw(x); break;
#endif
    }
}
static uint32_t page_pad_colour(unsigned pad)
{
    switch (lpage) {
    default: return stage1_pad_colour(pad);
#if LEARN_STAGE >= 2
    case 2: return stage2_pad_colour(pad);
#endif
#if LEARN_STAGE >= 3
    case 3: return stage3_pad_colour(pad);
#endif
#if LEARN_STAGE >= 4
    case 4: return stage4_pad_colour(pad);
#endif
    }
}

/* ---- the poll (ui_input.c, main loop) ---- */
static void learn_input(uint32_t pressed, uint32_t now_ticks)
{
    uint32_t l, now = fm1_ms;
    uint8_t kind, a, b;
    (void)now_ticks;
    lx.now = now;
    {
        uint32_t prev = lx.buttons;
        lx.buttons = 0;
        lx.btn_down = 0;
        lx.keys_down = 0;
        for (l = 0; l < NB; l++) {
            uint32_t bit = 1u << panel.btn[l];
            if (fm1_in.buttons & bit)
                lx.buttons |= 1u << l;
            if (pressed & bit)
                lx.btn_down |= 1u << l;
        }
        lx.btn_up = prev & ~lx.buttons;
    }
    if (pressed)
        ui_input_ms = now;
    /* the layer's own buttons: OCT -/+ the octave, SEQ the next page */
    if (lx.btn_down & LB(LB_OCT_DOWN)) { if (lx.octave > -2) lx.octave--; lx.redraw = 1; }
    if (lx.btn_down & LB(LB_OCT_UP)) { if (lx.octave < 2) lx.octave++; lx.redraw = 1; }
    if (lov_welcome && lx.btn_down) { lov_welcome = 0; lx.btn_down = 0; ld_dirty = 1; }   /* the welcome: any button */
    if ((lx.btn_down & LB(LB_SEQ)) && ((lx.buttons & LB(LB_GLO)) || lov_help)) {   /* GLO + SEQ: the help; SEQ pages it */
        lov_help = (uint8_t)((lov_help + 1u) % (HELP_SCREENS + 1u)); lov_log = 0; lx.btn_down &= ~LB(LB_SEQ); ld_dirty = 1;
    }
    if ((lx.btn_down & LB(LB_ARP)) && (lx.buttons & LB(LB_GLO))) {   /* GLO + ARP: the results log */
        lov_log = !lov_log; lov_help = 0; lx.btn_down &= ~LB(LB_ARP); ld_dirty = 1;
    }
    if ((lov_log || lov_help) && (lx.btn_down & ~(LB(LB_GLO) | LB(LB_SEQ) | LB(LB_ARP)))) {   /* any other button closes an overlay */
        lov_log = lov_help = 0; ld_dirty = 1;
    }
#if LEARN_STAGE >= 2
    if (lx.btn_down & LB(LB_SEQ)) {
        learn_set_page(lpage % LEARN_STAGE + 1u);
        lx.btn_down &= ~LB(LB_SEQ);
    }
#endif
    /* the knobs: roles 0..6 (SELECT, ALGO, PRESET, K1..K4 on the FM-1; the SMK's KNOB LAYOUT maps its eight) */
    for (l = 0; l < NE; l++) {
        int32_t d = panel_enc(l);
        if (d)
            page_encoder(&lx, l, d > 0 ? 1 : -1);
    }
#if BOARD_SMK37
    for (l = 0; l < 4u; l++) {
        uint8_t v = fm1_in.fader[l];
        if (lfader_last[l] == 0xFFu) {
            lfader_last[l] = v;                   /* (the first read: where it is, not a move) */
        } else if (v != lfader_last[l]) {
            lfader_last[l] = v;
            page_fader(&lx, l, v);
        }
    }
#endif
    /* what the player did since the last poll */
    while (lev_pull(&kind, &a, &b)) {
        if (lov_welcome && b) { lov_welcome = 0; ld_dirty = 1; }   /* (a key begins too) */
        if (kind == LEV_KEY) {
            int n = (int)a + 12 * lx.octave;
            if (n < 0) n = 0;
            if (n > 127) n = 127;
            if (b)
                lx.keys_down = 1;
            page_key(&lx, (unsigned)n, b);
        } else if (kind == LEV_PAD) {
            if (b)
                lx.keys_down = 1;
            page_pad(&lx, a, b);
        } else {
            lext_push(a, b);
            lx.redraw = 1;
        }
    }
    /* the partner box */
    {
        uint8_t cmd; const uint8_t *p; unsigned n;
        while (lsyx_pull(&cmd, &p, &n)) {
            if (cmd == LCMD_SETLESSON && n >= 1) {
                if (p[0]) lesson_start(p[0] - 1u); else lesson_stop();
            } else if (cmd == LCMD_LESSON && n >= 1 && p[0] && !les_idx) {
                lesson_start(p[0] - 1u);
            } else if (cmd == LCMD_SLOW && n >= 1) {
                lx.slow_percent = p[0] < 50u ? 50u : p[0] > 100u ? 100u : p[0];
            } else if (cmd == LCMD_LOGDUMP) {
                uint8_t out[LSYX_MAX];
                int k = log_dump(out, sizeof out);
                if (k >= 0) lsyx_send(LCMD_LOGDUMP, out, (unsigned)k);
            } else if (cmd == LCMD_KEY && n >= 2) {
                lx.root = p[0] % 12u; lx.scale = p[1] % SCALE_COUNT;
                lx.minor = lx.scale == SCALE_MINOR || lx.scale == SCALE_PENT_MIN || lx.scale == SCALE_BLUES;
            }
            page_sysex(&lx, cmd, p, n);
            lx.redraw = 1;
        }
    }
    page_control(&lx);
    lsyx_poll(now);
    if (lx.redraw) {
        lx.redraw = 0;
        ld_dirty = 1;
    }
    if (lnotice[0] && (int32_t)(now - lnotice_until) >= 0) {
        lnotice[0] = 0;
        ld_dirty = 1;
    }
}

/* ---- the overlays: the results log and the page's help ---- */
static void overlay_log(void)
{
    static const char *const KIND[7] = {"ECHO", "CHORD", "PROG", "RHYTHM", "TAKE", "SONG", "LESSON"};
    char line[40];
    unsigned n, i, count = log_count();
    n = ls_cat(line, 0, sizeof line, "RESULTS  STREAK "); n = ls_num(line, n, sizeof line, log_streak());
    n = ls_cat(line, n, sizeof line, "  TODAY "); ls_num(line, n, sizeof line, log_attempts());
    ld_bar_text(0, line, LC_PANEL, LC_TEXT);
    if (!count) {
        ld_text(4, 40, "NOTHING YET.", 0, LC_DIM);
        ld_text(4, 58, "A DRILL OR A LESSON WRITES", 0, LC_DIM);
        ld_text(4, 76, "ITS SCORE HERE.", 0, LC_DIM);
    }
    for (i = 0; i < count && i < 10u; i++) {      /* newest first */
        const struct log_entry *e = log_get(i);
        unsigned tries;
        if (!e) break;
        tries = e->hits + e->misses;
        n = ls_cat(line, 0, sizeof line, KIND[e->kind % 7u]);
        while (n < 7u) line[n++] = ' ';
        line[n] = 0;
        if (e->lesson) { n = ls_cat(line, n, sizeof line, "L"); n = ls_num(line, n, sizeof line, e->lesson); if (e->lesson < 10u) n = ls_cat(line, n, sizeof line, " "); n = ls_cat(line, n, sizeof line, " "); }
        else n = ls_cat(line, n, sizeof line, "    ");
        if (tries && e->kind <= LOGK_RHYTHM) { n = ls_num(line, n, sizeof line, e->hits); n = ls_cat(line, n, sizeof line, "/"); n = ls_num(line, n, sizeof line, tries); n = ls_cat(line, n, sizeof line, " "); }
        n = ls_num(line, n, sizeof line, e->score); n = ls_cat(line, n, sizeof line, "%");
        if (e->slow) n = ls_cat(line, n, sizeof line, " SLOW");
        ld_text(4, 22 + (int)i * 18, line, 0, e->score >= 80u ? LC_GREEN : LC_AMBER);
    }
    ld_text(4, 222, LBL_GLO "+ARP CLOSES", 0, LC_DIM);
}
/* the help: three screens a page (what it is for and how to learn here; the buttons; the knobs, faders, pads), in the
 * names printed on the panel in the player's hands. Each line fits the screen (29 characters). */
#if BOARD_SMK37
#define HK(n) "K" #n
#define HLINES_COMMON "GLOBE+SEQ: NEXT HELP SCREEN", "GLOBE+ARP: YOUR RESULTS", "GLOBE+PATCH 2S: PLAIN SLOOP"
static const char *const HELP1[3][12] = {
    {"PAGE 1 LANTERN: HEAR IT", "WHAT IT IS FOR:", "PLAY AND READ THE SCREEN.", "IT NAMES EVERY NOTE AND CHORD", "AND SAYS WHAT IT DOES IN THE", "KEY. WRONG NOTES ARE NUDGED.", "HOW TO LEARN HERE:", "ARP STARTS LESSON 1. PASS IT,", "THEN THE NEXT. 20 LESSONS.", "DRILLS: COPY WHAT YOU HEAR.", HLINES_COMMON},
    {"PAGE 1 BUTTONS", "KEYS: PLAY A NOTE", "PADS 1-7: CHORDS I-VII", "PADS 9-15: THE SAME + 7TH", "SEQ PLAY: START/STOP A DRILL", "SEQ REC: REPLAY REF, THEN YOU", "ARP: LESSON ON / OFF", "SCALE: NEXT SCALE TYPE", "GLOBE+KEY: THAT NOTE IS ROOT", "CHORD: HINTS+NUDGE ON/OFF", "PATCH: STOP EVERYTHING", "SEQ: NEXT PAGE", 0},
    {"PAGE 1 KNOBS", "K1: VOLUME (ALWAYS)", "K2: SOUND ENGINE", "K3: PRESET", "K4: REVERB  K5: DELAY", "K6: SLOW MODE 50-100%", "K7: PICK A LESSON", "K8: DRILL KIND", "OCTAVE -/+: OCTAVE", "", HLINES_COMMON[0], 0}};
static const char *const HELP2[3][12] = {
    {"PAGE 2 PULSE: LOOP IT", "WHAT IT IS FOR:", "A LOOP: DRUMS ON A GRID, A", "CHORD PER BEAT, A BASS THAT", "FOLLOWS THE KICK, A MELODY.", "HOW TO LEARN HERE:", "SEQ PLAY. CHANGE ONE THING,", "LISTEN. TAP CHORDS ON PADS.", "RECORD ONE PASS OF MELODY.", "CHORD = RHYTHM TRAINER.", HLINES_COMMON},
    {"PAGE 2 BUTTONS", "SEQ PLAY: LOOP ON / OFF", "PADS: THE LANE'S 16 STEPS", "GLOBE+PAD 1-4: PICK LANE", "FX: PADS = CHORDS / GRID", "SCALE: NEXT LANE  BT: RE-ROLL", "SEQ REC: CHORD REC / RAW-SNAP", "SEQ REC+SEQ PLAY: MELODY TAKE", "SEQ REC+BT: UNDO THE TAKE", "CHORD: RHYTHM TRAINER", "PATCH: STOP EVERYTHING", 0},
    {"PAGE 2 KNOBS", "K2: HITS  K3: SHIFT (LANE)", "K4: SWING*  K5: FILL*", "K6: BASS RULE  K7: TEMPO", "K8: SNAP TO GRID %", "PARA HELD: K2 PATCH K3 TRACK", "  K4 SLOW K5 LESSON K6 PERC", "  K7 BARS K8 BASS OCTAVE", "FADERS: MEL CHD BASS DRUM LVL", "* UNLOCKED BY THE TRAINER", HLINES_COMMON[0], 0}};
static const char *const HELP3[3][12] = {
    {"PAGE 3 SHADOW: GROW IT", "WHAT IT IS FOR:", "NEW PARTS MADE BY A RULE ON A", "PART YOU HAVE: 3RD ABOVE THE", "MELODY, ARPEGGIATED CHORDS,", "GHOST HATS. AND A TAPE DECK.", "HOW TO LEARN HERE:", "PAD 1-3 SOURCE, THEN A RULE.", "HOLD A RULE PAD: HEAR IT.", "TAPE PASSES, UNDO, COMPARE.", HLINES_COMMON},
    {"PAGE 3 BUTTONS", "PADS 1-3: SOURCE CHD MEL DRUM", "PADS 9-16: RULE = A SHADOW", "PADS 4-6: SLOT A B C, HOLD=RM", "PAD 7: TRACK  PAD 8: EXPORT", "SEQ REC+SEQ PLAY: TAPE A PASS", "SEQ REC (PLAYING): PUNCH BAR", "SEQ REC TAP: A/B LAST TAKES", "BT: UNDO  GLOBE+BT: REDO", "NOTE RPT: HEAR SRC / SHADOW", "FX: REMOVE  PARA: NEXT SLOT", 0},
    {"PAGE 3 KNOBS", "K2: OCTAVE  K3: DENSITY", "K4: VELOCITY  K5: PATCH", "K6: SEND PICK  K7: SEND AMT", "K8: SLOW MODE", "FADERS 1-3: OBEDIENCE A B C", "FADER 4: THE SEND", "GLOBE+PAD 1-6: SOLO (HELD)", "", "", HLINES_COMMON[0], 0}};
static const char *const HELP4[3][12] = {
    {"PAGE 4 STAGE: FINISH IT", "WHAT IT IS FOR:", "THE LOOP BECOMES A SECTION.", "EIGHT SLOTS. QUEUE THEM BY", "HAND AND A SONG HAPPENS.", "HOW TO LEARN HERE:", "STORE A VERSE. CHANGE THE", "LOOP, STORE A CHORUS. PLAY,", "SWITCH ON THE PADS, RECORD", "THE ORDER WITH SEQ REC.", HLINES_COMMON},
    {"PAGE 4 BUTTONS", "GLOBE+PAD 9-16: STORE AS SLOT", "PAD 1-8: SLOT AT NEXT LOOP", "2X TAP: NEXT BEAT  HOLD: ONCE", "PAD 9-16: SLOT'S TYPE", "SCALE: STORE TYPE  BT: STORE", "SEQ PLAY: LOOP  SEQ REC: REC", "PATCH: STOP (AGAIN: DISCARD)", "CHORD: NEXT TRANSITION", "PAD HELD 2S: WHAT IS IN IT", "", 0},
    {"PAGE 4 KNOBS", "K2: ENERGY 1-8", "K3: STORE-AS TYPE", "K4: LESSON  K5: SLOW", "K6: TRANSITION", "K7: TEMPO +-10%  K8: SWING", "FADERS: ENERGY DRUMS MUSIC", "  MELODY", "", "", HLINES_COMMON[0], 0}};
#else
#define HLINES_COMMON "GLO+SEQ: NEXT HELP SCREEN", "GLO+ARP: YOUR RESULTS", "GLO+HOME 2S: PLAIN SLOOP"
static const char *const HELP1[3][12] = {
    {"PAGE 1 EMBER: HEAR IT", "WHAT IT IS FOR:", "PLAY AND READ THE SCREEN.", "IT NAMES EVERY NOTE AND CHORD", "AND SAYS WHAT IT DOES IN THE", "KEY. WRONG NOTES ARE NUDGED.", "HOW TO LEARN HERE:", "ARP STARTS LESSON 1. PASS IT,", "THEN THE NEXT. 20 LESSONS.", "DRILLS: COPY WHAT YOU HEAR.", HLINES_COMMON},
    {"PAGE 1 BUTTONS", "KEYS: PLAY A NOTE", "GLO+SCL: CHORD KEYS ON/OFF", "  (A WHITE KEY = ITS CHORD)", "PLAY: START/STOP A DRILL", "REC: REPLAY REF, THEN YOU", "ARP: LESSON ON / OFF", "SCL: NEXT SCALE TYPE", "GLO+KEY: THAT NOTE IS ROOT", "ENV: HINTS+NUDGE  FX: STAFF", "HOME: STOP EVERYTHING", "SEQ: NEXT PAGE", 0},
    {"PAGE 1 KNOBS", "K1: SOUND ENGINE", "K2: PRESET", "K3: REVERB  K4: DELAY", "K5: SLOW MODE 50-100%", "K6: PICK A LESSON", "K7: DRILL KIND", "OCT -/+: OCTAVE", "", "", HLINES_COMMON[0], 0}};
static const char *const HELP2[3][12] = {
    {"PAGE 2 ROOT: LOOP IT", "WHAT IT IS FOR:", "A LOOP: DRUMS ON A GRID, A", "CHORD PER BEAT, A BASS THAT", "FOLLOWS THE KICK, A MELODY.", "HOW TO LEARN HERE:", "PLAY. CHANGE ONE THING,", "LISTEN. CHORD KEYS WRITE", "THE LOOP. RECORD A MELODY.", "ENV = RHYTHM TRAINER.", HLINES_COMMON},
    {"PAGE 2 BUTTONS", "PLAY: LOOP ON / OFF", "FX: KEYS = CHORDS / MELODY", "GLO+KEYS 1-16: LANE STEPS", "GLO+KEYS 17-19: BASS RULE", "SCL: NEXT LANE  SAVE: RE-ROLL", "REC: CHORD REC / RAW-SNAP", "REC+PLAY: MELODY TAKE", "REC+SAVE: UNDO THE TAKE", "ENV: TRAINER (ANY KEY TAPS)", "HOME: STOP EVERYTHING", 0},
    {"PAGE 2 KNOBS", "K1: HITS  K2: SHIFT (LANE)", "K3: SWING*  K4: FILL*", "K5: BASS RULE  K6: TEMPO", "K7: SNAP TO GRID %", "EDIT HELD: K1 PATCH K2 TRACK", "  K3 SLOW K4 LESSON K5 PERC", "  K6 BARS K7 BASS OCTAVE", "* UNLOCKED BY THE TRAINER", "", HLINES_COMMON[0], 0}};
static const char *const HELP3[3][12] = {
    {"PAGE 3 TAPE: GROW IT", "WHAT IT IS FOR:", "NEW PARTS MADE BY A RULE ON A", "PART YOU HAVE: 3RD ABOVE THE", "MELODY, ARPEGGIATED CHORDS,", "GHOST HATS. AND A TAPE DECK.", "HOW TO LEARN HERE:", "GLO+KEY 1-3 SOURCE, 9-16 RULE", "LFO: HEAR SOURCE, THEN SHADOW", "TAPE PASSES, UNDO, COMPARE.", HLINES_COMMON},
    {"PAGE 3 BUTTONS", "GLO+KEYS 1-3: SOURCE", "GLO+KEYS 9-16: RULE", "GLO+KEYS 4-6: SLOT A B C", "GLO+KEYS 17-19: TRACK", "REC+PLAY: TAPE A PASS", "REC (PLAYING): PUNCH A BAR", "REC TAP: A/B THE LAST TAKES", "SAVE: UNDO  GLO+SAVE: REDO", "LFO: HEAR SRC / SHADOW", "FX: REMOVE  EDIT: NEXT SLOT", 0},
    {"PAGE 3 KNOBS", "K1: OCTAVE  K2: OBEDIENCE", "GLO+K2: DENSITY  K3: VELOCITY", "K4: PATCH  K5: SEND PICK", "K6: SEND AMOUNT  K7: SLOW", "HOME+REC: EXPORT OVER USB", "", "", "", "", HLINES_COMMON[0], 0}};
static const char *const HELP4[3][12] = {
    {"PAGE 4 SCRIBE: FINISH IT", "WHAT IT IS FOR:", "THE LOOP BECOMES A SECTION.", "EIGHT SLOTS. QUEUE THEM BY", "HAND AND A SONG HAPPENS.", "HOW TO LEARN HERE:", "STORE A VERSE. CHANGE THE", "LOOP, STORE A CHORUS. PLAY,", "SWITCH WITH GLO+KEYS, RECORD", "THE ORDER WITH REC.", HLINES_COMMON},
    {"PAGE 4 BUTTONS", "GLO+KEYS 9-16: STORE AS SLOT", "GLO+KEYS 1-8: SLOT, NEXT LOOP", "2X TAP: NEXT BEAT  HOLD: ONCE", "SCL: STORE TYPE  SAVE: STORE", "PLAY: LOOP  REC: RECORD", "HOME: STOP (AGAIN: DISCARD)", "ENV: NEXT TRANSITION", "", "", "", 0},
    {"PAGE 4 KNOBS", "K1: ENERGY 1-8", "K2: STORE-AS TYPE", "K3: LESSON  K4: SLOW", "K5: TRANSITION", "K6: TEMPO +-10%  K7: SWING", "", "", "", "", HLINES_COMMON[0], 0}};
#endif
static void overlay_help(void)
{
    const char *const (*h)[12] = lpage == 2u ? HELP2 : lpage == 3u ? HELP3 : lpage == 4u ? HELP4 : HELP1;
    const char *const *sc = h[(lov_help - 1u) % HELP_SCREENS];
    char tag[8];
    unsigned i, n;
    ld_bar_text(0, sc[0], LC_PANEL, LC_TEXT);
    n = ls_num(tag, 0, sizeof tag, lov_help); n = ls_cat(tag, n, sizeof tag, "/"); ls_num(tag, n, sizeof tag, HELP_SCREENS);
    ld_text(212, 1, tag, 0, LC_GREY);
    for (i = 1; i < 12u && sc[i]; i++)
        if (sc[i][0]) { unsigned L = 0; while (sc[i][L]) L++; ld_text(4, 20 + (int)(i - 1u) * 17, sc[i], 0, sc[i][L - 1u] == ':' ? LC_AMBER : LC_TEXT); }
    ld_text(4, 222, "SEQ: NEXT   ANY OTHER: CLOSE", 0, LC_DIM);
}
static void overlay_welcome(void)
{
    ld_bar_text(0, "WELCOME TO LEARN MODE", LC_AMBER, LC_BG);
    ld_text(4, 26, "1. PLAY THE KEYS. THE SCREEN", 0, LC_TEXT);
    ld_text(4, 44, "   NAMES WHAT YOU PLAY.", 0, LC_TEXT);
    ld_text(4, 70, "2. PRESS ARP: LESSON 1.", 0, LC_TEXT);
    ld_text(4, 88, "   IT TALKS YOU THROUGH.", 0, LC_TEXT);
    ld_text(4, 114, "3. " LBL_GLO "+SEQ: HELP, ANY TIME.", 0, LC_TEXT);
    ld_text(4, 132, "   " LBL_GLO "+ARP: YOUR RESULTS.", 0, LC_TEXT);
    ld_text(4, 158, "SEQ: THE NEXT PAGE (4 PAGES).", 0, LC_GREY);
    ld_text(4, 176, LBL_GLO "+" LBL_HOME " 2S: PLAIN SLOOP.", 0, LC_GREY);
    ld_text(4, 222, "PRESS ANY KEY TO BEGIN", 0, LC_AMBER);
}

/* ---- the screen (ui_draw.c): 1 = LEARN mode drew (or kept) the screen, 0 = not ours ---- */
static int learn_screen(void)
{
    uint32_t now = fm1_ms;
    int pass;
    if (!learn_on)
        return 0;
    if (!ui.force && !ld_dirty)
        return 1;
    if (!ui.force && now - ld_last_ms < 40u)
        return 1;                                 /* (at most 25 frames a second) */
    ld_last_ms = now;
    ld_dirty = 0;
    cv_oy = 0;
    for (pass = 0; pass < 2; pass++) {
        ld_y0 = pass * 120;
        cv_begin(240, 120, LC_BG);
        if (lov_welcome) overlay_welcome();
        else if (lov_help) overlay_help();
        else if (lov_log) overlay_log();
        else {
            char tag[6];
            unsigned n = ls_cat(tag, 0, sizeof tag, "P");
            n = ls_num(tag, n, sizeof tag, lpage); n = ls_cat(tag, n, sizeof tag, "/"); ls_num(tag, n, sizeof tag, LEARN_STAGE);
            page_draw(&lx);
            ld_text(188, 2, tag, 0, LC_DIM);    /* which of the pages this is */
        }
        cv_blit(0, (uint32_t)ld_y0);
    }
    return 1;
}

/* ---- the lights (ui_input.c ui_leds) ---- */
static void learn_leds(void)
{
    uint32_t c;
    uint8_t nl[LED_BUF];
    memset(nl, 0, sizeof nl);
    led_put(nl, panel.btn[B_OCTDN], lx.octave < 0);
    led_put(nl, panel.btn[B_OCTUP], lx.octave > 0);
    led_put(nl, panel.btn[B_ARP], les_idx != 0u);
    led_put(nl, panel.btn[B_SEQ], LEARN_STAGE >= 2 && ((fm1_ms / 500u) & 1u) == 0u && lpage != LEARN_STAGE);
#if BOARD_SMK37
    for (c = 0; c < SMK_NLED; c++)
        fm1_led[c] = nl[c];
    smk_pad_daw = 1;                              /* every pad comes to the layer through the outer-key queue */
    smk_pad_piano = 0;
    smk_pad_pick = 0;
    if ((int32_t)(fm1_ms - bled_hold_until) < 0)
        for (c = 0; c < SMK_NLED; c++)
            fm1_led[c] = c == bled_id;
    if ((int32_t)(fm1_ms - padled_hold_until) < 0)
        return;
    for (c = 0; c < SMK_NPAD; c++) {
        uint32_t rgb = page_pad_colour(c);
        padled_set(c, (uint8_t)(rgb >> 16), (uint8_t)(rgb >> 8), (uint8_t)rgb);
    }
#else
    {
        uint8_t dl[LED_BUF];
        uint32_t k;
        memset(dl, 0, sizeof dl);
        for (k = 0; k < 27u; k++) {               /* the key LEDs: follow-me bright, what sounds dim */
            led_put(nl, LED_KEY(k), (int)((lbright_keys >> k) & 1u));
            led_put(dl, LED_KEY(k), (int)((llit_keys >> k) & 1u));
        }
        for (c = 0; c < FM1_NCOL; c++) {
            fm1_led[c] = nl[c];
            fm1_led_dim[c] = (uint8_t)(dl[c] & ~nl[c]);
            fm1_led_bg[c] = 0;
        }
    }
#endif
}
