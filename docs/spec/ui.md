# HealthWatch UI

This page describes HealthWatch, the app in the `ota_1` partition (`firmware/src/hw/`). The launcher menu in `ota_0` is the upstream M5StopWatch-UserDemo with an added HealthWatch entry and is not covered here.

The screen is a 466 x 466 round AMOLED: black background, white eyes, the owner's words in light blue. Text on the screen is limited to item names, the area, amounts, counts and times, the agent's reply and what it heard (Japanese). States and actions are shown with icons: a blue circle for the blue key (a play mark to start, an arrow to go on), a yellow circle for the yellow key (a microphone to talk, a circular arrow to retry).

## Flow

1. On start the watch shows small eyes and three dots that light up in turn, and sends `start` once Wi-Fi is connected.
2. The agent suggests an area to train. The reply is spoken and shown; the fixed guide sentence after it (`よければ「メニューを出して」と言ってね。…`) is spoken only.
3. The owner holds the yellow key and answers. The agent either suggests again or sends a menu.
4. On the menu screen the owner can talk again to change the menu, or tap the blue key to start.
5. The watch runs the menu item by item without the network. After each item it stays on the item-done screen, then on the next-item screen, each until the blue key is tapped.
6. When everything is done the watch shows the done screen and sends `finish`, which saves the menu.

## Screens

| Screen | What it shows |
|---|---|
| Suggestion | Small eyes (20 % size), the agent's reply in white (up to 7 lines; past 5 lines the spacing tightens; longer text ends with `…`), what the agent heard in light blue (up to 2 lines), and the yellow microphone key when talking is possible |
| Menu | The area in yellow with a loop icon and `×N` (only when there are items to repeat), the items with their amounts (`N回`, `N分` or `N秒`), a divider and a flag icon on each item done once at the end, and the blue play key |
| Rep item | A row of dots, one per round, lit up to the current round (a flag icon in the end part), the item name, the big count with `/ target`, an arrow with the next item or a flag icon on the last item, and a yellow ring that fills with the count |
| Timed item | The item name, the interval as a flame icon in orange (`hard`) or a wave icon in grey (`easy`) with its number, the big elapsed time `mm:ss`, an arrow with the next interval's icon and the time to it (a flag icon and the time to the end on the last interval), and an orange ring that fills with the time |
| Item done | The ring flashes twice and stays thicker, the number shrinks away, a yellow check is drawn and coloured particles spread outward (about 1 s). Then the item name, the check and the blue arrow key stay still |
| Next item | Round dots (a flag icon in the end part) with `i / n`, a down chevron, the item name, its amount in yellow, and the blue play key |
| Done | The eyes (70 % size), the area with a loop icon and `×N`, and a clock icon with the total time (`m:ss`, or `h:mm:ss` from one hour) |

The menu list shows up to 8 rows. A longer list scrolls by dragging on the screen, with a bar on the right.

Items with sides are listed by the agent as separate items, for example `リバースランジ（右）` and `リバースランジ（左）`.

### Status line

On the suggestion and menu screens, one line under the content shows the state:

| Line | When |
|---|---|
| Yellow level bar | While recording |
| Error icon, and the yellow retry or microphone key | After a failed request |
| Three dots lighting up in turn | While a request is in flight |
| (the yellow microphone key on the suggestion, the blue play key on the menu) | Otherwise |

### Errors

| Icon | Trigger |
|---|---|
| Wi-Fi with a slash | Wi-Fi is not connected 3 s after start-up or a restart, or a request found no Wi-Fi within 5 s |
| Cloud with a cross | Any other failure: Cognito sign-in failed, the runtime did not answer, answered with an error or timed out, the response could not be used, the request did not fit the buffer, `finish` failed (on the done screen), or the PSRAM buffers could not be allocated at start-up |

The yellow retry key appears where a yellow press sends the request again (`start` before the first answer, `finish` on the done screen). Errors are also written to the USB serial log.

Audio that arrived before an error is not played.

## Controls

| Input | Screen | Action |
|---|---|---|
| Yellow key, hold | Suggestion, menu | Records while held and sends the recording on release. Recordings shorter than 0.4 s are not sent. Recording stops after 20 s. |
| Yellow key, press | Suggestion, before the first answer | Sends `start` again |
| Yellow key, press | Done, after saving failed | Sends `finish` again |
| Blue key, tap | Menu, next item | Starts the (next) item |
| Blue key, tap | Item done | Goes to the next-item screen |
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
| End of an item | For a rep item the last count, then `次の種目` and a 60 ms vibration, and the item-done screen |
| End of the workout | `おつかれさま` (spoken) and a 400 ms vibration, then the done screen |
| Between items, sides and rounds | The watch waits on the item-done screen and then the next-item screen, each until the blue key is tapped. Rest time is not counted. |

There is no pause.

## Eyes

| Motion | Timing |
|---|---|
| Blink | 170 ms. The next blink comes 2.2 to 5.7 s later; one in five comes 2 s sooner (double blink). |
| Glance | Every 1.2 to 4.0 s, mostly near the centre and sometimes further out |

## Font

| Item | Value |
|---|---|
| Main font | Noto Sans JP Medium, 24 px, converted into the firmware by `firmware/tools/make_font.py` |
| Characters in the main font | ASCII, JIS X 0208 rows 1 to 8 (symbols, digits, Latin, kana, Greek, Cyrillic, box drawing), JIS level 1 kanji, and a short list of extra characters in `make_font.py` |
| Fallback font | M5GFX built-in `lgfxJapanGothic_24` (JIS level 1 and 2) |
| Big numbers | Noto Sans JP Medium, 120 px, digits, `:` and `/` only |

Each character missing from the main font is drawn with the fallback font. Characters outside the Basic Multilingual Plane (4-byte UTF-8, such as emoji) are treated as missing and handed to the fallback font.
