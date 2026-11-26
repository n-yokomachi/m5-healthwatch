"""種目の一覧に照らしてAIが組んだメニューを整形する。"""
import json
from dataclasses import dataclass
from pathlib import Path

LEVELS = ("hard", "easy")


@dataclass(frozen=True)
class Exercise:
    name: str
    area: str
    kind: str  # "reps" | "time"
    sides: bool
    guide: str = ""  # 回数や時間の目安。上限ではない


@dataclass(frozen=True)
class Catalog:
    exercises: dict[str, Exercise]
    guide: str = ""  # 周回数などメニュー全体の目安


@dataclass(frozen=True)
class Item:
    name: str
    kind: str
    reps: int | None = None
    intervals: tuple[tuple[str, int], ...] = ()

    def to_json(self) -> dict:
        if self.kind == "time":
            return {"name": self.name, "kind": "time",
                    "intervals": [{"level": lv, "sec": sec} for lv, sec in self.intervals]}
        return {"name": self.name, "kind": "reps", "reps": self.reps}


@dataclass(frozen=True)
class Menu:
    area: str
    rounds: int
    items: tuple[Item, ...]
    after: tuple[Item, ...] = ()

    def to_json(self) -> dict:
        out = {"area": self.area, "rounds": self.rounds,
               "items": [i.to_json() for i in self.items]}
        if self.after:
            out["after"] = [i.to_json() for i in self.after]
        return out

    def speech(self) -> str:
        after = "、".join(_speak_item(i) for i in self.after)
        if not self.items:
            return f"{self.area}。{after}"
        text = f"{self.area}を{self.rounds}周。" + "、".join(_speak_item(i) for i in self.items)
        return f"{text}。最後に{after}" if after else text


def load_catalog(path: Path) -> Catalog:
    data = json.loads(Path(path).read_text(encoding="utf-8"))
    exercises = {e["name"]: Exercise(e["name"], e["area"], e["kind"], bool(e["sides"]), e.get("guide", ""))
                 for e in data["exercises"]}
    return Catalog(exercises, data.get("guide", ""))


def _is_int(v) -> bool:
    return isinstance(v, int) and not isinstance(v, bool)


def _speak_item(item: Item) -> str:
    if item.kind == "reps":
        return f"{item.name}{item.reps}回"
    total = sum(sec for _, sec in item.intervals)
    minutes, sec = divmod(total, 60)
    span = (f"{minutes}分" if minutes else "") + (f"{sec}秒" if sec else "")
    return f"{item.name}{span}"


def _fill(pattern: tuple[tuple[str, int], ...], total: int) -> tuple[tuple[str, int], ...]:
    """区間の並びを繰り返して合計 total 秒にする。最後の区間は余りの秒で切る。"""
    out: list[tuple[str, int]] = []
    left = total
    while left > 0:
        for level, sec in pattern:
            if left <= 0:
                break
            out.append((level, min(sec, left)))
            left -= sec
    return tuple(out)


def _build_items(raw_item: dict, ex: Exercise) -> list[Item]:
    if ex.kind == "time":
        intervals = tuple((iv["level"], iv["sec"])
                          for iv in raw_item.get("intervals") or []
                          if isinstance(iv, dict) and iv.get("level") in LEVELS and _is_int(iv.get("sec")) and iv["sec"] > 0)
        if not intervals:
            return []
        minutes = raw_item.get("minutes")
        if _is_int(minutes) and minutes > 0:
            intervals = _fill(intervals, minutes * 60)
        make = lambda name: Item(name, "time", intervals=intervals)
    else:
        reps = raw_item.get("reps")
        if not _is_int(reps) or reps <= 0:
            return []
        make = lambda name: Item(name, "reps", reps=reps)
    if ex.sides:
        return [make(f"{ex.name}（右）"), make(f"{ex.name}（左）")]
    return [make(ex.name)]


RIGHT, LEFT = "（右）", "（左）"


def _clamp_items(raw_items, catalog: Catalog) -> list[Item]:
    # 保存済みのメニューを写すと左右のある種目は「〇〇（右）」「〇〇（左）」の組で届く。
    # 組は右の値で1件の種目に戻し、片側だけの名前も一覧の名前に戻す。
    raws = [r for r in raw_items if isinstance(r, dict)] if isinstance(raw_items, list) else []
    items: list[Item] = []
    i = 0
    while i < len(raws):
        raw_item = raws[i]
        i += 1
        name = raw_item.get("name")
        if isinstance(name, str) and name.endswith((RIGHT, LEFT)):
            base = name.removesuffix(RIGHT).removesuffix(LEFT)
            if name.endswith(RIGHT) and i < len(raws) and raws[i].get("name") == base + LEFT:
                i += 1
            name = base
        ex = catalog.exercises.get(name)
        if ex is not None:
            items.extend(_build_items(raw_item, ex))
    return items


def clamp_menu(raw: dict, catalog: Catalog) -> Menu | None:
    """一覧にない種目と不正な区間を外し、左右のある種目を展開する。残る種目がなければNone。"""
    rounds = raw.get("rounds")
    if not _is_int(rounds) or rounds <= 0:
        return None
    items = _clamp_items(raw.get("items"), catalog)
    after = _clamp_items(raw.get("after"), catalog)
    if not items and not after:
        return None
    return Menu(str(raw.get("area", "")), rounds, tuple(items), tuple(after))

