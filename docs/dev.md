# Developing ADHD Coach

The app was called Coach while it was built, so the code and these notes still say Coach. It was built in stages, Plans 1 to 4 plus a hardware spike (Plan S); the notes name them where it helps.

Toolchain: `pebble-tool` 5.0.40 (installed with `uv tool install --python 3.13 pebble-tool`), SDK 4.33.1 (`pebble sdk install latest`). Target platform: `emery` (Pebble Time 2).

## Host tests (pure C in `src/c/core/`)

```sh
make -C test            # builds src/c/core/*.c + test/test_*.c, prints "N passed, M failed"
make -C test js         # C-packed vectors, then node --test over js/*.test.js (needs Node 22+, as does tools/cdp.js)
```

## Build and emulator (headless)

Always pass `--emulator emery --vnc`: `--vnc` keeps QEMU headless (no window on the desktop).

```sh
pebble build
pebble install --emulator emery --vnc                       # starts the emulator if needed, installs, launches
pebble logs --emulator emery --vnc                          # streams APP_LOG output (Ctrl-C to stop)
pebble screenshot --emulator emery --vnc --no-open out.png  # 200 x 228 PNG
pebble emu-button --emulator emery --vnc click select       # back | up | select | down
pebble emu-button --emulator emery --vnc push up            # hold down …
pebble emu-button --emulator emery --vnc release up         # … and let go (long press / repeat tests)
pebble emu-button --emulator emery --vnc click down -n 5 -i 150   # repeated clicks
pebble emu-set-time --emulator emery --vnc 06:58:00         # set the watch clock (today); or Unix UTC seconds
```

To capture launch logs: start `pebble logs` in the background (e.g. `timeout 8 pebble logs … > log.txt &`), then install.

Screenshots use the emulator's colour correction by default; add `--no-correction` to get the raw 64-colour palette values.

## Facts measured on the emulator

- `persist_get_max_size()` = **1048576** bytes (1 MB).
- App heap available: about 130 KB (build report: "Free RAM available (heap): 130349 bytes").
- Back from the app returns to the system watch face.

## Touch in the emulator

The emery QEMU has a touch device, but pebble-tool has no command for it and its control protocol has no touch message. Pointer events sent to the VNC port (5901 with `--vnc`) reach the app as a touchdown and a liftoff at 0,0, with no position updates. The SDK's tap recognizer never fired on them. So Coach builds its taps from raw `touch_service` events (`cc_tap_feed` and `cc_rail_zone` in `cc_touch.c`, host-tested), and real taps are a watch-only check.

`src/c/ui/railtap.c` holds the one touch subscription. Each rail window registers its Up, Select, Down and long-Select handlers (called with a NULL recognizer, so a handler that reads it must check) and optionally a body tap; a tap on the rail calls the handler beside it, and a 500 ms hold on the Select rail the long one. The registry holds `MAX_WINDOWS` (12; ten windows register as of 1.4.0, and 1.3.0 had nine against a cap of eight). A window past the cap silently gets no taps, so raise it when adding one. Registered windows turn the system's touch bridge off, except the transcript, whose ScrollLayer scrolls by swipe through it. `main.c` calls `app_touch_navigation_enable(true)`, which is what lets lists and menus scroll by touch at all: third-party apps are opted out by default.

To walk touch on the emulator, patch `on_touch` for the walk to replace each event's position with the next point from a list, and send taps to the VNC port with any small RFB client (touchdown is button 1 down, liftoff is button up). Remove the patch afterwards.

## Sounds

Every cue in `src/c/platform/buzz.c` is a vibration plus a short `speaker_play_notes` tune, skipped when Settings > Sounds is off (`K_SOUNDS`, watch-only, not synced), `speaker_is_muted()`, or `quiet_time_is_active()`. A failed play logs "sound: not played". The emulator accepts the calls but has no audio.

## Alarms

`cc_alarm.c` is the pure part, host-tested: the next ring, set, stop, snooze, and the day choices. `wake.c` adds each alarm's next ring to the fires (`CC_FIRE_ALARM`, id = its index), so the minute tick and the wakeups treat it like any other fire; a due alarm skips the Quiet Time check. `alarm_win.c` rings, `alarms_win.c` is the list and the editor. Alarms are watch-only and stored as the raw struct under `K_ALARMS` (47). `cc_alarms_read` takes this version's struct or 0.7.0's, whose 12-byte alarms are a prefix of today's (the mode was added at the end and reads as sound). Add new fields at the end too, and teach it the old size, or saved alarms read as none. A second alarm due while one rings waits for the next minute tick.

Note to-dos work the same way. `cc_todo.c` is the pure part, host-tested: add (naming from the transcript), answers from the alert, the fires and the coming morning. `wake.c` adds each reminder to the fires as `CC_FIRE_NOTE` (id = the to-do's id) and answers through `todo_done`; `alert_win.c` dresses the to-do as a task for its screen. They are watch-only, stored as the raw 244-byte struct under `K_TODOS` (48), so a change to `CcTodo` must keep reading the old size or saved to-dos read as none. The menus are in `voice_win.c` (the note log) and `today_win.c` (the rows), sharing `voice_todo_at` and `voice_todo_label`; both open `when_win.c`, the day-and-time picker, whose steps and limits are `cc_todo_pick`. It shares its head line and big clock with the alarm editor through `poster_draw_editor_head` and `poster_draw_editor_clock`.

## Dictation in the emulator

`pebble transcribe "some words" --emulator emery --vnc` answers the emulator's next dictation request with that text; `--error connectivity` (or `disabled`, `no-speech-detected`) answers with a failure. Start it, then press Down on home. With no server running, the system "Listening" screen waits about 8 s and then fails with a missed note; press Back inside that window to test Back-while-listening. On the watch, a session that gives no answer at all ends after 180 s as a missed note (`DEADLINE_MS` in `voice.c`), so a lost callback can't block later notes. The emulator never gets there, because it fails first; the check used a temporary patch that skipped `dictation_session_start`.

Demo builds don't need it: `COACH_FAKE_DICTATION` (on whenever `COACH_DEMO` is) skips the system session and, 1.5 s later, returns in turn an over-long transcript (clipped to 920 bytes), "Buy oat milk" (or "Garden ideas" for a rename), and a connectivity failure.

Voice notes live in persist keys 6000 + slot × 4 + part (100 slots, a header and up to three parts each), tombstones of deleted synced notes in key 44.

## Things the emulator taught us

- Every `pebble` connection that talks the Pebble protocol (`install`, `logs`) **resyncs the watch clock to the Mac**. `emu-set-time` sticks only until the next such connection, so time-jump tests run without a log stream: set the time, drive buttons, read the result from screenshots.
- Jumping the clock past armed wakeups makes the firmware show "While your Pebble was off wakeup events occurred for: Coach" instead of launching the app.
- Under zsh, `$E="--emulator emery --vnc"` does not word-split; wrap commands in a function: `p() { pebble "$@" --emulator emery --vnc; }`.
- A button press sent while the app is still launching lands on the launcher. Wait about 3 s after `install`.
- `launch_reason()` is 3 (`APP_LAUNCH_WAKEUP`) when a wakeup opened the app, 1 from the launcher, 2 straight after `pebble install`.
- `pebble screenshot` resyncs the clock too, so a jump made with `emu-set-time` is undone by the next screenshot. Jumping past armed wakeups also brings up "While your Pebble was off…". For other-day checks, patch the demo sample instead.
- The app image (.text + .data + .bss) must stay under 64 KB: `PebbleProcessInfo.virtual_size` is 16-bit, and the build fails past 65,535. Big tables go on the heap: `g_steps`, `g_items`, `g_day`, `g_todos`, the last-number table and the store's persist buffer are allocated in `store_load`; the notes slot index, persist buffer and scratch transcript on the first notes call; the rail-tap registry with the first window; and the transcript buffers in `voice.c` and `voice_win.c` live only while used. With the reminder picker the image reached 65,116 bytes. 1.4.1 brought it to 59,936: list rows, action menus, clipped left-aligned text and glyph lines are shared in `poster.c` (`poster_draw_row`, `poster_open_menu`, `poster_text`, the `STROKES` table), and `wscript` builds with link-time optimisation (`-flto=auto`), about 3.5 KB of that. LTO drops whatever nothing refers to, and nothing refers to the app header (`__pbl_app_info`), so `wscript` names it with `--undefined`, and its `check_header` fails the build if `pebble-app.bin` does not start with `PBLAPP`; without it the build passed and the emulator refused the install. Use the helpers rather than copying their bodies. A glyph's lines are one string per glyph in `Glyph` order: a byte is a point, x << 4 | y with x from 1, joined to the one before; 1 lifts the pen, 2 or 3 sets the stroke width. A new glyph needs its string at its enum position. A buffer that moves to the heap must lose every `sizeof` of it, which would now measure a pointer. `arm-none-eabi-size build/emery/pebble-app.elf` (in the SDK's toolchain folder) gives the total and `arm-none-eabi-nm --size-sort -S` the largest functions; with LTO the `.o` files hold bytecode, so drop the two LTO lines from `wscript` for per-file sizes.
- Gothic has no "×"; it draws a box.
- `pebble wipe` does not reset a running emulator: its flash lives in the QEMU process. Run `pebble kill`, then `pebble wipe`; the next command boots a fresh watch. Start the fresh boot with one command (e.g. `pebble logs`) and wait before `install`, or two boots race for the VNC port.

## Phone sync and the planner (Plan 4)

The phone side is `src/pkjs/`: `codec.js` (the record and frame formats, byte for byte the C ones), `store.js` (the master copy in localStorage), `sync.js` (one sync session), `pages.js` (the planner and export pages as data: URLs), `index.js` (Pebble events). The watch side is `src/c/platform/sync.c` over `src/c/core/cc_sync.c`.

Message keys `CMD`, `DATA` (bytes), `REV` (uint32), `ARG` (int32); `app_message_open(1024, 1024)`. `DATA` holds frames `[type u8][len u16 LE][record]`, at most 900 bytes a message. Frame types: 1 item, 2 step, 3 settings, 4 log chunk, 5 history day, 6 note head, 7 note part, 8 tombstones, 9 rename, 10 delete. The phone drives:

| # | CMD | From | Contents |
|---|---|---|---|
| 1 | HELLO | phone | `[phone_id u32][rev u32][log_utc u32][hist_day i32]` |
| 2 | UP | watch | frames: the schedule (only when the phone's rev is 0), settings, logs from `log_utc`, days after `hist_day`, unsynced notes (all of them for a new `phone_id`), tombstones |
| 3 | UP_END | watch | REV watch rev, ARG notes sent |
| 4 | ACK | phone | DATA note ids stored, ARG tombstones consumed, REV the adopted rev or 0 |
| 5 | SCHED | phone | frames items, steps, settings; ARG 1 on the first message |
| 6 | SCHED_END | phone | REV, ARG item count |
| 7 | OPS | phone | frames rename and delete |
| 8 | DONE | watch | 1 applied, 0 busy, -1 invalid |

The phone pushes its schedule whenever the two revs differ, and adopts the watch's when its own rev is 0 (first sync, or cleared storage). A push commits only with home or Today on top (or no window); otherwise the watch answers busy and the phone tries again every 30 s. Either side gives up after 12 s of silence. Settings carry the time they were last edited; the newer side wins.

Phone localStorage keys: `meta` (phone id, last sync), `sched` (rev, nextId, items, settings, settingsUtc), `logs`, `hist` (365 days), `notes`, `ops` (note renames and deletes waiting for the watch). Watch persist key 45 is the sync record.

The planner is `planner/` (`index.html`, `planner.css`, `model.js`, `app.js`). `tools/build-planner.js` inlines it into `src/pkjs/planner_page.js` and copies the model to `src/pkjs/planner_model.js` (both generated and git-ignored; the wscript runs it before every build, `make -C test js` too). PebbleKit JS injects the phone's data as `window.COACH` and opens the page as a `data:` URL, so it needs no network. Save returns the result through `return_to` (the emulator tool) or `pebblejs://close#`.

Phone apps cap URL length (Chromium at 2 MB; an Android Intent near 1 MB, two bytes a character), so `pages.js` keeps every page under 400,000 characters. The planner gets the newest notes and logs that fit a byte budget and the 60 days its history shows; `more` counts what stayed behind. An export of everything (`{"export": "notes" | "numbers" | "days"}` in the close response, with the edits saved first if there are any) opens the same page in export mode, split into parts cut between notes or CSV rows, with Next part and Done. The page waits in `pending` until it closes, so if the phone app cannot open a page straight after another one closes, the next Settings tap shows it.

To look at the page in a browser: `node tools/build-planner.js --preview /tmp/planner.html` writes it with sample data.

### Driving the planner in the emulator

`pebble emu-app-config` opens the page with the `BROWSER` program. `tools/planner-browser.sh` hands it to `tools/cdp.js`, which drives headless Chrome over the DevTools protocol (no window) through a JSON list of steps: `{"until": "js"}`, `{"click": "css"}`, `{"type": ["css", "text"]}`, `{"eval": "js"}`, `{"log": "js"}`, `{"shot": "name.png", "full": true}`, `{"wait": ms}`.

```sh
PLANNER_STEPS=steps.json PLANNER_OUT=out BROWSER=$PWD/tools/planner-browser.sh \
  pebble emu-app-config --emulator emery --vnc
```

Screenshots and `cdp.log` land in `PLANNER_OUT`. Start with `{"until": "!!window.CoachPlanner && location.search.indexOf('return_to') >= 0"}` and end with a click on `[data-act=save]` and a short wait.

pebble-tool 5.0.40 drops the planner's Save on the floor: pypkjs hands the result to JavaScript as bytes, and its JSON bridge throws before `webviewclosed` fires. For the walk, `pypkjs/runner/websocket.py` in `do_config_ws` needs `result = result.decode('utf-8')` after the `struct.unpack_from` line. Real phones are not affected.

### What the sync walk taught us

- Killing the emulator loses data on both sides. The watch's flash lives in the QEMU process, and the phone's localStorage is a `dbm.dumb` file whose index is only written when Coach's JavaScript closes. pebble-tool also kills and restarts QEMU on its own now and then. Exit Coach (Back on home) before `pebble kill`.
- To wipe only the phone: exit Coach, delete `~/Library/Application Support/Pebble SDK/4.33.1/emery/localstorage/<uuid>.*`, install. To wipe only the watch: `pebble kill`, delete `qemu_spi_flash.bin` from the same folder, boot with `pebble ping`, start `pebble logs`, then install.
- `pebble ping` leaves a "Ping" notification on the watch; Back clears it.

## Demo build (`COACH_DEMO 1` in `src/c/config.h`)

- On the home screen, **hold Down for 1.5 s** to reset the demo: the sample schedule is rewritten with the routine 3 min and the dentist task 6 min from now, the day state is cleared and any run is dropped.
- Later is 2 min for tasks and reminders (30 and 10 in a normal build), so ignore → auto-Later → missed plays out in minutes.
- Focus phases are 1 min each, and the sample event "Dentist check-up" sits 69 min out with one hour-before rung, which alerts 9 min after the reset.
