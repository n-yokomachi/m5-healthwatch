import asyncio
import base64

import httpx
import pytest

import main
from coach import GUIDE, Coach

# bedrock_agentcore の app.py が呼び出しのたびに使う asyncio.iscoroutinefunction は Python 3.14 で非推奨。
pytestmark = pytest.mark.filterwarnings(
    "ignore:'asyncio.iscoroutinefunction' is deprecated:DeprecationWarning")


class Client:
    """starlette.testclient は httpx2 を求めて警告を出すので、httpx の ASGITransport で呼ぶ。"""

    def post(self, path, json):
        async def call():
            transport = httpx.ASGITransport(app=main.app)
            async with httpx.AsyncClient(transport=transport, base_url="http://test") as c:
                return await c.post(path, json=json)
        return asyncio.run(call())


@pytest.fixture
def client(monkeypatch):
    monkeypatch.setenv("TABLE_NAME", "healthwatch-test")
    main.coaches.clear()
    # start はモデルを呼ぶので、Coach には固定の返事を返すエージェントを渡す。
    monkeypatch.setattr(main, "Coach", lambda store, catalog: Coach(store, catalog, agent=lambda prompt: "今日は脚にしない？"))
    return Client()


@pytest.fixture
def stub_voice(monkeypatch):
    async def transcribe(pcm):
        return ""
    monkeypatch.setattr(main, "transcribe", transcribe)
    monkeypatch.setattr(main, "synthesize", lambda text: b"\0\0")


def test_audio_over_20s_is_rejected(client):
    r = client.post("/invocations", json={"type": "talk", "audio": base64.b64encode(b"\0" * 640_002).decode()})
    assert r.status_code == 400


def test_audio_is_last_key(client, stub_voice):
    assert list(client.post("/invocations", json={"type": "start"}).json())[-1] == "audio"


def test_empty_transcript_is_heard_as_empty(client, stub_voice):
    r = client.post("/invocations", json={"type": "talk", "audio": base64.b64encode(b"\0" * 10).decode()}).json()
    assert r["heard"] == "" and r["reply"] == "今日はどこを鍛える？" and r["audio"] == "AAA="


def test_finish_does_not_build_session_coach(client, stub_voice, monkeypatch):
    saved = []
    monkeypatch.setattr(main, "_store", lambda: type("S", (), {"put_workout": lambda self, m, at: saved.append(m)})())
    menu = {"area": "脚", "rounds": 1, "items": [{"name": "リバースランジ（右）", "kind": "reps", "reps": 10}]}
    r = client.post("/invocations", json={"type": "finish", "menu": menu})
    assert r.json()["stage"] == "done" and saved == [menu] and main.coaches == {}


def test_finish_accepts_after_only_menu(client, stub_voice, monkeypatch):
    saved = []
    monkeypatch.setattr(main, "_store", lambda: type("S", (), {"put_workout": lambda self, m, at: saved.append(m)})())
    menu = {"area": "有酸素", "rounds": 1, "items": [],
            "after": [{"name": "エアロバイク", "kind": "time", "intervals": [{"level": "hard", "sec": 600}]}]}
    r = client.post("/invocations", json={"type": "finish", "menu": menu})
    assert r.status_code == 200 and r.json()["stage"] == "done" and saved == [menu]


def test_non_string_text_is_400(client, stub_voice):
    assert client.post("/invocations", json={"type": "talk", "text": 62.4}).status_code == 400


def test_transcribe_timeout_is_audio_length_plus_10s(client, stub_voice, monkeypatch):
    timeouts = []

    async def wait_for(aw, timeout):
        timeouts.append(timeout)
        return await aw
    monkeypatch.setattr(main.asyncio, "wait_for", wait_for)
    pcm = b"\0" * 640_000  # 20秒
    client.post("/invocations", json={"type": "talk", "audio": base64.b64encode(pcm).decode()})
    assert timeouts == [30.0]


def test_body_has_fixed_shape(client, stub_voice):
    r = client.post("/invocations", json={"type": "start"})
    body = r.content
    assert b',"audio":"' in body and body.endswith(b'"}')
    assert "脚".encode() in body and b"\\u" not in body
