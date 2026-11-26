# HealthWatch UI

This page describes HealthWatch, the app in the `ota_1` partition (`firmware/src/hw/`). The launcher menu in `ota_0` is the upstream M5StopWatch-UserDemo with an added HealthWatch entry and is not covered here.

All on-screen text is Japanese. The screen is a 466 x 466 round AMOLED: black background, white eyes, the owner's words in light blue, errors in pink.

## Flow

1. On start the watch shows small eyes, `今日はどこを鍛える？` and `考え中…`, and sends `start` once Wi-Fi is connected.
2. The agent suggests an area to train, and its reply replaces the opening line. The reply is spoken and shown.
3. The owner holds the yellow key and answers. The agent either suggests again or sends a menu.
4. On the menu screen the owner can talk again to change the menu, or tap the blue key to start.
5. The watch runs the menu item by item without the network, stopping on the next-item screen between items.
6. When everything is done the watch shows the done screen and sends `finish`, which saves the menu.

## Screens

| Screen | What it shows |
|---|---|
| Suggestion | Small eyes (20 % size), the agent's reply in white (up to 7 lines; past 5 lines the spacing tightens; longer text ends with `…`), and what the agent heard in light blue (up to 2 lines) |
| Menu | The area in yellow with `N周` (only when there are items to repeat), the items with their amounts (`N回`, `N分` or `N秒`), `最後に` and the items done once at the end, and a blue dot with `準備ができたらスタート` |
| Rep item | `N周目` or `最後に`, the item name, the big count with `/ target`, `次は<item>` or `これで終わり`, and a yellow ring that fills with the count |
| Timed item | The item name, the interval (`ややきつい` in orange or `楽` in grey, with `N本目`), the big elapsed time `mm:ss`, the time to the next change (`ややきつくするまで`, `楽にするまで`, `次の区間まで` or `終わりまで`), and an orange ring that fills with the time |
| Next item | `N周目` or `最後に` with `i / n種目`, `次は`, the item name, its amount in yellow, and `準備ができたらスタート` |
| Done | The eyes (70 % size), `おつかれさま！`, and the area, rounds and total time |

The menu list shows up to 8 rows. A longer list scrolls by dragging on the screen, with a bar on the right.

Items with sides are listed by the agent as separate items, for example `リバースランジ（右）` and `リバースランジ（左）`.

### Status line

On the suggestion and menu screens, one line under the content shows the state:

| Line | When |
|---|---|
| Level bar and `聞き取り中` | While recording |
| An error in pink | After a failed request |
| `考え中…` | While a request is in flight; the eyes look up and to the right |
| (nothing, or `準備ができたらスタート` on the menu) | Otherwise |

### Errors

| Message | Trigger |
|---|---|
| `Wi-Fiにつながってないよ` | Wi-Fi is not connected 3 s after start-up or a restart, or a request found no Wi-Fi within 5 s |
| `ログインできなかったよ` | Cognito sign-in failed |
| `エージェントにつながらないよ` | The runtime did not answer, answered with an error, or timed out |
| `返事を読めなかったよ` | The response could not be parsed or had no usable menu |
| `送れる大きさを超えたよ` | The request body did not fit the buffer |
| `保存できなかったよ` | `finish` failed (shown on the done screen) |
| `メモリが足りないよ` | The PSRAM buffers could not be allocated at start-up; nothing is sent |

Audio that arrived before an error is not played.

## Controls

| Input | Screen | Action |
|---|---|---|
| Yellow key, hold | Suggestion, menu | Records while held and sends the recording on release. Recordings shorter than 0.4 s are not sent. Recording stops after 20 s. |
| Yellow key, press | Suggestion, before the first answer | Sends `start` again |
| Yellow key, press | Done, after `保存できなかったよ` | Sends `finish` again |
| Blue key, tap | Menu, next item | Starts the (next) item |
| Blue key, hold | Any | Back to the opening screen with a new session and `start` again. A workout in progress is not saved, and a request in flight is ignored when it returns. |
| Yellow and blue keys, hold together for 1 s | Any | Leaves HealthWatch for the launcher. A recording in progress is dropped. |
| Drag up or down | Menu | Scrolls a list longer than 8 rows |

While both keys are down, the single-key actions are skipped until both are released. The blue hold threshold is the M5Unified default of 500 ms.

## Workout

| Item | Behaviour |
|---|---|
| Rep items | The count goes up by one every 3 s and is read out (1 to 50). |
| Timed items | Elapsed time like a stopwatch. At each change of level the watch says `ややきつくして` or `楽にして`. |
| Start of an item | `スタート`, and for a timed item the first level |
| End of an item | For a rep item the last count, then `次の種目` and a 60 ms vibration, and the next-item screen |
| End of the workout | `おつかれさま！` and a 400 ms vibration, then the done screen |
| Between items, sides and rounds | The watch waits on the next-item screen until the blue key is tapped. Rest time is not counted. |

There is no pause.

## Eyes

| Motion | Timing |
|---|---|
| Blink | 170 ms. The next blink comes 2.2 to 5.7 s later; one in five comes 2 s sooner (double blink). |
| Glance | Every 1.2 to 4.0 s, mostly near the centre and sometimes further out |
| Thinking | Look up and to the right while a request is in flight |

## Font

| Item | Value |
|---|---|
| Main font | Noto Sans JP Medium, 24 px, converted into the firmware by `firmware/tools/make_font.py` |
| Characters in the main font | ASCII, JIS X 0208 rows 1 to 8 (symbols, digits, Latin, kana, Greek, Cyrillic, box drawing), JIS level 1 kanji, and a short list of extra characters in `make_font.py` |
| Fallback font | M5GFX built-in `lgfxJapanGothic_24` (JIS level 1 and 2) |
| Big numbers | Noto Sans JP Medium, 120 px, digits, `:` and `/` only |

Each character missing from the main font is drawn with the fallback font. Characters outside the Basic Multilingual Plane (4-byte UTF-8, such as emoji) are treated as missing and handed to the fallback font.
