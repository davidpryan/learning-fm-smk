# Learn Firmware Manual

*Learn firmware 0.4, October 2026.*

This manual covers learn firmware 0.4 for the M-VAVE **SMK-37 Elite** and **FM-1**: four teaching pages, 36 lessons and every control. New to it? Start with [Getting Started](GETTING-STARTED.md); this manual is the reference.

## Overview

The learn firmware is SLOOP (an open groovebox firmware) with a teaching mode added, called **LEARN mode**. Plain SLOOP is still there and one held combination switches between the two. LEARN mode has four pages, each building on the one before:

| Page | SMK-37 name | FM-1 name | What you learn |
| --- | --- | --- | --- |
| 1 Hear it | Lantern | Ember | notes, chords and keys, by ear and on the screen |
| 2 Loop it | Pulse | Root | rhythm, drum patterns, chord loops, bass, recording a melody |
| 3 Grow it | Shadow | Tape | new parts made by rules from parts you have; takes and overdubs |
| 4 Finish it | Stage | Scribe | turning loops into sections and sections into a song |

Nothing is locked: you can visit any page at any time. Each page's last lessons say when the next page is a good next step.

### How this manual names controls

The two devices print some buttons differently. Where they differ, the manual gives **SMK-37 name / FM-1 name**.

| Role | SMK-37 Elite | FM-1 |
| --- | --- | --- |
| Modifier, held with another control | GLOBE | GLO |
| Stop everything | PATCH | HOME |
| Start / stop | SEQ PLAY | PLAY |
| Record / replay | SEQ REC | REC |
| Hints, trainer, transitions (by page) | CHORD | ENV |
| Next scale, lane or type (by page) | SCALE | SCL |
| Hear source / shadow (page 3) | NOTE REPEAT | LFO |
| Second knob row, next slot (by page) | PARA | EDIT |
| Undo, store, next patch (by page) | BT | SAVE |
| Mode switch (by page) | FX | FX |
| Lessons | ARP | ARP |
| Next page | SEQ | SEQ |
| Octave | OCTAVE - / + | OCT - / + |

**Knobs.** On the SMK-37, **K1 is always the volume**, and the pages use **K2 to K8**. On the FM-1 the pages use **K1 to K7** and the **MASTER** knob is the volume. So the same job is one knob number higher on the SMK-37: a knob given as **K7 / K6** is printed K7 on the SMK-37 and K6 on the FM-1. LEARN mode always uses the SMK-37's default knob layout, whatever the HOME menu's KNOB LAYOUT is set to, and restores your setting when you leave.

**Pads and keys.** The SMK-37 has 16 pads (top row 1-8, bottom row 9-16) and 37 keys. The FM-1 has no pads, so it does the same jobs with **GLO + a key**, counting keys from the lowest: key 1 is the leftmost, black keys included.

## Installing, updating and going back

Installs use SLOOP's own installer and update format, so the stock M-VAVE tools still recognise the device and you can always go back.

**You need:** the device, charged; a USB data cable straight into the computer (no hub); desktop Chrome or Edge.

**Install or update**

1. SMK-37 Elite only, the first time: take a full flash backup with the SMK-37 port kit's procedure (github.com/genkigenki/fm1-to-smk37, *docs/02-update-and-recovery.md*). Check the dump contains the calibration record (`trimdat`). It makes every later step reversible.
2. Optional but wise: back up your SLOOP projects, presets and samples from SLOOP's web editor.
3. Open [davidpryan.github.io/learning-fm-smk](https://davidpryan.github.io/learning-fm-smk/) and choose the page for your device. Each page checks the model and refuses the other device's package.
4. Press **INSTALL** and keep the cable connected until the device restarts. An update installs the same way, over the previous version.
5. The device boots into plain SLOOP. Enter LEARN mode as described in the next section.

| Device | Package | Model identity it checks |
| --- | --- | --- |
| SMK-37 Elite | `sloop-smk37-elite-learn-0.4.fwsc` | SMK-37 Elite |
| FM-1 | `felucca-learn-0.4.fwsc` | FM-1 |

Each package has a `.sha256` file beside it on the [release page](https://github.com/davidpryan/learning-fm-smk/releases/tag/v0.4); `sha256sum -c <package>.sha256` checks a download.

**What is written.** The installer writes the firmware area only. It never touches the bootloader, the Bluetooth pairing area, the factory calibration or the chip key. SLOOP keeps your projects, presets and samples across installs. LEARN mode itself writes nothing to flash, with one exception on the SMK-37: your lesson progress, two bytes inside SLOOP's own settings record.

**Going back**

- **To plain SLOOP**, without reinstalling: hold **GLOBE + PATCH / GLO + HOME** for two seconds.
- **To the stock firmware:** SMK-37: M-VAVE's updater (M-UPGRADE) with the official package. FM-1: the button *Return to the official firmware* on its install page, or M-UPGRADE.
- **If an install was interrupted**, the device waits in update mode: reconnect and press INSTALL again. More in *Troubleshooting and recovery* below.

## LEARN mode basics

These work the same on every page.

### Switching in and out

Hold **GLOBE + PATCH / GLO + HOME** for two seconds. SLOOP's own menu may open briefly while you hold; keep holding. LEARN mode stops SLOOP's sequencer and opens on page 1 with a welcome screen; any key or button closes it, without playing anything. The same hold returns you to plain SLOOP.

The pages write their loops into SLOOP's four live tracks, and SLOOP autosaves those. Save a SLOOP project you care about to a slot before entering LEARN mode.

### Reading the screen

| Where | What it shows |
| --- | --- |
| Top line | the page's state (key, tempo, bars, slow mode), the page tag **P1/4**, and a small square that turns green when a second device is talking to this one |
| Bar over the top line | notices: what a button or knob just did. Amber notices come from a lesson (step results, lesson done) and stay up until they time out |
| Big line | the main thing: a note, a chord, a score, a section name |
| Amber bar | a lesson's text: read it, then press any key or pad |
| Green bar | your turn: play now |
| Red bar | recording |
| Purple line | the running lesson: number, title, step (**L3 HOME NOTE 2/2**) |
| Bottom line, yellow | a why-hint: what the thing you just did does musically |

Chord numbers follow standard notation: capitals are major (**I IV V**), lower case minor (**ii vi**), ° diminished (**vii°**), ø half-diminished seventh (**viiø7**).

### Help on the screen

**GLOBE + SEQ / GLO + SEQ** opens the current page's help, four screens: what the page is for and how to learn on it, its buttons and pads, its knobs and faders, and what its screen shows (on page 1 the colour legend, on page 3 the rule list). **SEQ** goes to the next screen; any other button closes it.

### Your results

**GLOBE + ARP / GLO + ARP** opens the results log; the same combination closes it. Each line is one drill, take, song or lesson, newest first: its number, kind, lesson (**L3**), right answers out of tries, and score. **SLOW** marks a result played in slow mode; **DONE** marks a finished lesson, with its average score. The header counts your **STREAK** (passes in a row) and **TRIES**. The log is kept until power-off.

While the help, the results or the welcome screen is open, the page's clock stops: a drill or the rhythm trainer waits for you instead of counting misses. A loop that is playing keeps playing.

### Lessons

**ARP** starts the page's next lesson and stops a running one. Each page has its own lessons (page 1: 20; pages 2-4: 5 or 6 each) and remembers which one is next. A knob picks another (page 1 **K7 / K6**, page 2 **PARA + K5 / EDIT + K4**, page 3 **GLOBE + K8 / GLO + K7**, page 4 **K4 / K3**); a **\*** after a title means you have passed it.

A lesson is a series of steps:

- **Text steps** show on an amber bar; any key or pad continues. The press also does what it normally does.
- **Scored steps** (drills, rhythms, grooves, takes, songs) have a target, from 50% to 90%. Reach it and the next step starts by itself. Miss it and the step waits: the top bar says **NOT YET: NEED 70%** and the start button tries it again. After two misses the screen suggests slow mode.

When the last step passes, the top bar says **L3 DONE! ARP: NEXT, L4**, the next lesson becomes the page's pick, and the log records the lesson's average score.

**Progress.** The SMK-37 keeps each page's lesson progress, and the swing and fill unlocks, when you power off. The FM-1 has no spare settings space, so there progress lasts until power-off; pick up again with the lesson knob.

### Slow mode

Every page has a slow-mode control (50-100%) that slows drills, the trainer and replays. Use it to learn, then come back up to speed: a pass in slow mode is logged as SLOW, doesn't count towards the streak, and doesn't unlock swing or fill. Slow takes on page 3 don't pass a lesson step.

### Stop everything

**PATCH / HOME** stops the current drill, recording or loop and silences every note.

## Page 1: Hear it (Lantern / Ember)

Page 1 names every note and chord you play, explains what it does in the key, and trains your ear with four drills.

### The screen

- **Top line:** the key and scale (**C MAJOR**), the octave if not 0, slow mode if on.
- **Big line:** the note you hold and its octave (**C4**); with three or more keys, the chord (**Am**). After a drill, your score.
- **Line under it:** what the note is in the key (**3RD OF THE KEY**), the interval between your last two notes, or the chord's number (**vi IN C MAJOR**).
- **Key strip:** the whole keyboard; held keys light cyan, a key to press lights white.
- **Orange line:** your last four chords as numbers (**I - V - vi - IV**).
- **SMK-37:** the pad palette, two rows of eight laid out like the pads. **FM-1:** a treble staff with the note you hold.
- **Prompt bar, lesson line, YOU row** (your last answer: green notes were right, red wrong) and the **why-hint** at the bottom.

### Controls

| Control | SMK-37 Elite | FM-1 |
| --- | --- | --- |
| Play | keys (C3 is the lowest at octave 0) | keys (F3 is the lowest at octave 0) |
| Chords | pads 1-7: the chords of the key; pads 9-15: the same with a 7th; pad 8 sus4, pad 16 sus2 (of the last chord) | **GLO + SCL** turns CHORD KEYS on / off: each white key plays its chord |
| Set the root | GLOBE + a key | GLO + a key |
| Next scale (major, minor, pentatonic major, pentatonic minor, blues) | SCALE | SCL |
| Hints and nudging on / off | CHORD | ENV |
| Start / stop a drill (in a lesson: retry the step) | SEQ PLAY | PLAY |
| Replay: the reference, then your attempt | SEQ REC | REC |
| Lesson start / stop | ARP | ARP |
| Stop everything | PATCH | HOME |
| Next sound engine / next preset | PARA / BT | EDIT / SAVE |
| Staff on / off | n/a | FX |
| Octave | OCTAVE - / + | OCT - / + |
| Sound engine (SLOOP's engines; the top bar names each) | K2 | K1 |
| Preset | K3 | K2 |
| Reverb / delay | K4 / K5 (also faders 1 / 2) | K3 / K4 |
| Slow mode 50-100% | K6 (also fader 4) | K5 |
| Pick a lesson | K7 | K6 |
| Drill kind: echo, chord match, progression, scale | K8 | K7 |

**Hints and nudging.** With hints on, a note outside the key is moved to the nearest note in it, and each chord you play shows a why-hint (**IV LIFTS AWAY FROM HOME**). With hints off, off-key notes are silent and the top bar names them (**C# IS NOT IN C MAJOR**).

### The drills

Choose a kind with **K8 / K7** and press **SEQ PLAY / PLAY**. A free drill has ten rounds and passes at 80%; in a lesson, the step sets the rounds and the target.

| Drill | You hear | You do |
| --- | --- | --- |
| Echo | one note | play it; any octave counts |
| Chord match | a chord | play it: a pad, a chord key, or the notes |
| Progression | chords in order (I-V-vi-IV unless the lesson sets others) | play each one back |
| Scale | the next note up the scale | play the lit key; the big line names it |

Each round: the bar says **LISTEN**, then turns green for **YOUR TURN**. You have eight seconds, longer in slow mode. In the echo, chord and progression drills nothing is shown until you answer; a miss, or time running out, lights the answer, names it and plays it again (**MISS: HERE IT IS**). The scale drill lights the next key all along, because finding it is the point.

After the last round the result card shows your score and **PASSED** or how far you were from the target. **SEQ REC / REC** replays the reference and then your attempt.

### Chord colours

The SMK-37 pads and the chord cells on the screen share one colour language. The 7th row of pads is a dimmer shade.

| Colour | Means |
| --- | --- |
| Green | the home chord: I (major key) or i (minor key) |
| Orange | V or v: pulls back home |
| Cyan | IV or iv: lifts away from home |
| Blue | another minor chord |
| Purple | another major chord (III, VI, VII in a minor key) |
| Red | diminished: tense |
| Grey | sus4 / sus2 |

Help screen 4 on page 1 shows the same legend on the device.

## Page 2: Loop it (Pulse / Root)

Page 2 is a loop you build one part at a time: four drum lanes, a chord for every beat, a bass line that follows the kick, and a melody you record. Press **SEQ PLAY / PLAY** and change one thing at a time while it plays.

### The screen

- **Top line:** tempo, bars, key, then swing, fill and slow mode when set.
- **Big line:** a knob's new value for two seconds; otherwise the bar and the chord under the play head (**BAR 1 vi**), or **PRESS SEQ PLAY** when stopped. During the trainer: the count-in, then **ON TIME**, **EARLY**, **LATE** or **MISS**.
- **The grid:** rows **KICK, SNARE, HAT, PERC** (the drum lanes; the one you're editing has its name in colour), **BASS**, **CHD** (one chord a beat, by number) and **MEL**. A lit cell is a note; the white column is the play head.
- **SMK-37 in chord mode:** the pad palette replaces the drum lanes, so you can see which pad is which chord.
- **FM-1 with GLO held:** the drum lanes become a picture of the keys: keys 1-16 lit where the lane has steps, and keys 17-19 showing the bass rule.
- **Status line:** what the pads / keys do now, and the bass rule (**PADS: STEPS  BASS: KICK-LOCK**).

### Drums: Euclidean lanes

Each lane spreads its **hits** as evenly as possible over 16 steps; **shift** rotates the pattern. Four kicks on 16 steps is four-on-the-floor; three gives a tresillo-like push. Choose the lane with **SCALE / SCL**, then turn hits and shift. You can also toggle single steps:

- SMK-37: in step mode the 16 pads are the lane's 16 steps; **GLOBE + pad 1-4** picks the lane.
- FM-1: **GLO + key 1-16** toggles that step.

**BT / SAVE** re-rolls the lane from its hits and shift, clearing hand-placed steps.

### Chords and bass

The chord row holds one chord per beat. To write it: press **SEQ REC / REC** once (a click starts), play chords on the pads (SMK-37, chord mode: **FX**) or the chord keys (FM-1, chord mode: **FX**), one per beat, then press **SEQ REC / REC** again. The bass follows the chords and the kick by one of three rules: **KICK-LOCK** (a bass note on every kick), **OFFBEAT** (a note two 16ths after each kick), **WALK** (eighth notes alternating the root and fifth, an octave jump every other beat, regardless of the kick).

### Recording a melody

Press **SEQ REC + SEQ PLAY / REC + PLAY** together. The red bar says **RECORDING ONE PASS: PLAY!**; play over the loop. Recording ends by itself when the loop comes round. The take is snapped towards the grid by the snap knob; **SEQ REC / REC** then switches between the raw take and the snapped one, and **SEQ REC + BT / REC + SAVE** undoes it.

### The rhythm trainer

**CHORD / ENV** starts it. It counts in with four big numbers (**4 3 2 1**) and clicks. Round 1: tap on every beat. Round 2: tap the pattern (the kick lane). Tap with **pad 1** on the SMK-37, or **any key** on the FM-1; a green square flashes when a tap is due. Each tap reads **ON TIME**, **EARLY** or **LATE**, and the line below counts hits and misses.

Passing at full speed unlocks the two locked knobs: **swing** after a pass on the beat, **fill** after a pass on a pattern with offbeats. Until then the knob says **LOCKED: PASS THE TRAINER**.

### Controls

| Control | SMK-37 Elite | FM-1 |
| --- | --- | --- |
| Loop on / off | SEQ PLAY | PLAY |
| Pads / keys: steps or chords | FX (pads) | FX (keys: chords or melody) |
| Pick a lane | SCALE, or GLOBE + pad 1-4 | SCL |
| Toggle a step | pad 1-16 | GLO + key 1-16 |
| Bass rule | K6 | K5, or GLO + key 17-19 |
| Re-roll the lane | BT | SAVE |
| Chord recording on / off; after a take, raw / snapped | SEQ REC (tap) | REC (tap) |
| Melody take | SEQ REC + SEQ PLAY | REC + PLAY |
| Undo the take | SEQ REC + BT | REC + SAVE |
| Rhythm trainer | CHORD | ENV |
| Lesson start / stop | ARP | ARP |
| Stop everything | PATCH | HOME |
| Hits / shift (the lane) | K2 / K3 | K1 / K2 |
| Swing / fill (unlocked by the trainer) | K4 / K5 | K3 / K4 |
| Tempo | K7 | K6 |
| Snap to grid % | K8 | K7 |
| Second row, knob held with PARA / EDIT: patch, track, slow mode, lesson, perc sound, bars, bass octave | PARA + K2 to K8 | EDIT + K1 to K7 |
| Levels: melody, chords, bass, drums | faders 1-4 | n/a |

## Page 3: Grow it (Shadow / Tape)

Page 3 grows the loop from page 2 with **shadows**: new parts made by a rule from a part you already have, such as a harmony a third above the melody, or the chords played as an arpeggio. It also has a tape deck for recording takes of the melody, chords or bass.

### Making a shadow

1. Pick a **source**: pad 1 chords, pad 2 melody, pad 3 drums (FM-1: **GLO + key 1, 2 or 3**). The big line names it, and the lanes give way to the rule list.
2. Pick a **rule** from pads 9-16 (FM-1: **GLO + key 9-16**). The list shows which pad is which rule; the ones that fit your source are bright.
3. The shadow plays with the loop. The big line shows its slot and rule (**B 3RD ABOVE**) and the line under it its settings.

| Pad / key | Rule | Source | What it plays |
| --- | --- | --- | --- |
| 9 | HOLD | chords | the chord's notes, one per beat |
| 10 | ARP | chords | the chord's notes in a pattern you tap in |
| 11 | 3RD ABOVE | melody | the melody, two scale steps higher |
| 12 | 6TH BELOW | melody | the melody, five scale steps lower |
| 13 | ECHO | melody | the melody a beat late, an octave up, softer |
| 14 | ANSWER | melody | the last four notes, backwards |
| 15 | TOP NOTE | chords | the highest note of each chord |
| 16 | GHOST | drums | soft hits between the hats |

For ARP, tap the order of the notes on pads 9-16 (FM-1: GLO + keys 9-16) when the prompt asks. Hold a rule pad to hear its explanation; **NOTE REPEAT / LFO** plays the source alone, then the shadow alone, so you can hear what the rule did.

There are three **slots**, A, B and C, one per source (pads 4-6; FM-1 GLO + keys 4-6). **PARA / EDIT** selects the next slot, **FX** removes the selected shadow, and holding a slot pad for two seconds removes it too.

**Obedience** sets how strictly a shadow follows its rule: **STRICT**, **HUMAN** (small variations) or **LOOSE**. It is fader 1-3 for slots A-C on the SMK-37, and **K2** on the FM-1.

### The tape deck

The tape records one pass of the loop onto a track: the melody, the chords or the bass (**SCALE / SCL** or pad 7 picks; FM-1 also GLO + key 17-19).

- **Tape a pass:** **SEQ REC + SEQ PLAY / REC + PLAY**, play, and press **SEQ PLAY / PLAY** to finish.
- **Punch in:** while playing, hold **SEQ REC / REC**: the next bar is re-recorded.
- **Compare:** tap **SEQ REC / REC** to switch between your last two takes (A/B). Up to four takes are kept; the screen shows **TAKE 2 OF 3**.
- **Undo / redo:** **BT / SAVE** undoes the last take; **GLOBE + BT / GLO + SAVE** redoes it.
- **Export:** pad 8 (FM-1: **HOME + REC**) sends the loop over USB MIDI, so a computer or the other device can capture it.

A melody take with at least four notes, 80% of them in the key, at full speed, says **A TAKE IN KEY!**: a good sign you're ready for page 4.

### The screen

While no source is armed, four lanes show **MEL, CHD, BASS, DRM**: a bar for how much each plays (in colour if it has a shadow), the play head, punched bars in red, and a level meter on the right. Below are the prompt, the lesson line and the takes line.

### Controls

| Control | SMK-37 Elite | FM-1 |
| --- | --- | --- |
| Source: chords, melody, drums | pads 1-3 | GLO + keys 1-3 |
| Slot A, B, C | pads 4-6 (hold 2 s: remove) | GLO + keys 4-6 |
| Tape track | pad 7 or SCALE | GLO + keys 17-19 or SCL |
| Export over USB | pad 8 | HOME + REC |
| Rule | pads 9-16 | GLO + keys 9-16 |
| Solo a part while held | GLOBE + pad 1-6 | n/a |
| Tape a pass | SEQ REC + SEQ PLAY | REC + PLAY |
| Punch a bar (while playing) | hold SEQ REC | hold REC |
| A/B the last takes | SEQ REC (tap) | REC (tap) |
| Undo / redo a take | BT / GLOBE + BT | SAVE / GLO + SAVE |
| Hear source, then shadow | NOTE REPEAT | LFO |
| Remove the shadow / next slot | FX / PARA | FX / EDIT |
| Hints on / off | CHORD | ENV |
| Lesson start / stop; pick a lesson | ARP; GLOBE + K8 | ARP; GLO + K7 |
| Octave of the shadow | K2 | K1 |
| Density | K3 | GLO + K2 |
| Obedience | faders 1-3 | K2 |
| Velocity | K4 | K3 |
| Patch | K5 | K4 |
| Send: reverb or delay; its amount | K6; K7 (or fader 4) | K5; K6 |
| Slow mode | K8 | K7 |

## Page 4: Finish it (Stage / Scribe)

Page 4 turns loops into a song. You store the current loop as a **section** (a verse, say), change the loop and store another (a chorus), then switch between sections by hand while it plays, and record the order you played.

### Sections

There are eight slots. To store the loop in one:

- SMK-37: **GLOBE + pad 9-16** stores into slots 1-8 (or **BT** stores into the selected slot).
- FM-1: **GLO + key 9-16** (or **SAVE**).

The section takes the **store-as type** shown on screen (**STORE AS VERSE**); **SCALE / SCL** or **K3 / K2** changes it. A section keeps the loop's chords, drums, parts, tempo and energy. Types: INTRO, VERSE, PRE (pre-chorus), CHORUS, BRIDGE, BREAK, OUTRO, FREE, each with its own colour. On the SMK-37, pad 9-16 without GLOBE changes that slot's type.

### Playing them

With the loop playing (**SEQ PLAY / PLAY**), press a section's pad 1-8 (FM-1: **GLO + key 1-8**):

- **Once:** it starts at the end of the current loop. The screen says **> CHORUS IN 4 BEATS** and the pad blinks.
- **Twice quickly:** it starts on the next beat.
- **Held:** it plays once, then returns.

Hold an SMK-37 pad for two seconds to see what's in it (type, energy, bars, chords).

**Energy** (**K2 / K1**, 1-8) is how much is playing: 1 drums only, 2 adds the bass, 3 the chords, 4 the melody, and 5-8 make it all louder. Each type starts at its usual energy (a chorus 8, a break 1).

**Transitions** happen on the last bar before the change. **CHORD / ENV** or **K6 / K5** chooses one:

| Transition | On the last bar |
| --- | --- |
| NONE | nothing |
| DROP | drums and bass drop out |
| RISER | the hats double, the reverb rises |
| FILL | a snare and hat roll |
| HAND-OFF | the melody fades, the next chords come in early |

### Recording the song

1. Press **SEQ REC / REC**: **SONG RECORD ON**.
2. Play your sections in the order you want with the pads / GLO + keys. The song bar fills with each section's colour as you go.
3. Press **PATCH / HOME** to stop: **KEPT. PATCH AGAIN: DISCARD**. Press it again within two seconds to throw the take away.

**Song lessons** give you an order to copy (a ghost). Store one slot of each type they ask for. The slot of the next section lights two bars before it's due and blinks through the last bar: queue it while it's lit. The score averages the order and the energy (each section at its type's usual energy), and changes made late fail the step. After a pass, **SEQ REC / REC** alternates the ghost's order and yours on screen.

### The screen

Top: tempo and **STOPPED / PLAYING / RECORDING**. Big line: the section playing, in its colour (**LOOP** when none). Then the bar count and what's queued, the elapsed time and the song bar, the eight slots (**1V 2C** = slot 1 verse, slot 2 chorus; the queued one framed in orange), and lines for the store-as type, the transition and the energy bar.

### Controls

| Control | SMK-37 Elite | FM-1 |
| --- | --- | --- |
| Store the loop as slot 1-8 | GLOBE + pad 9-16 | GLO + key 9-16 |
| Store into the selected slot | BT | SAVE |
| Queue slot 1-8 (twice: next beat; hold: once) | pad 1-8 | GLO + key 1-8 |
| Change a slot's type | pad 9-16 | n/a |
| Store-as type | SCALE or K3 | SCL or K2 |
| Loop on / off | SEQ PLAY | PLAY |
| Song record | SEQ REC | REC |
| Stop (again within 2 s: discard the record) | PATCH | HOME |
| Next transition | CHORD or K6 | ENV or K5 |
| Energy 1-8 | K2 (or fader 1) | K1 |
| Lesson start / stop; pick a lesson | ARP; K4 | ARP; K3 |
| Slow mode | K5 | K4 |
| Tempo, within 10% of the section's | K7 | K6 |
| Swing | K8 | K7 |
| Levels: drums, music, melody | faders 2-4 | n/a |

## The lessons

There are 36 lessons: 20 on page 1, then 6, 5 and 5 on pages 2-4. ARP on a page starts that page's next one. Targets are the score each scored step needs.

| # | Page | Lesson | What you do | Targets |
| --- | --- | --- | --- | --- |
| 1 | 1 | First Sounds | echo notes 1-3 of the scale | 50% |
| 2 | 1 | Up The Steps | climb the C major scale, twice | 60%, 70% |
| 3 | 1 | Home Note | hear the root and the 5th | 70% |
| 4 | 1 | Neighbors | echo the first five notes | 70% |
| 5 | 1 | Your First Chord | find I, then I or V | 60%, 70% |
| 6 | 1 | Two Chords | play I then V in time | 70% |
| 7 | 1 | Three Chords | find I, IV or V; play I-IV-V | 75% |
| 8 | 1 | The Four-Chord Song | play I-V-vi-IV | 75% |
| 9 | 1 | Minor Mood | A minor scale; find i, iv or v | 70% |
| 10 | 1 | Pentatonic Play | echo and climb the pentatonic | 75%, 80% |
| 11 | 1 | New Key: G | G major scale; I-IV-V in G | 75% |
| 12 | 1 | New Key: F | F major (mind the Bb); I-V-vi-IV in F | 75% |
| 13 | 1 | Sevenths | find the 7th chords of I, IV, V | 75% |
| 14 | 1 | ii - V - I | the jazz turnaround ii7-V7-Imaj7 | 75% |
| 15 | 1 | Blues In A | blues-scale echo; Imaj7-IVmaj7-V7 | 75% |
| 16 | 1 | Relative Minor | vi-IV-I-V | 75% |
| 17 | 1 | Ear: Intervals | root, 3rd or 5th by ear | 85% |
| 18 | 1 | Fast Hands | the scale twice, no slow mode | 90% |
| 19 | 1 | Chord Recall | any chord of the key by ear | 85% |
| 20 | 1 | Graduation | ten notes, then four rounds at full tempo | 90% |
| 21 | 2 | Steady Beat | tap on the beat at 80, then 100 BPM; unlocks swing | 70%, 80% |
| 22 | 2 | Offbeats | tap beats and "ands", then only the ands; unlocks fill | 70%, 60% |
| 23 | 2 | Spread The Kick | set the kick to 3 hits, then 5 | match |
| 24 | 2 | Snare And Hat | snare 3 hits shifted 2; hat 6 hits | match |
| 25 | 2 | A Chord Loop | record I V vi IV into the chord row | guided |
| 26 | 2 | One Melody Pass | record one pass of 6+ notes in the key | 80% |
| 27 | 3 | Your First Shadow | a 3rd-above shadow of the melody; hear it; try 6th below | guided |
| 28 | 3 | Chord Shadows | arpeggio and top-note shadows; density and octave | guided |
| 29 | 3 | Tape A Take | tape a melody pass, 4+ notes in the key | 80% |
| 30 | 3 | A Better Take | 8+ notes all in the key; A/B and undo takes | 90% |
| 31 | 3 | Ghost Notes | a ghost shadow on the drums; obedience | guided |
| 32 | 4 | Verse And Chorus | store a verse and a chorus; play them in order | 60% |
| 33 | 4 | Back And Forth | verse, chorus, verse, chorus | 70% |
| 34 | 4 | Start And End | intro, verse, chorus, outro | 70% |
| 35 | 4 | Build And Drop | verse, pre, chorus, break, chorus, with a transition | 70% |
| 36 | 4 | A Whole Song | intro to outro, eight sections | 75% |

**Match** steps pass as soon as the lane matches. **Guided** lessons are text steps that walk you through the controls; any press continues.

## Using the SMK-37 and the FM-1 together

Each device works fully on its own. Linked, they act as one setup: what you play on one counts on the other, and lessons, drills and loops are shown on both screens.

**Connecting them.** Both are USB devices, so neither can host the other. Use one of:

- a computer, phone or tablet (with a hub) running a MIDI routing app that passes all messages, SysEx included, from each device to the other;
- a small USB MIDI host box;
- for one direction only, the SMK-37's TRS MIDI OUT into the FM-1's TRS MIDI IN. This carries notes, not the screen sharing.

When the link works, the small square at the right of the top line turns **green** on both devices (it checks every few seconds).

**What is shared**

| Page | Shared between the devices |
| --- | --- |
| All | notes played on either; a lesson started on one starts on the other if none is running there |
| 1 | the key; chord hints; the running drill's prompt and results; notes from the other device count as answers |
| 2 | the grid, the chords and the trainer's results |
| 3 | shadows created, solos, transport and takes |
| 4 | queued sections, transitions, the section playing and the song position |

A typical split: play and listen on the SMK-37's keys and pads, and read the FM-1's staff and screen as a second display.

## Troubleshooting and recovery

Most problems are a setting; the device itself is very hard to harm. Work down the table, then the recovery steps.

| Symptom | What to do |
| --- | --- |
| No sound | turn up **K1 / MASTER**; press **PATCH / HOME** to clear stuck states; check the page isn't waiting on a lesson step |
| A key makes no sound | hints are off and the note isn't in the key: press **CHORD / ENV**, or change the key |
| The SMK-37 knobs do the wrong thing | the labels assume LEARN mode's knob layout; leave and re-enter LEARN mode |
| Swing or fill says LOCKED | pass the rhythm trainer on page 2 at full speed (lesson 21 or 22 also unlocks them) |
| A lesson step won't move on | it needs its target: read the top bar (**NOT YET: NEED 70%**), press start to retry, try slow mode |
| Progress gone after power-off | expected on the FM-1; pick the lesson with the knob. On the SMK-37, progress is saved when the device is quiet: stop the loop and wait two seconds before switching off |
| The help or results won't close | any other button closes the help; **GLOBE + ARP / GLO + ARP** closes the results |
| The two devices don't see each other | the router must pass SysEx; the green square shows when it works |

### Recovery

Stop at the first step that works.

1. **The screen is stuck.** Hold **GLOBE + PATCH / GLO + HOME** for two seconds to go to plain SLOOP. If that doesn't respond, power off and on; only what LEARN mode keeps in memory is lost.
2. **You want the stock firmware back.** SMK-37: M-VAVE's updater with the official package. FM-1: *Return to the official firmware* on its install page.
3. **An install was cut off, or the device restarts into update mode.** Reconnect USB, open your device's install page and press **INSTALL** again.
4. **The firmware doesn't start.** After two failed starts SLOOP opens a USB recovery screen where the installer can write a package again. On the FM-1 you can also hold **OCT -** while switching on (*SLOOP USB RESCUE*). After a third failed start the chip enters its built-in update mode, which software can't erase. On the SMK-37, restore with the port kit's tools and your full flash backup (check it contains `trimdat`). Read any low-level tool's instructions fully first.

**Reporting a problem.** Open an issue on [the project's GitHub page](https://github.com/davidpryan/learning-fm-smk/issues) with the device, the package name and its SHA-256, what you pressed, what the screen showed, and whether the installer still sees the device. Attach `provenance.json` from the release.
