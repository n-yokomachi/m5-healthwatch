"""StopWatchとの会話を段階（suggest → menu）で進める。"""
import json
import re
from datetime import UTC, datetime
from pathlib import Path

from strands import Agent, tool

from catalog import Catalog, Menu, clamp_menu
from model.load import load_model

ASK_AREA = "今日はどこを鍛える？"
GUIDE = "よければ「メニューを出して」と言ってね。ほかの部位にしたいときは、その部位を言ってね。"
START = "今日の部位を提案して"
READY = "準備ができたらスタートボタンを押してね"
DONE = "おつかれさま！"
PROMPT = Path(__file__).with_name("prompt.md")

_FIRST_SENTENCE = re.compile(r"^.*?[。！？!?]")
_SENTENCE = re.compile(r"[^。！？!?]*[。！？!?]")
_DIGIT = re.compile(r"\d")


def _first(said: str) -> str:
    said = said.strip()
    match = _FIRST_SENTENCE.match(said)
    return match.group(0) if match else (f"{said}。" if said else "")


def _suggestion(said: str) -> str:
    """start の返事から、質問の文があればその最初の1文を、なければ最初の1文を選ぶ。"""
    for sentence in _SENTENCE.findall(said):
        if sentence.rstrip().endswith(("？", "?")):
            return sentence.strip()
    return _first(said)


def finish(store, menu) -> dict:
    """運動の後のメニューを保存する。セッションの状態に頼らない。

    メニューは set_menu のときに一覧で確認済みで、左右の展開後の名前（〇〇（右））を含むので、
    clamp_menu には通し直さず形だけを確認してそのまま保存する。
    """
    if not isinstance(menu, dict) or not any(isinstance(menu.get(k), list) and menu[k] for k in ("items", "after")):
        raise ValueError("menu is invalid")
    store.put_workout(menu, datetime.now(UTC))
    return {"stage": "done", "reply": DONE}


class Coach:
    """1つのセッションの会話の段階と確定したメニューを持つ。finish だけは状態を使わない。"""

    def __init__(self, store, catalog: Catalog, agent=None):
        self._store = store
        self._catalog = catalog
        self.stage = "suggest"
        self.menu: Menu | None = None
        self._starting = False
        self._agent = agent or self._build_agent()

    def handle(self, req: dict) -> dict:
        kind = req.get("type")
        if kind == "start":
            self.stage, self.menu = "suggest", None
            self._starting = True
            try:
                said = str(self._agent(START))
            finally:
                self._starting = False
            first = _suggestion(said)
            return {"stage": "suggest", "reply": (ASK_AREA if not first or _DIGIT.search(first) else first) + GUIDE}
        if kind == "talk":
            text = req.get("text") or ""
            if not isinstance(text, str):
                raise ValueError("text must be a string")
            return self._talk(text)
        if kind == "finish":
            return finish(self._store, req.get("menu"))
        raise ValueError(f"unknown type: {kind!r}")

    @tool
    def get_history(self) -> dict:
        """前回のメニューと、部位ごとに最後のメニューを返す。履歴がなければ None と空の辞書。"""
        return {"last_workout": self._store.last_workout(), "last_by_area": self._store.last_by_area()}

    @tool
    def set_menu(self, menu: dict) -> dict:
        """今日のメニューを確定する。一覧にない種目は外され、左右のある種目は右と左に分かれる。

        Args:
            menu: {"area": 部位, "rounds": 周回数, "items": [種目]}。種目は回数なら
                {"name": 名前, "kind": "reps", "reps": 回数}、時間なら
                {"name": 名前, "kind": "time", "intervals": [{"level": "hard" か "easy", "sec": 秒数}]}。
                時間の種目に "minutes": 合計の分数 を付けると、intervals の並びを繰り返して
                その分数になるまで埋める（例: hard 120 秒と easy 60 秒の並びに minutes 15）。
                筋トレの周回がすべて終わった後に1回だけやる種目（エアロバイクなど）は
                "after" に同じ形の種目の配列で入れる。周回の items には入れない。
                after だけで items が空のときも rounds は 1 にする。
        """
        if self._starting:
            return {"error": "今は部位の提案だけ。メニューは部位が決まってから確定する。"}
        clamped = clamp_menu(menu, self._catalog) if isinstance(menu, dict) else None
        if clamped is None:
            rounds = menu.get("rounds") if isinstance(menu, dict) else None
            if isinstance(rounds, bool) or not isinstance(rounds, int) or rounds <= 0:
                return {"error": "周回数（rounds）が1以上の整数ではない。整数にして組み直して。"}
            return {"error": "一覧にある種目が残らなかった。一覧の種目で組み直して。"}
        self.menu = clamped
        self.stage = "menu"
        return clamped.to_json()

    def _talk(self, text: str) -> dict:
        if not text.strip():
            return self._repeat()
        said = str(self._agent(text))
        if self.stage == "menu":
            return self._menu_reply(said)
        return {"stage": self.stage, "reply": said}

    def _repeat(self) -> dict:
        if self.stage == "menu":
            return {"stage": "menu", "reply": f"{self.menu.speech()}。{READY}", "menu": self.menu.to_json()}
        return {"stage": self.stage, "reply": ASK_AREA + GUIDE}

    def _menu_reply(self, said: str) -> dict:
        # 画面と声のメニューを一致させるため、数字はモデルの文から読み上げず Menu.speech() で読む。
        # 最初の1文に数字があれば、その文も読まない。
        first = _first(said)
        if _DIGIT.search(first):
            first = ""
        return {"stage": "menu", "reply": f"{first}{self.menu.speech()}。{READY}", "menu": self.menu.to_json()}

    def _build_agent(self) -> Agent:
        return Agent(
            model=load_model(),
            system_prompt=system_prompt(self._catalog),
            tools=[self.get_history, self.set_menu],
            callback_handler=None,
        )


def system_prompt(catalog: Catalog) -> str:
    """prompt.md に種目の一覧と目安を続けたシステムプロンプト。"""
    names = []
    for e in catalog.exercises.values():
        entry = {"name": e.name, "area": e.area, "kind": e.kind}
        if e.guide:
            entry["guide"] = e.guide
        names.append(entry)
    text = PROMPT.read_text(encoding="utf-8") + "\n種目の一覧:\n" + json.dumps(names, ensure_ascii=False)
    if catalog.guide:
        text += "\nメニュー全体の目安: " + catalog.guide
    return text
