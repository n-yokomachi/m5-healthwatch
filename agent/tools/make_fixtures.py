"""device_sim.py が送る録音（16kHz・16bit・モノラルのPCM）を Polly で作る。"""
from pathlib import Path

import boto3

REGION = "ap-northeast-1"
RATE = 16000
OUT = Path(__file__).parent / "out"

LONG_TEXT = (
    "今日はいつもより少し早く起きて、朝ごはんにトーストとヨーグルトを食べました。"
    "昼は軽くサラダを食べて、夕方までずっと机に座って仕事をしていたので、"
    "肩と背中が固まっている感じがします。"
)

polly = boto3.client("polly", region_name=REGION)


def speak(text: str) -> bytes:
    res = polly.synthesize_speech(
        Text=text,
        OutputFormat="pcm",
        SampleRate=str(RATE),
        VoiceId="Kazuha",
        Engine="neural",
        LanguageCode="ja-JP",
    )
    return res["AudioStream"].read()


OUT.mkdir(exist_ok=True)
files = {
    "ok": speak("それでいいよ"),
    "silence": bytes(RATE * 2),  # 1秒
    "long": speak(LONG_TEXT),
}
for name, pcm in files.items():
    (OUT / f"{name}.pcm").write_bytes(pcm)
    print(f"{name}.pcm: {len(pcm)} bytes ({len(pcm) / (RATE * 2):.1f}s)")
