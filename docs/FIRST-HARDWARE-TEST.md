# First test on hardware

The firmwares have passed CI (real toolchain) and the host suite (every page
on SLOOP's UI harness), but no one has run them on a device yet. Do this in
order, one device at a time, and stop at the first surprise.

## 0. Before anything

1. Read `SAFETY.md`. Charge the device; use a data cable straight into a
   computer with desktop Chrome or Edge.
2. **SMK-37 Elite: take a full-flash backup** with the port kit's procedure
   (https://github.com/genkigenki/fm1-to-smk37, docs/02-update-and-recovery.md)
   and check that `trimdat` is present in the dump. Keep it somewhere safe.
   It is the one thing that makes every later step reversible.
3. If plain SLOOP is not on the device yet, install plain SLOOP first
   (`sloop-smk37-elite.fwsc` / `felucca.fwsc` from the same CI run) and play
   with it for a few minutes. That separates "SLOOP on my unit" problems
   (calibration, pads, the model name) from learn-layer problems.

## 1. Install the learn firmware

From the release site, the page for the device in your hands. Wait for the
restart. It boots into plain SLOOP: everything should look and behave like
step 0.3. Check: keys sound, pads sound, SEQ pages, HOME menu.

## 2. Enter LEARN mode

(If a SLOOP project you care about is in working memory, save it to a slot
first: the learn pages write into the live tracks.)

Hold **GLO + HOME** for two seconds (on the SMK-37, the buttons that
HARDWARE CALIBRATION mapped to GLO and HOME; the HOME menu may open at 0.7 s,
that is expected, keep holding). The screen should show page 1 with the
header `C MAJOR  OCT 0` and the notice `LEARN ON. GLO+HOME 2S = SLOOP`.

If it does not switch: note what the screen shows and whether SLOOP still
responds; then power-cycle. Report.

## 3. Page 1 in five minutes

| Do | Expect |
| --- | --- |
| Play single keys | the big line names the note and octave; the keymap strip lights the key; the FM-1 key LED lights under it |
| Play C E G together | `C` with sub line `I` |
| Play C# in C major | you hear D (nudged); ENV then C# again: muted with `NOT IN THE KEY` |
| SMK: tap pad 1, pad 5 | the I and V triads, the roman numerals in the progression line, a why-hint |
| PLAY | an echo drill: a note plays, then `YOUR TURN`; copy it; `HIT` / `MISS` |
| HOME | silence, the drill stops |
| ARP | lesson 1 "First Sounds": two text steps (any key continues), then a drill |
| K1 / K2 | the engine / the preset change (notice names them) |
| GLO + HOME 2 s | plain SLOOP is back, the keys play SLOOP's track again |

Then page 2 (SEQ): PLAY should start the default loop (four kicks, two
snares, hats, chords I V vi IV, a bass under the kick). If audio stutters
or a note hangs, note what you did and press HOME, then GLO+HOME.

## 3b. What 0.4 changed that only the device can show

The host suite renders every screen, but these depend on the real panel,
pads, LCD or flash:

| Check | Expect |
| --- | --- |
| A page 1 echo drill, answered wrong | nothing lit while you listen and answer; after the miss the key lights (FM-1: its LED), the note is named and plays |
| Chord numbers on the SMK pad palette | `ii iii vi vii°` in lower case, readable at arm's length; the bottom row `I7 … viiø` |
| The SMK pads on pages 3 and 4 | no pad that should be usable looks off (dim pads are now half brightness); empty store pads a faint white |
| SMK: K1 in LEARN mode | always the volume, even with HOME > KNOB LAYOUT = SMK; after GLOBE+PATCH the menu's layout is back |
| SMK: lesson progress | pass lesson 1, wait for the loop to be stopped and 2 s without touching anything, power off and on, enter LEARN: page 1 says `ARP: L2 …` and K7 shows `L1 … *` |
| FM-1: lesson progress | resets at power-off (expected: no spare settings byte) |
| GLO + SEQ during a drill | the drill waits behind the help (no misses counted) |
| Page 2 with the loop playing, many screen changes | no audio stutter (the screen redraws at most 25 times a second, as SLOOP) |

## 4. What to report

Device and model, the package name and SHA-256, what you did, what the
screen showed, whether plain SLOOP still works after GLO+HOME, and whether
the installer still sees the device. The practice log can be exported over
SysEx (`LCMD_LOGDUMP`) but nothing on the device depends on it.
