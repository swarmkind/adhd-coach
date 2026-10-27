# Testing ADHD Coach

Coach is the app's working name, and Plans 1 to 4 (plus the hardware spike, Plan S) are the stages it was built in. Each stage's checks are kept below as they were run.

Two layers: host tests for the pure C core, and a walk-through in the emery emulator (headless, see `docs/dev.md`). Results below are from the Plan 1 pass on 2026-10-23.

## Host tests

```sh
make -C test
```

Covers time maths, the 256-byte record codec (items, steps, settings, day chunks, logs, run, history), next due and heads-up, Later / ignore / sweep / rollover, fire collection and the wakeup plan, the routine runner and fixed-point number stepping, and the storage ring.

```sh
make -C test js
```

Plan 4's phone side. `test/vectors.c` packs records with the watch's own C codec and writes `test/build/vectors.json`; the Node tests (`js/*.test.js`) decode those bytes with `src/pkjs/codec.js` and pack them back, so both sides agree byte for byte (UTF-8 cut points, a 920-byte note split across records, frames, day numbers). They also run the phone store and a whole sync session against a scripted watch: first-sync adoption, settings by newest edit, ACK only after storing, the busy retry, note edits cleared by identity, and a session cut off at every message. `pages.test.js` fills a phone with up to three years of heavy use and checks that the planner page stays under its URL limit, and that the exports split into parts that lose no note or row. `index.test.js` drives `src/pkjs/index.js` through a fake `Pebble` to check which page each Settings tap and each close opens.

## Emulator walk-through

Use a `COACH_DEMO 1` build. Hold Down on home for 1.5 s to place the routine and the dentist task a few minutes from now.

| Check | How | Expected | Result |
|---|---|---|---|
| Sample loads once | Fresh install, then relaunch | "sample schedule written: 5 items", then "schedule read: 5 items" | Pass |
| Last number survives relaunch | Save a weight, relaunch | "last 82.4 today" under the value box | Pass |
| Reminder alert | Drink water due | Blue screen, "13:31 · every hour", 8 pips, "0 of 8 today"; Select = done | Pass |
| Routine alert | Morning routine due | Amber, "4 steps", Select = start, Up = later | Pass |
| Task heads-up | Important task 10 min out | Rose, "in 10 min · 13:28", only "got it" | Pass |
| More menu | Hold Select on an alert | Done already / Pause until I resume / Later 60 min | Pass |
| Ignored alert | Leave it 60 s | Action IGNORED; a task gets one auto-Later | Pass |
| Reminder asks a number | 10 pushups, Select | Number screen in reps, save counts it done | Pass |
| Routine, every step type | Start the routine | Manual (marquee title), timed with pause/resume, auto-advance with 5 s undo, number with + / − | Pass |
| Number acceleration | Hold Up 3 s on Weight | 82.4 → 98.3 | Pass |
| Long step name | "Stretch hamstrings and hips" | Marquees, then rests with an ellipsis | Pass |
| Parked run | Relaunch mid-Plank | Resumes on Plank with the right time left | Pass |
| Done screen | Finish the routine | "Done", next item and time, "or: Tidy desk", closes after 6 s | Pass |
| Home states | Parked run, next item, all done | "paused · 3 of 4" + go; "in 15 min"; "All done for today" + tomorrow's first | Pass |
| Today | Open from home | Time order then anytime; started / done / missed / off today; Select opens actions | Pass |
| Wakeup with the app closed | Close, wait for the due time | App launches (reason 3) into the alert | Pass |
| No wakeup loop | Alert on screen | No wakeup re-armed for what is already showing | Pass (after fix) |
| Resume a pause | Today: Skip today on Drink water, then Resume | Row reads "off today", menu offers Resume first, then back to "1 of 8" (action 8 → status 0) | Pass |
| Today Done asks a number | Today: Done on 10 pushups | Number screen in reps; save counts it (count 1) | Pass |
| Start resumes today's run | Start, Leave on Plank, Start again from Today | Same step and time left; no second START | Pass |
| Crowded alerts | Several items due in the same few minutes | Important first; wakeups at least 60 s apart; never more than 8 armed | Pass |
| Release build | `COACH_DEMO 0`, fresh emulator (kill, wipe, install) | "sample schedule written: 5 items"; routine 07:00, water 09:00-21:00, pushups 10:00-20:00; wakeups armed for 15:00, 16:00, Monday 07:00 | Pass |
| Wakeup launch exits | Opened by a wakeup, answer the last alert | App exits (to the launcher in the emulator), remaining wakeups re-armed | Pass |

## Plan 2 walk-through (2026-10-23)

Each Plan 2 screen was checked on the emulator as it was built. Where a check needed a date other than today, it used a temporary demo patch, because emulator clock jumps don't hold (see `docs/dev.md`). Screens are in `docs/screenshots/plan2/`.

| Check | How | Expected | Result |
|---|---|---|---|
| Event alert, same day | Demo build: event at +69 min, hour-before rung at +9 min | Coral, "in 60 min · 16:58", Up later, Select got it, Down empty, Back closes; heads-up buzz | Pass |
| Event Later is clamped | Up on that alert | Wakeup and Today's "later 16:48": 10 min before the event, not 2 h on | Pass |
| Event alert, days ahead | Rung 7 days before | "in 7 days · Fri 10:30" | Pass |
| Event in Today | Rung day, then after Got it | "Fri 30 Oct · 10:30 · in 7 days", sorted by today's rung; then "got it" | Pass |
| Event More menu | Hold Select on the alert | Pause until I resume / Later 60 min (no "Done already") | Pass |
| Settings | Today, last row Settings; Later for tasks 30 → 20, relaunch | Grey list with values; number screen "20 min", Back = cancel; 20 kept after relaunch | Pass |
| Focus from home | Up on home | Green "Focus", "25:00" ("1:00" in the walk), "4 rounds of 25 min", Select start | Pass |
| Focus controls | Start, pause, resume, hold Select +5, Down | "Focus 1 of 4", "ends …", "paused"; +5 adds 5:00; Down goes to "Short break" | Pass |
| Home during Focus | Back to home | Green "break · 1 min left", "Focus", Select go | Pass |
| Focus wakes the closed app | Exit, wait for the phase end | Launch reason 3, fire kind 3, "Focus 2 of 4", next end armed | Pass |
| Focus behind other screens | Demo build: start Focus, hold Select, Lengths; let a phase end. Then let alerts arrive during Focus | Log "focus: next phase" on time while Settings, or the "Call the dentist" alert, stays in front; Focus shows the right phase ("Focus 3 of 4") once uncovered | Pass (after fix) |
| Back on an event alert | Demo build: Back on the hour-before alert | Alert closes: action 2 (skip), no Later wakeup armed | Pass (after fix) |
| Focus on this | Today, task menu, Focus on this, start, Stop | Label shown; "Mark Call the dentist done" / "Not yet"; Mark → task done | Pass |
| Launcher glance | Exit Coach, open the launcher | "Coach / Drink water · 17:00" | Pass |
| Release build | `COACH_DEMO 0`, kill, wipe, install | "sample schedule written: 6 items"; wakeups 17:00, 18:00, tomorrow 09:00 (water and the event's first rung), Monday 07:00 | Pass |
| Image size | `pebble build` | Under the 64 KB app image limit (41,624 bytes; steps table on the heap) | Pass |

## Plan 3 walk-through (2026-10-23)

Demo builds fake the dictation (a long transcript, a short one, then a connectivity failure, in turn). The release build was checked against real emulator dictation with `pebble transcribe`. Screens are in `docs/screenshots/plan3/`.

| Check | How | Expected | Result |
|---|---|---|---|
| Note from home | Demo build: Down on home, three times | "note saved" with status 0, 0, 1 (two transcripts, one missed); a tick, a tick, a double tick | Pass |
| Notes survive a relaunch | Reinstall, open Today | "Voice notes · 4 notes" | Pass |
| Link to the running item | Start the routine, Leave, Down on home | "link 1"; home still "paused · 1 of 4" | Pass |
| Voice log | Today, Voice notes | Newest first; "0:02 · Morning routine", "missed · 0:01"; lavender highlight | Pass |
| Long transcript | A 1,170-byte fake transcript | Saved at 920 bytes in four records; the note scrolls to its true last words | Pass |
| Missed note | Open it | "No transcript" and "phone not reachable" | Pass |
| Rename | Hold Select, Rename (dictates) | Title becomes "Garden ideas" | Pass |
| Delete | Hold Select, Delete, "Delete note" | Gone from the log; "Keep" is the other choice | Pass |
| Real dictation | Release build, `pebble transcribe "…"`, Down on home | System "Listening" screen, then a 39-byte note, no confirmation screen | Pass |
| Dictation error | `pebble transcribe --error connectivity` | Missed note, reason "phone not reachable" | Pass |
| Back while listening | No transcribe server, Down, then Back | "note cancelled"; nothing saved | Pass |
| Release build | `COACH_DEMO 0`, kill, wipe, install | "sample schedule written: 6 items"; no note reads at launch; image 52,880 bytes | Pass |

## Plan 4 walk-through (2026-10-23)

Release build (`COACH_DEMO 0`) on a watch carrying an earlier demo schedule. The planner was driven in headless Chrome by `tools/cdp.js` through `pebble emu-app-config` (see `docs/dev.md`). Screens are in `docs/screenshots/plan4/`.

| Check | How | Expected | Result |
|---|---|---|---|
| Sync on launch | Install, read logs | "sync: hello rev 1 …", "up 1 msgs", "ack 0 notes"; the log re-sent from its chunk is deduped on the phone | Pass |
| Planner opens | `emu-app-config` | "planner: opening, 54096 characters"; schedule, notes, history and settings tabs drawn from the phone's copy | Pass |
| Planner edits reach the watch | Rename "Drink water" to "Water, big glass", add "Stand up" every 45 min until 17:00, Focus one step up, rename a note, Save | Phone "planner: saved, schedule rev 2, 1 note edits"; watch "schedule rev 2 7 items applied", "ops 1" | Pass |
| Watch shows the edits | Home, Today, Settings, Voice notes | "Water, big glass" on home; "Stand up 09:00-17:00" on Today; Focus 6 min; note titled "Morning routine idea"; "Phone sync · today 18:53" | Pass |
| Busy watch | Start the routine, Save from the planner | "schedule rev 2 6 items busy", phone "trying again in 30 s"; still busy under the done screen; applied on the first retry with Today on top | Pass |
| Alert holds a push | Fresh watch, a sample alert on screen | Busy until the alert is answered, then applied on the next retry | Pass |
| Watch wiped | Phone at rev 5 with 7 items; delete the emulator flash, boot, install | Watch "sample schedule written: 6 items", sends rev 0; phone "sending schedule rev 5, 7 items"; applied | Pass |
| Phone wiped | Exit Coach, delete the phone's localStorage, install | Phone "adopted 7 items rev 1" and the watch's one note; watch takes rev 1 from the ACK | Pass |
| Corrupt phone storage | A key that is not JSON | Read as empty, no crash (the emulator produced one, see `docs/dev.md`) | Pass |
| Note recorded while open | Down on home with Coach open | Saved on the watch; reaches the phone at the next launch or planner save | Pass |
| Planner with a year of notes (after the final review) | 1,100 older notes added to the phone's store | "planner: opening, 338733 characters"; Notes says "These are the newest 747. The phone keeps 357 older ones too" | Pass |
| Full export in parts | Export all notes on that phone | "export: notes part 1, 323704 characters"; the page reads "part 1 of 2" with Next part; part 2 ends on the newest note; Done, then Settings opens the planner again | Pass |
| Export the phone app cannot show at once | The emulator has no listener for a second page | The next Settings tap opens the waiting export | Pass |
| Dictation that never answers | Temporary, uncommitted patch that skips `dictation_session_start` | Before the fix the next Down did nothing; after it, "dictation: no answer in 180 s", one missed note, and the next Down records again. Without the patch a note still saves once and nothing follows 180 s later | Pass |
| Today under a shrinking push | Today open on its last item row, planner deletes four items | Selection lands on Settings, and Select opens Settings | Pass |

## Focus setup and Coach as the clock (2026-10-23)

Added after Plan 4. Checked on the emulator with the release build; screens were looked at, not kept.

| Check | How | Result | Verdict |
|---|---|---|---|
| Setup screen | Up on home, no session running | "Focus", minus and plus either side of the focus minutes and of "min break", "4 rounds"; rail plus, start, minus | Pass |
| Off-grid length snaps | Focus left at 6 min by an earlier walk; Up, Up, Down | 10, 15, 10 | Pass |
| Held at the limit | Down at 5 min | Stays 5 | Pass |
| Start uses the lengths | Select | "Focus 1 of 4" with the chosen length; Up shows nothing while running | Pass |
| A tap in the emulator | VNC pointer taps | Arrive only as touchdown and liftoff at 0,0 with no position, so nothing changes (see `docs/dev.md`); the tap logic is host-tested | Watch-only |
| Back on home | Back | Home stays | Pass |
| Exit Coach | Hold Select, Exit Coach | The watch face | Pass |
| Today from the menu | Hold Select, Today | Today opens | Pass |

## Touch and sounds (2026-10-23)

Added after the Focus setup screen. The emulator's touches carry no position (see `docs/dev.md`), so the rail checks used a temporary, uncommitted patch that gave each touch the next point from a scripted list, through the real touch path.

| Check | How | Result | Verdict |
|---|---|---|---|
| Up rail on home | Tap at 180,52 | Focus opens | Pass |
| Focus row | Tap at 140,100 | 5 min becomes 10 | Pass |
| Hold the Select rail | Touch held 900 ms at 180,114 on Focus | The Lengths menu opens, and no session starts | Pass |
| Select rail on home | Tap at 180,114 | Today opens | Pass |
| Routine steps | Select rail on "Drink a glass of water", then on Weight: Up, Up, Down, Select | Step done; 30.0, 30.2, 30.1 kg; saved and on to Stretch; no crash with a tap's empty recognizer | Pass |
| A system screen on top | Taps while "While your Pebble was off…" was showing | Ignored by Coach | Pass |
| Sounds switch | Settings > Sounds, Select twice | "on", "off", "on"; no "sound: not played" in the log. The emulator has no audio, so the tones themselves are watch-only | Pass |

## Alarms (2026-10-25)

Walked on the emulator at midnight, buttons only. The emulator has no audio and its touches carry no position, so the tones and taps are watch-only checks.

| Check | How | Result | Verdict |
|---|---|---|---|
| Find it | Hold Select on home | Today, Alarms, Exit Coach; Alarms shows "New alarm" | Pass |
| Add | New alarm, Down 7 on the hour, Up 3 on the minutes, Down on the days, Select | "00:03 Once" in the list; wakeup armed at 00:03:00; bell and 00:03 beside the date | Pass |
| Ring with Coach open | Wait for 00:03 | Ring screen; the minute tick and the wakeup both fired it, one ring screen | Pass |
| Snooze | Up | Wakeup re-armed at 00:13:16; the clock's bell reads 00:13 | Pass |
| Ring with Coach closed | Exit Coach, set the clock to 00:13:05 | Launch reason 3 (wakeup), ringing | Pass |
| Stop | Down | Coach exits; no alarm wakeup left; the list reads "Once · off" | Pass |
| Change | Select on the row, Change, days to Weekdays | "00:03 Weekdays"; editor underlines only the digits in focus | Pass |
| 12 h style | `emu-time-format --format 12h` | Editor "12:03 am", list "12:03 am", clock bell "12:03" | Pass |
| Rings stop on their own | Temporary patch: 3 rings instead of 200; an every-day alarm at 00:08 | Rang 00:08:00, stopped itself 00:08:09, still on, re-armed for the next day; patch removed | Pass |
| Delete | Select on the row, Delete | Empty list; wakeup gone; no bell on the clock | Pass |
| Upgrade from 0.7.0 (0.8.0) | Save 07:00 every day with the 0.7.0 build, install 0.8.0 | "07:00 / Every day · sound" | Pass |
| Sound choice (0.8.0) | Change, Select to the fourth field, Down | "Sound + vibrate" becomes "Vibrate only"; Save; "Wednesdays · vibrate · off" fits the row | Pass |
| Vibrate-only ring (0.8.0) | Once, vibrate only, two minutes ahead | Rang at 00:18:00; Down stopped it | Pass |

## Readability sweep

Every screen from the scripted walk is in `docs/screenshots/plan1/`, in walk order: heads-up, home, Today, Today actions, routine alert, each step type, the More menu, parked home, number entry, the long step name mid-marquee, task alert, done screen, home after.

- Smallest text is Gothic 14 Bold (rail labels, step counter, pips caption). Everything else is 18 or larger.
- Nothing is clipped. The only cut text is a marquee mid-scroll, and the last Today row cut by the screen edge (it scrolls).
- Plan 2: smallest text is still Gothic 14 Bold. Event, Focus and settings sub lines are Gothic 18. "Fri 30 Oct · 10:30 · in 7 days" fits a Today row. A long event name wraps onto two lines at 28 ("Dentist check / -up") rather than stepping down to 24; it reads, but a smarter break is a deferred minor.
- Rail labels needed room. At 34 px, "today" touched the rail's left edge and sat 2 px from the screen edge. The rail is now 38 px: labels clear both sides by 3 px, and the text column keeps an 8 px gap.

## Watch-only checks (Plan S)

- Wakeups when another app is in front, and after a reboot.
- Hold Back on an open alert. In the emulator it arrives as Back on release (Later, or seen for a heads-up); check the watch does not exit first.
- A routine parked yesterday: today's Start begins a fresh run (host-tested; the emulator cannot hold a date change).
- Buzz strength by priority on the real LRA motor.
- Readability in daylight on the reflective screen.
- Midnight rollover with the app closed overnight. The emulator resyncs its clock on every connection, so host tests cover rollover instead.
- A wakeup that lands past the catch-up window marks the item missed (host-tested; the emulator cannot hold a jumped clock).
- Voice notes (Plan 3): with Coach set under Settings > Quick Launch > Hold Down, hold Down on the watch face: Coach should go straight to listening, save with a tick, and exit to the watch face. With the phone locked, the transcript should still arrive (Gravel's local whisper). With Bluetooth off, a missed note with "phone not reachable".
- Phone sync (Plan 4): open Settings in Gravel for Coach. The planner should open from its bundled page with the network off, show the real schedule and notes, and Save should come back to Gravel (the emulator needed a one-line pypkjs fix for that, see `docs/dev.md`). Saving with the phone locked, or with Coach closed on the watch, should apply the next time Coach opens.
- Sync with the phone locked: open Coach with the phone in a pocket; the "Phone sync" row in Settings should read "today HH:MM".
- Data size: the planner is one data: URL kept under 400,000 characters. With a lot kept on the phone it carries the newest notes, logs and 60 days, and an export of the rest opens as its own page, in parts. Check Gravel opens the planner, and opens the export page straight after the planner closes. If it doesn't, the next tap on Settings shows it.
- Skip an alert with Down and check that no voice note starts on the home screen underneath. Once in the emulator a note started 3 s after a Down on an alert; a second try did not repeat it.
- Focus taps: on the setup screen, tap the right half of the focus minutes and it should go up 5; the left half down 5; the break row the same in 1 min steps. A drag across a row should change nothing. If taps do nothing at all, say so: the code reads raw touch events, and the emulator could not show their positions.
- Touch in lists: Today, the voice log, Settings and the menus should scroll by swiping and open a row by tapping. If they don't, check the watch's own touch setting is on: Coach opts in, but the system decides.
- Touch on the rail: tap each rail icon on home, an alert, a routine step and a transcript; each should act like its button. Hold the Select rail for half a second to get the hold menu. Watch for accidental taps from a sleeve on an alert.
- Sounds: each cue should be audible and tell itself apart: alert (two notes; three for important; a run up, twice, for critical), heads-up (two short and a long), reminder (one soft note), saved (a tiny blip), missed note (two falling), Focus (a two-note chime; a run up at the end). No sound with Sounds off, with the watch muted, or in Quiet Time.
- Alarms: set one a few minutes ahead and let it ring. Four beeps and a long buzz every 3 s, with the light on; Up or Back snoozes 10 minutes, Down stops. Check it is loud enough to wake you and rings in Quiet Time. Set one to Vibrate only: it should buzz with no beeps. A sound alarm should beep even with Coach's Sounds switch off; with the watch muted, the editor should say so. Then test it with Coach closed (Exit Coach first): it should open Coach, ring, and leave after Stop. In the editor, a tap on the hour, the minutes or the days should underline it; a tap on the rail's snooze should snooze, and a tap on stop should do nothing.
- Battery: Coach keeps the touch sensor listening while it is open. Compare a day with Coach left up as the clock against the usual drain.
- Coach as the clock: leave Coach open on home for a few hours. It should still be there, unless something else took over (a reboot, another app). Check whether holding Back on home leaves Coach anyway; if it does, that is the system's own exit and fine. Set Quick Launch hold Up to Coach so home is one hold away after a reboot.
- Quiet Time (Plan 2): with it on, a reminder slot passes with no screen and no buzz, the log reads `quiet: skip item N`, and Today shows it neither counted nor missed. Events, tasks and routines still alert. The emulator has no Quiet Time switch.

## Timer steps and battery bar (1.1.0, 2026-10-25)

Release build on the emulator, screens checked by VNC grabs.

| Check | Steps | Expected | Result |
|---|---|---|---|
| Timer waits | Start the sample routine, reach Plank | 1:00, "Select to start", "start" on the rail; still 1:00 five seconds later | Pass |
| Start, stop, again | Select, wait, Select, Select | Counts down with "ends 07:03" and "stop"; "stopped" holds the time; carries on from it | Pass |
| Time up | Start Plank, wait 61 s | Moves to Weight (3 of 4) with "undo" for 5 s, then the number rail | Pass |
| Battery bar | `pebble emu-battery --percent` 80, 40, 20 | Four blue bars, two blue, one red | Pass |
| Finish beeps | Sounds on, watch not muted | Three beeps with a firm buzz | Not checkable in the emulator: wrist check |

## Focus reset, relative alarms, battery shape (1.2.0, 2026-10-27)

| Check | Steps | Expected | Result |
|---|---|---|---|
| Focus reset | Start Focus, wait, Up | "reset" at top right while running; 24:57 back to 25:00, paused | Pass |
| Time now | Hold Select, Alarms; New alarm, Custom | "Now 15:18" heads the list; "now 15:27" top right in the editor; the list's time moves on each minute | Pass |
| In N min | New alarm | In 5/10/15/30 min and In 1 hour, each with its ring time, then Custom | Pass |
| In 5 min rings | Pick In 5 min, jump the clock 285 s | A 15:23 "Once · sound" row; rings as Alarm 15:23 Once; Stop removes it from the list | Pass |
| Battery shape | `pebble emu-battery --percent` 80, 20 | Grey outline with a terminal; four bars in the home colour; one red bar | Pass |

## Voice note to-dos (1.3.0, 2026-10-27)

| Check | Steps | Expected | Result |
|---|---|---|---|
| Hold menu | `pebble transcribe "Call the bank about the blocked card before Friday"`, Down on home; Today, Voice notes, hold Select | In 30 min · 16:17, In 1 hour · 16:47, Tomorrow · 09:00, Add to Today, Rename, Delete | Pass |
| In 30 min | Pick it | Tick; the log row reads "0:05 · to do"; Today lists "Call the bank about the", "note · 16:18" after Tidy desk | Pass, after a fix: the time is now taken when the menu opens (it had moved a minute) |
| Reminder | Jump the clock to 16:19:10 | Purple alert "note · 16:18", later / done / skip | Pass |
| Done | Select | The row leaves Today | Pass |
| Today menu | Add to Today, Select on the row | Done, the three times, Open note; no "No reminder" until it has a time | Pass |
| Open note | Pick it | The transcript, Back returns to Today | Pass |
| New time | In 1 hour; Select again | "note · 16:50"; the menu now has No reminder | Pass |
| Tomorrow | Pick it | "note · tomorrow 09:00" | Pass |
| Next morning | Jump to 09:00:10, Back on the system notice, skip Drink water | "note · 09:00" alert; Later gives "note · 09:30" | Pass |
| Six at most | Six notes as to-dos, hold Select on a seventh | "To-do list full" | Not walked: host-tested in `test_todo.c` |
| No answer | Leave the alert | One automatic Later, then it stays in Today quietly | Not walked: host-tested in `test_todo.c` |

## Pick day and time (1.4.0, 2026-10-27)

| Check | Steps | Expected | Result |
|---|---|---|---|
| Survives the update | Install 1.4.0 over 1.3.0 | The to-do still reads "note · tomorrow 09:30" (to-dos and the day now load onto the heap) | Pass |
| In both menus | Select on the to-do in Today; hold Select on its note | "Pick day and time" after the three quick times | Pass |
| Starts from the reminder | Pick it | "Tomorrow", 09:30, day underlined, "now 16:11" | Pass |
| Day, hour, minutes | Up 3; Select, Up 5; Select, Up 1 | "Sat 31 Oct", "in 4 days"; 14:30; 14:35 with "save" on the rail | Pass |
| Saved | Select | "note · Sat 14:35" in Today | Pass |
| A week or more | Again, Up 10, Select 3 times | "note · Tue 10 Nov 14:35" | Pass |
| Not in the past | Again, Down 16 | "Today", 16:15, "in 3 min" (now 16:12) | Pass |
| It rings | Save, jump to 16:15:10 | "note · 16:15" alert; Later gives "note · 16:45" | Pass |
| Back cancels | Voice log, Pick day and time, Back | The note still "0:05 · to do" at 16:45 | Pass |
| Alarm editor unchanged | Alarms, 07:00, Change; Select | "< Alarm", "now 16:14", 07:00 with the hour then the minutes underlined | Pass |
| Rail taps | Tap +, next, - on the picker; tap the day or the time | Steps, moves on, underlines what was tapped | Wrist check: the emulator sends no touch positions |


## Size refactor (1.4.1, 2026-10-27)

An A/B walk: the same emulator snapshot and clock (Wed 28 Oct 22:30:05), the same 48 scripted screens through VNC, 1.4.0 against the new build, compared pixel by pixel. The walk covers home, Focus, the home menu, Today and its to-do menu and picker, the voice log and note menu, settings, a routine item menu, the alarm list, editor and new-alarm menu, an alarm ring, a to-do alert, the Focus menu, and a routine run to its Done screen.

| Check | Expected | Result |
|---|---|---|
| Harness repeats | Two walks of 1.4.0 match | 46 of 48; the alarm ring and a marquee frame caught at different moments, so the marquee waits went up and the ring is compared allowing for its slide |
| Shared drawing (no LTO), 63,475 bytes | Every screen matches 1.4.0 | 48 of 48 |
| LTO, first try | Installs | Failed: LTO dropped the app header and the build still passed. Fixed in `wscript`; the build now checks for it |
| LTO, 59,936 bytes | Every screen matches 1.4.0 | 47 of 48 exact; the alarm ring matches with a 2 px shift, the window caught at a different point of its auto-stop slide (two 1.4.0 walks differ the same way) |
| Stack, LTO | Room left after the walk | 1,744 B used below `main` at the deepest, 2,180 B never touched |
| 1.4.1 | Installs over the snapshot | Home matches 1.4.0 pixel for pixel |
| Host tests | All pass | C 810 of 810, JS 50 of 50 |
