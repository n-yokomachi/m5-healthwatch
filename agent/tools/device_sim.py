"""StopWatch の代わりに AgentCore Runtime を呼んで、通信の形と予算を確認する（標準ライブラリのみ）。"""
import argparse
import base64
import json
import time
import urllib.error
import urllib.parse
import urllib.request
import uuid
import wave
from pathlib import Path

REGION = "ap-northeast-1"
HERE = Path(__file__).parent
OUT = HERE / "out"
MAX_BODY = 1_200_000
MAX_REPLY_BYTES = 800_000


def load_env() -> dict:
    env = {}
    for line in (HERE.parent.parent / ".env").read_text().splitlines():
        if "=" in line and not line.lstrip().startswith("#"):
            k, v = line.split("=", 1)
            env[k.strip()] = v.strip().strip("'\"")
    return env


def get_token(env: dict) -> str:
    body = json.dumps({
        "AuthFlow": "USER_PASSWORD_AUTH",
        "ClientId": env["HW_USER_POOL_CLIENT_ID"],
        "AuthParameters": {"USERNAME": env["HW_USERNAME"], "PASSWORD": env["HW_PASSWORD"]},
    }).encode()
    req = urllib.request.Request(
        f"https://cognito-idp.{REGION}.amazonaws.com/",
        data=body,
        headers={
            "X-Amz-Target": "AWSCognitoIdentityProviderService.InitiateAuth",
            "Content-Type": "application/x-amz-json-1.1",
        },
    )
    with urllib.request.urlopen(req, timeout=30) as res:
        return json.load(res)["AuthenticationResult"]["AccessToken"]


class Device:
    def __init__(self, env: dict, token: str):
        arn = urllib.parse.quote(env["HW_RUNTIME_ARN"], safe="")
        self.url = (f"https://bedrock-agentcore.{REGION}.amazonaws.com/runtimes/{arn}"
                    "/invocations?qualifier=DEFAULT")
        self.token = token
        self.n = 0

    def call(self, payload: dict, session: str) -> dict | None:
        self.n += 1
        body = json.dumps(payload).encode()
        req = urllib.request.Request(self.url, data=body, headers={
            "Authorization": f"Bearer {self.token}",
            "Content-Type": "application/json",
            "X-Amzn-Bedrock-AgentCore-Runtime-Session-Id": session,
        })
        t0 = time.monotonic()
        try:
            with urllib.request.urlopen(req, timeout=180) as res:
                status, headers, raw = res.status, res.headers, res.read()
        except urllib.error.HTTPError as e:
            status, headers, raw = e.code, e.headers, e.read()
        sec = time.monotonic() - t0
        print(f"[{self.n}] {payload['type']} status={status} req={len(body)}B res={len(raw)}B "
              f"time={sec:.1f}s Content-Length={headers.get('Content-Length')} "
              f"Transfer-Encoding={headers.get('Transfer-Encoding')}")
        if status != 200:
            print(f"    body: {raw[:300].decode(errors='replace')}")
            return None
        i = raw.find(b'"audio"')
        if i >= 0:
            print(f"    raw tail: {raw[max(0, i - 20):i + 40]!r} ... {raw[-20:]!r}")
        data = json.loads(raw)
        print(f"    stage={data.get('stage')} heard={data.get('heard')!r} reply={data.get('reply')!r}")
        if "audio" in data:
            pcm = base64.b64decode(data["audio"])
            print(f"    audio={len(pcm)}B ({len(pcm) / 32000:.1f}s) "
                  f"budget: body<={MAX_BODY} {'OK' if len(raw) <= MAX_BODY else 'OVER'}, "
                  f"audio<={MAX_REPLY_BYTES} {'OK' if len(pcm) <= MAX_REPLY_BYTES else 'OVER'}")
            with wave.open(str(OUT / f"{self.n}.wav"), "wb") as w:
                w.setnchannels(1)
                w.setsampwidth(2)
                w.setframerate(16000)
                w.writeframes(pcm)
        return data


def new_session() -> str:
    return str(uuid.uuid4()) + "-healthwatch"  # 33文字以上


def pcm_b64(name: str) -> str:
    return base64.b64encode((OUT / f"{name}.pcm").read_bytes()).decode()


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--wait-before-finish", type=int, default=0, metavar="SEC")
    p.add_argument("--long", action="store_true", help="start then one talk with the ~20s recording")
    p.add_argument("--text", action="store_true", help="send talk as text instead of audio")
    p.add_argument("--oversize", action="store_true", help="send a 21s recording")
    args = p.parse_args()

    OUT.mkdir(exist_ok=True)
    env = load_env()
    dev = Device(env, get_token(env))
    session = new_session()

    if args.oversize:
        audio = base64.b64encode(bytes(21 * 32000)).decode()
        dev.call({"type": "start"}, session)
        dev.call({"type": "talk", "audio": audio}, session)
        return
    if args.long:
        dev.call({"type": "start"}, session)
        dev.call({"type": "talk", "audio": pcm_b64("long")}, session)
        return

    if args.text:
        dev.call({"type": "start"}, session)
        res = dev.call({"type": "talk", "text": "上半身と体幹にして"}, session)
        if res:
            print("    menu:", json.dumps(res.get("menu"), ensure_ascii=False))
        return

    dev.call({"type": "start"}, session)
    dev.call({"type": "talk", "audio": pcm_b64("silence")}, session)
    res = dev.call({"type": "talk", "audio": pcm_b64("ok")}, session)
    if not res or "menu" not in res:
        print("no menu received")
        return
    if args.wait_before_finish:
        print(f"waiting {args.wait_before_finish}s before finish")
        time.sleep(args.wait_before_finish)
    dev.call({"type": "finish", "menu": res["menu"]}, new_session())


main()
