<div align="center">

<h1>m5-healthwatch</h1>
<p>A voice workout coach on the M5Stack StopWatch.</p>
  <p>
    <img src="https://img.shields.io/badge/ESP32--S3-M5Stack%20StopWatch-orange" alt="ESP32-S3 M5Stack StopWatch">
    <img src="https://img.shields.io/badge/PlatformIO-pioarduino-f5822a" alt="PlatformIO">
    <img src="https://img.shields.io/badge/Amazon%20Bedrock-AgentCore-ff9900" alt="Amazon Bedrock AgentCore">
    <img src="https://img.shields.io/badge/Python-3.13-3776ab" alt="Python 3.13">
    <img src="https://img.shields.io/badge/License-MIT-blue" alt="License: MIT">
  </p>

</div>

HealthWatch runs your daily bodyweight workout by voice, without a phone. Open it from the StopWatch's launcher and it suggests what to train today. Hold the yellow key to answer, and the coach builds a menu from your exercise catalog and your last session. Tap the blue key to start: the watch counts reps aloud, times intervals on an exercise bike, and buzzes when an item or the whole workout is done. The finished menu is saved for next time.

The coach is a [Strands](https://strandsagents.com/) agent on Amazon Bedrock AgentCore Runtime. It transcribes speech with Amazon Transcribe, plans with Amazon Nova 2 Lite, speaks with Amazon Polly and keeps the workout history in DynamoDB. The watch signs in with Amazon Cognito.

## How it works

```
 StopWatch (ota_1: HealthWatch)                   AWS (ap-northeast-1)
+--------------------------------+   HTTPS    +---------------------------------------+
| hold yellow key: record        |  JWT from  | AgentCore Runtime (Strands agent)     |
| 16 kHz PCM, sent as JSON       |  Cognito   |   Amazon Transcribe  speech-to-text   |
|                                | ---------> |   Nova 2 Lite        plans the menu   |
| reply: text on screen,         |            |   Amazon Polly       16 kHz reply     |
| 16 kHz PCM on the speaker      | <--------- |   DynamoDB           workout history  |
|                                |            +---------------------------------------+
| workout: counts and phrases    |
| from built-in audio, offline   |
+--------------------------------+
 ota_0: stock launcher menu with a HealthWatch entry that boots ota_1
```

The workout itself needs no network: the counts 1-50 and the fixed phrases are compiled into the firmware.

## Requirements

- M5Stack StopWatch
- An AWS account with access to Amazon Bedrock (Nova 2 Lite) in `ap-northeast-1`
- Node.js, the [AgentCore CLI](https://github.com/aws/agentcore-cli) (`npm install -g @aws/agentcore`) and the AWS CLI
- [PlatformIO](https://platformio.org/) (`pio`) and [uv](https://docs.astral.sh/uv/)

## Quick start

Run the commands from the repository root. `<PORT>` is the watch's USB serial port, for example `/dev/cu.usbmodem1101`.

### AWS side

1. Copy the settings. Fill in `WIFI_SSID` and `WIFI_PASS` now; the `HW_*` values come from the next steps:

   ```sh
   cp .env.example .env
   ```

2. Optionally copy the exercise catalog and edit it. Without `catalog.json` the agent uses `catalog.example.json`:

   ```sh
   cp agent/app/agent/catalog.example.json agent/app/agent/catalog.json
   ```

3. Create the Cognito user pool and the DynamoDB table `HealthWatch`. Note the `UserPoolId` and `UserPoolClientId` outputs:

   ```sh
   (cd agent/infra && npm install && npx cdk deploy)
   ```

4. Create the user and write `HW_USER_POOL_CLIENT_ID`, `HW_USERNAME` and `HW_PASSWORD` to `.env`. The password needs upper and lower case letters, a digit and a symbol:

   ```sh
   aws cognito-idp admin-create-user --user-pool-id <UserPoolId> --username healthwatch --message-action SUPPRESS
   aws cognito-idp admin-set-user-password --user-pool-id <UserPoolId> --username healthwatch --password='<password>' --permanent
   ```

5. In `agent/agentcore/agentcore.json`, set the pool ID in `discoveryUrl` and the client ID in `allowedClients`. Then deploy the agent and copy the runtime ARN from `agentcore status` to `HW_RUNTIME_ARN` in `.env`. On the first deploy, `agentcore deploy` writes the git-ignored `agent/agentcore/aws-targets.json` (`[{"name": "default", "account": "<account-id>", "region": "ap-northeast-1"}]`) from the signed-in account and `AWS_REGION`:

   ```sh
   (cd agent && AWS_REGION=ap-northeast-1 agentcore deploy && agentcore status)
   ```

6. Check the agent with the script that stands in for the StopWatch. Each round trip should return 200, and the menu is saved to the table:

   ```sh
   (cd agent && uv run --project app/agent tools/make_fixtures.py && python3 tools/device_sim.py)
   (cd agent && agentcore traces runtime list)
   ```

### StopWatch side

1. Download the font and convert it (it is not included), then generate the workout audio with Amazon Polly (needs AWS credentials allowed to call Polly in `ap-northeast-1`):

   ```sh
   mkdir -p firmware/fonts
   curl -L -o firmware/fonts/NotoSansJP-Medium.otf \
     https://github.com/notofonts/noto-cjk/raw/main/Sans/SubsetOTF/JP/NotoSansJP-Medium.otf
   uv run --with freetype-py python firmware/tools/make_font.py
   uv run --with freetype-py python firmware/tools/make_font.py 120 --chars "0123456789:/" \
     --out firmware/src/hw/font_digits.h --name FONT_DIGITS
   uv run --with boto3 python firmware/tools/make_clips.py
   ```

2. Build and flash the launcher menu into `ota_0`:

   ```sh
   pio run -d menu -t upload --upload-port <PORT>
   ```

3. Build HealthWatch and write it to `ota_1` at `0x510000` ([partitions](docs/spec/firmware.md#partitions)). The build reads `.env`, so finish the AWS side first:

   ```sh
   pio run -d firmware
   uvx esptool --chip esp32s3 --port <PORT> write-flash 0x510000 firmware/.pio/build/m5stopwatch/firmware.bin
   ```

4. On the watch, open `HealthWatch` in the launcher.

Details: [firmware](docs/spec/firmware.md), [UI](docs/spec/ui.md), [design](docs/superpowers/specs/2026-11-26-health-coach-design.md).

## Controls

| Input | Action |
|---|---|
| Yellow key, hold | Talk on the suggestion and menu screens. Release to send. |
| Blue key, tap | Start the menu, or the next item |
| Blue key, hold | Start over from the opening screen. A workout in progress is not saved. |
| Yellow and blue keys, hold together for 1 s | Back to the launcher menu |
| Drag on the menu | Scroll a list longer than 8 rows |

The screens, messages and timings are in [docs/spec/ui.md](docs/spec/ui.md).

## Exercise catalog

The agent picks exercises only from `agent/app/agent/catalog.json`, and items that are not in it are dropped from the menu. That file is git-ignored so that your own exercises and amounts stay out of the repository; `catalog.example.json` is the public sample and is used when `catalog.json` does not exist. Each exercise has a name, the area it trains, `reps` or `time`, whether it has left and right sides, and a guide amount used the first time an area is trained.

## Notes

- The text on the watch and the speech are Japanese.
- The Cognito password and the other `.env` values are compiled into the firmware. Anyone holding the watch can read them out; if that happens, reset the password and rebuild.

## License

- `menu/` is the [M5StopWatch-UserDemo](https://github.com/m5stack/M5StopWatch-UserDemo) launcher by M5Stack, under the MIT License (`menu/LICENSE`). The changes are listed in [menu/README.md](menu/README.md).
- Noto Sans JP is licensed under the SIL Open Font License 1.1. It is not included in this repository; it is downloaded and converted during setup.
- Everything else: MIT License (`LICENSE`).
