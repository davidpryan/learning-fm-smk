/* SPDX-License-Identifier: GPL-3.0-only
 * The learn layer, one include for SLOOP's single translation unit (firmware/src/felucca.c, FELUCCA_LEARN=1).
 * Order matters: the header, the pure helpers, then the plumbing, the stages and the mode. */
#include "learn.h"
#include "theory.c"
#include "log.c"
#include "lessons.c"
#include "draw.c"
#include "midi.c"
#include "sysex.c"
#include "stage1.c"
#if LEARN_STAGE >= 2
#include "song.c"
#include "stage2.c"
#endif
#if LEARN_STAGE >= 3
#include "stage3.c"
#endif
#if LEARN_STAGE >= 4
#include "stage4.c"
#endif
#include "main.c"
