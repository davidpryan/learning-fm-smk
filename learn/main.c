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
static void stage_init_page(unsigned p)
{
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
        learn_notice(l->unlocks & UNLOCK_STAGE2 && lpage == 1u && LEARN_STAGE > 1 ? "LESSON DONE! SEQ: PAGE 2 IS READY" : "LESSON DONE!", 2500);
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
        stage_init_page(1);
        learn_notice("LEARN ON. GLO+HOME 2S = SLOOP", 2500);
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
        page_draw(&lx);
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
