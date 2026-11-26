import io

import boto3
from botocore.response import StreamingBody
from botocore.stub import Stubber

from voice import synthesize


def test_synthesize_uses_16k_pcm_kazuha():
    client = boto3.client("polly", region_name="ap-northeast-1")
    with Stubber(client) as s:
        s.add_response(
            "synthesize_speech",
            {"AudioStream": StreamingBody(io.BytesIO(b"\x01\x02"), 2)},
            expected_params={
                "Text": "今日はどこを鍛える？",
                "OutputFormat": "pcm",
                "SampleRate": "16000",
                "VoiceId": "Kazuha",
                "Engine": "neural",
                "LanguageCode": "ja-JP",
            },
        )
        assert synthesize("今日はどこを鍛える？", client) == b"\x01\x02"
