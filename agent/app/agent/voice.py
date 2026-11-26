import asyncio

import boto3
from amazon_transcribe.client import TranscribeStreamingClient
from amazon_transcribe.handlers import TranscriptResultStreamHandler
from amazon_transcribe.model import TranscriptEvent

REGION = "ap-northeast-1"
SAMPLE_RATE = 16000
CHUNK_BYTES = 6400  # 200ms of 16kHz 16bit mono PCM


class _FinalTextHandler(TranscriptResultStreamHandler):
    def __init__(self, stream):
        super().__init__(stream)
        self.parts: list[str] = []

    async def handle_transcript_event(self, event: TranscriptEvent):
        for result in event.transcript.results:
            if result.is_partial or not result.alternatives:
                continue
            self.parts.append(result.alternatives[0].transcript)


async def transcribe(pcm: bytes) -> str:
    client = TranscribeStreamingClient(region=REGION)
    stream = await client.start_stream_transcription(
        language_code="ja-JP",
        media_sample_rate_hz=SAMPLE_RATE,
        media_encoding="pcm",
    )
    handler = _FinalTextHandler(stream.output_stream)

    async def send():
        for i in range(0, len(pcm), CHUNK_BYTES):
            await stream.input_stream.send_audio_event(audio_chunk=pcm[i:i + CHUNK_BYTES])
        await stream.input_stream.end_stream()

    await asyncio.gather(send(), handler.handle_events())
    return "".join(handler.parts)


def synthesize(text: str, client=None) -> bytes:
    client = client or boto3.client("polly", region_name=REGION)
    res = client.synthesize_speech(
        Text=text,
        OutputFormat="pcm",
        SampleRate=str(SAMPLE_RATE),
        VoiceId="Kazuha",
        Engine="neural",
        LanguageCode="ja-JP",
    )
    return res["AudioStream"].read()
