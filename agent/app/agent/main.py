import asyncio
import base64
import binascii
import json
import logging
import os
from pathlib import Path

import boto3
from bedrock_agentcore.runtime import BedrockAgentCoreApp
from starlette.exceptions import HTTPException
from starlette.responses import Response

from catalog import load_catalog
from coach import GUIDE, Coach, finish
from store import Store
from voice import REGION, synthesize, transcribe

MAX_AUDIO = 640_000  # 録音は最大20秒
MAX_REPLY = 800_000  # 返事は25秒まで
PCM_BYTES_PER_SEC = 32_000  # 16kHz・16bit・モノラル
# 録り終えた音声を一気に送っても Transcribe は実時間で処理するので、録音の長さに余裕を加えて待つ。
TRANSCRIBE_MARGIN_SEC = 10
HERE = Path(__file__).parent
log = logging.getLogger(__name__)

coaches: dict[str, Coach] = {}


def _catalog_path() -> Path:
    path = HERE / "catalog.json"
    return path if path.exists() else HERE / "catalog.example.json"


def _store() -> Store:
    return Store(os.environ["TABLE_NAME"], boto3.client("dynamodb", region_name=REGION))


def _coach(session_id: str) -> Coach:
    if session_id not in coaches:
        coaches[session_id] = Coach(_store(), load_catalog(_catalog_path()))
    return coaches[session_id]


async def _hear(audio: str) -> str:
    try:
        pcm = base64.b64decode(audio, validate=True)
    except binascii.Error:
        raise HTTPException(400, "audio is not base64")
    if len(pcm) > MAX_AUDIO:
        raise HTTPException(400, "audio is longer than 20 seconds")
    try:
        timeout = len(pcm) / PCM_BYTES_PER_SEC + TRANSCRIBE_MARGIN_SEC
        return await asyncio.wait_for(transcribe(pcm), timeout)
    except Exception:
        log.exception("transcribe failed")
        # タイムアウトや文字起こしの失敗は空の文字起こしとして扱い、同じ質問を繰り返す。
        return ""


def create_app():
    app = BedrockAgentCoreApp()

    @app.entrypoint
    async def invoke(payload, context):
        req = dict(payload)
        if "audio" in req:
            req["text"] = await _hear(req.pop("audio"))
        try:
            if req.get("type") == "finish":
                # 運動中にセッションが消えている前提で、Coach と Agent を作らずに保存する。
                res = await asyncio.to_thread(finish, _store(), req.get("menu"))
            else:
                coach = _coach(context.session_id or "default-session")
                res = await asyncio.to_thread(coach.handle, req)
        except ValueError as e:
            raise HTTPException(400, str(e))

        audio = await asyncio.to_thread(synthesize, res["reply"])
        if len(audio) > MAX_REPLY and res["stage"] == "menu":
            res["reply"] = coach.menu.speech()
            audio = await asyncio.to_thread(synthesize, res["reply"])

        out = {"stage": res["stage"]}
        if req.get("type") == "talk":
            out["heard"] = req.get("text") or ""
        # 決まった案内の文は声でだけ読み、画面には出さない。
        out["reply"] = res["reply"].removesuffix(GUIDE)
        if "menu" in res:
            out["menu"] = res["menu"]
        out["audio"] = base64.b64encode(audio).decode()
        # StopWatch は本文の "audio":" の位置から音声を読むので、空白のない形に固定する。
        return Response(json.dumps(out, ensure_ascii=False, separators=(",", ":")), media_type="application/json")

    return app


app = create_app()
if __name__ == "__main__":
    app.run()
