import os

from strands.models.bedrock import BedrockModel

DEFAULT_MODEL_ID = "jp.amazon.nova-2-lite-v1:0"
REGION = "ap-northeast-1"


def load_model() -> BedrockModel:
    """Nova 2 Lite の推論プロファイルを使う。MODEL_ID で差し替えられる。"""
    return BedrockModel(model_id=os.environ.get("MODEL_ID", DEFAULT_MODEL_ID), region_name=REGION)
