# Learn-layer hooks in SLOOP (all behind FELUCCA_LEARN, default 0)

With FELUCCA_LEARN=0 (plain SLOOP) every hook compiles to nothing and the
build is byte-identical to upstream. With FELUCCA_LEARN=1 the hooks route
input, the screen, the LEDs, the pads, incoming MIDI and SysEx to
learn/learn.c when LEARN mode is on, exactly as ui_daw.c does for DAW mode.

| File | Hook |
| --- | --- |
| tools/build.py | FELUCCA_LEARN and LEARN_STAGE pass through from the environment |
| felucca.c | default FELUCCA_LEARN 0; `#include "../../../learn/pre.h"` (the entry points' prototypes) right after it, before seq.c; `#include "../../../learn/learn.c"` after ui_daw.c |
| ui_input.c ui_input() | `learn_switch(&pressed); if (learn_on) { learn_input(pressed, now); return; }` after daw_switch |
| ui_input.c ui_leds() | `if (learn_on) { learn_leds(); return; }` after the DAW branch |
| ui_draw.c ui_draw() | `if (learn_screen()) return;` after the DAW branch |
| seq.c ext_keys_block() | first thing in the loop: in LEARN mode every queued pad (SMK_EXT_DAW + pad, since the layer sets smk_pad_daw) and outer key goes to learn_ext_event |
| seq.c keyboard_block() | learn_follow() (the mode change, both ways), then in LEARN mode learn_keys_block() (the 27 keys to the layer's ring, the layer's sound requests to the tracks) instead of the synth's keys |
| seq.c midi_in | notes from USB MIDI are also shown to the learn layer (learn_midi_in) |
| editor.c ed_service() | SysEx F0 7D 'L' 'N' frames go to learn_sysex (before the editor's "FL") |

Upstream: the SLOOP smk37 branch commit in ../../UPSTREAM.txt. `git diff <that commit> -- firmware/src tools/build.py` shows exactly these hooks and nothing else.
