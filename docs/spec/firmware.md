# Firmware

The StopWatch carries two programs:

| Partition | Program | Source | Framework |
|---|---|---|---|
| `ota_0` | Launcher menu (M5StopWatch-UserDemo with a HealthWatch entry) | `menu/` | ESP-IDF 5.5 via PlatformIO (pioarduino 55.03.312) |
| `ota_1` | HealthWatch | `firmware/` | Arduino-ESP32 via PlatformIO (pioarduino 55.03.312), M5Unified, M5GFX, ArduinoJson 7 |

The screens and controls are described in [ui.md](ui.md).

## Source layout

| File | Contents |
|---|---|
| `src/main.cpp` | `setup()` and `loop()`: device setup, then the HealthWatch flow |
| `src/shared.cpp`, `src/shared.h` | Device setup (screen canvas, font, mic task, Wi-Fi), Noto text drawing with the fallback font, the eyes, the voice level bar, switching back to the launcher |
| `src/hw/health.cpp` | The conversation and workout flow, the keys, the network task |
| `src/hw/screens.cpp` | The screens |
| `src/hw/client.cpp` | HTTPS to Cognito and the AgentCore Runtime |
| `src/hw/reply.cpp` | Streaming parser for the agent's response (JSON with base64 PCM last) |
| `src/hw/chunked.cpp` | HTTP chunked transfer decoding |
| `src/hw/workout.cpp` | Menu parsing and workout progress |

`workout.cpp`, `reply.cpp` and `chunked.cpp` do not depend on Arduino and have host tests in `test/` (`pio test -e native`).

## Build-time settings

`firmware/scripts/env_config.py`, a PlatformIO pre-script, reads the repository's `.env` and writes `firmware/src/env_config.h` (git-ignored). The file is rewritten only when its content changes. Lines starting with `#` are ignored and matching quotes around a value are removed.

| Setting | Meaning |
|---|---|
| `WIFI_SSID` | Wi-Fi network the watch joins |
| `WIFI_PASS` | Wi-Fi password |
| `HW_USER_POOL_CLIENT_ID` | Cognito app client ID |
| `HW_USERNAME`, `HW_PASSWORD` | The Cognito user the watch signs in as |
| `HW_RUNTIME_ARN` | AgentCore Runtime ARN. The invocation URL `https://bedrock-agentcore.ap-northeast-1.amazonaws.com/runtimes/<ARN, URL-encoded>/invocations?qualifier=DEFAULT` is built from it. |

A missing setting is reported in the build log and compiled in as an empty string. The build does not stop.

## Partitions

Both projects use the same table (`firmware/partitions.csv`, `menu/partitions.csv`) on 16 MB flash. Offsets left empty in the file are placed by the partition tool; the resulting offsets are:

| Name | Type | Offset | Size |
|---|---|---|---|
| `nvs` | data, nvs | `0x9000` | `0x4000` |
| `otadata` | data, ota | `0xd000` | `0x2000` |
| `phy_init` | data, phy | `0xf000` | `0x1000` |
| `ota_0` | app | `0x20000` | `0x4f0000` (4.9 MB) |
| `ota_1` | app | `0x510000` | `0x4f0000` (4.9 MB) |
| `storage` | data, fat | `0xa00000` | 4 MB |
| `coredump` | data, coredump | `0xe00000` | `0x10000` |

`ota_1` starts where `ota_0` ends: `0x20000 + 0x4f0000 = 0x510000`. The HealthWatch image (`firmware/.pio/build/m5stopwatch/firmware.bin`) is written there:

```sh
cd firmware
pio run
uvx esptool --chip esp32s3 --port <PORT> write-flash 0x510000 .pio/build/m5stopwatch/firmware.bin
```

The launcher is flashed with `pio run -t upload --upload-port <PORT>` in `menu/`. HealthWatch is written only with the esptool command above, at the `ota_1` offset.

### Switching between the launcher and HealthWatch

| From | Action | What happens |
|---|---|---|
| Launcher | open `HealthWatch` | The entry looks for an app in `ota_1`. If there is one, it sets `ota_1` as the boot partition and restarts. If not, it logs `no app in ota_1` and closes. |
| HealthWatch | hold the yellow and blue keys together for 1 s | The app checks for an app in `ota_0`, clears the screen, sets `ota_0` as the boot partition, waits 300 ms and restarts. If there is none, it logs `launcher not found in ota_0` and stays. |

The boot partition is stored in `otadata`, so the watch starts the last selected program after a reset or power cycle.

## Talking to the agent

All requests are HTTPS `POST`s of JSON to the AgentCore Runtime with `Authorization: Bearer <Cognito access token>` and `X-Amzn-Bedrock-AgentCore-Runtime-Session-Id`. One request runs at a time, in a network task on core 0.

| Body | When |
|---|---|
| `{"type":"start"}` | At start-up and after a restart (blue key held), once Wi-Fi is connected |
| `{"type":"talk","audio":"<base64 PCM>"}` | After a recording on the suggestion or menu screen |
| `{"type":"finish","menu":{...}}` | When the whole workout is done, in a session of its own. The menu is sent back exactly as the agent sent it. |

The response is JSON whose last key is `audio`: `{"stage":"suggest"|"menu","heard":"...","reply":"...","menu":{...},"audio":"<base64 PCM>"}`. `heard` is present only for `talk`, `menu` only when the agent sends one. The watch reads the response as it arrives: the keys before `audio` (up to 32 KiB) are kept as text, and the audio is decoded straight into a PSRAM buffer, so the body never has to fit in memory.

The menu has `area`, `rounds`, `items` (done `rounds` times) and the optional `after` (done once at the end). Each item has `name` and `kind`: `reps` with `reps`, or `time` with `intervals`, a list of `{"level":"hard"|"easy","sec":N}`. Items with an unknown kind, a rep item without reps and a time item without a valid interval are dropped. A menu with `rounds` below 1 or no item left is rejected.

| Item | Value |
|---|---|
| Sign-in | Cognito `InitiateAuth` (`USER_PASSWORD_AUTH`) at `cognito-idp.ap-northeast-1.amazonaws.com` |
| Token | Fetched once Wi-Fi is up, before the first request; refreshed 60 s before it expires; on 401 or 403 the request is retried once with a new token |
| Trusted roots | Amazon Root CA 1 and Starfield Services Root CA G2, compiled in |
| Waiting for Wi-Fi | up to 5 s per request |
| Connect timeout | 5 s |
| TLS handshake timeout | 15 s |
| Response timeout | 60 s, also the longest gap between body bytes |
| Request body | up to 900,000 bytes (a 20 s recording as `talk` JSON is 853,360 bytes) |
| Reply audio | 16 kHz, 16-bit mono; up to 25 s (800,000 bytes); the rest is dropped |
| Session ID | `hw-` and 32 random hex digits, new at start-up and on every restart |

A response that arrives after the conversation was restarted is dropped.

## Recording

| Item | Value |
|---|---|
| Sample rate | 16 kHz, 16-bit mono |
| Mic noise filter level | 64 |
| Record call | 800 samples (50 ms) |
| Buffer | 20 s in PSRAM; recording stops when it is full |
| Minimum length | 0.4 s; shorter recordings are not sent |

A mic task on core 1 fills the buffer while the yellow key is held. On release, the app stops the mic, waits until every queued sample is filled, and sends the recording as `talk`.

The level bar shows the peak of the last finished 50 ms chunk divided by 400, capped at 40.

## Workout sounds

| Item | Value |
|---|---|
| Reply audio | 16 kHz from the agent, played once the response is complete |
| Workout clips | 8 kHz, compiled in (`src/hw/clips.h`): counts 1 to 50, `スタート`, `おつかれさま！`, `ややきつくして`, `楽にして`, `次の種目` |
| Volume | maximum (255) |
| Rep interval | 3 s per rep (`REP_INTERVAL_MS` in `src/hw/workout.h`) |
| Vibration | 60 ms when an item ends, 400 ms when the workout ends |

The reply and the clips share one speaker channel. Each workout event cuts what is playing; the last count of an item is followed by the closing clip. Counts above 50 are not read out. The mic and the speaker share the I2S lines, so the speaker is stopped before recording. Outside the workout screens the speaker is stopped once it has gone quiet.

Without `clips.h` the firmware still builds and the workout runs silently.

## Power

| Item | Behaviour |
|---|---|
| Wi-Fi | Modem sleep, switched off (full power) only during a request |
| Microphone | Started when the yellow key is pressed, stopped on release |
| Frame interval | 33 ms |
| Brightness | 150 |

## Fonts

HealthWatch embeds Noto Sans JP Medium as a VLW font in `firmware/src/font_noto.h`, and a 120 px digits-only font in `firmware/src/hw/font_digits.h`. Both are generated from `firmware/fonts/NotoSansJP-Medium.otf`; none of these files are in the repository.

```sh
cd firmware
mkdir -p fonts
curl -L -o fonts/NotoSansJP-Medium.otf \
  https://github.com/notofonts/noto-cjk/raw/main/Sans/SubsetOTF/JP/NotoSansJP-Medium.otf
uv run --with freetype-py python tools/make_font.py
uv run --with freetype-py python tools/make_font.py 120 --chars "0123456789:/" \
  --out src/hw/font_digits.h --name FONT_DIGITS
```

`make_font.py` takes an optional pixel size (default 24). `--chars` limits the glyphs to the given characters, `--out` sets the output header and `--name` the array name. The character set and the fallback font are described in [ui.md](ui.md#font). Without `font_digits.h` the big numbers use the normal font.

## Workout clips

```sh
cd firmware
uv run --with boto3 python tools/make_clips.py
```

`make_clips.py` synthesizes the counts 1-50 and the phrases with Amazon Polly (neural, Kazuha, 8 kHz, 16-bit mono PCM; region `ap-northeast-1`), cuts the silence at both ends, and writes `src/hw/clips.h`. The audio totals about 630 KB; the script fails above 700,000 bytes. It needs AWS credentials allowed to call `polly:SynthesizeSpeech`.

## Launcher icon

`menu/tools/make_icon.py` generates `menu/main/assets/images/icon_health.c`, a 200 x 200 RGB565 LVGL image of a dumbbell. The generated file is in the repository.

```sh
cd menu
uv run --with pillow python tools/make_icon.py
```

## Serial log

Both programs log over USB at 115200 baud:

```sh
pio device monitor -b 115200
```
