from datetime import UTC, datetime

import pytest

from coach import GUIDE, Coach


class FakeAgent:
    """呼ばれるたびに script の次の項目の tools を Coach のツールの本体で実行し、text を返す。"""

    def __init__(self, script):
        self.script = list(script)
        self.coach = None

    def __call__(self, prompt):
        step = self.script.pop(0)
        for name, args in step.get("tools", []):
            getattr(self.coach, name)(**args)
        return step["text"]


@pytest.fixture
def make_coach(store, catalog):
    def make(script):
        fake = FakeAgent(script)
        coach = Coach(store, catalog, agent=fake)
        fake.coach = coach
        return coach
    return make


@pytest.fixture
def coach(make_coach):
    return make_coach([])


SQUAT_15 = {"area": "脚", "rounds": 1, "items": [{"name": "スクワット", "kind": "reps", "reps": 15}]}
LUNGE_10 = {"area": "脚", "rounds": 2, "items": [{"name": "リバースランジ", "kind": "reps", "reps": 10}]}


def test_start_suggests_an_area_from_the_model(make_coach):
    coach = make_coach([{"tools": [("get_history", {})], "text": "前回は脚だったから今日は上半身にしない？"}])
    r = coach.handle({"type": "start"})
    assert r == {"stage": "suggest", "reply": "前回は脚だったから今日は上半身にしない？" + GUIDE}


def test_start_does_not_confirm_a_menu_and_keeps_one_sentence_without_digits(make_coach):
    coach = make_coach([
        {"tools": [("set_menu", {"menu": SQUAT_15})], "text": "今日は脚にしよう。スクワットを15回やろう。"},
        {"tools": [("set_menu", {"menu": SQUAT_15})], "text": "いいね"},
    ])
    r = coach.handle({"type": "start"})
    assert r == {"stage": "suggest", "reply": "今日は脚にしよう。" + GUIDE} and coach.menu is None
    assert coach.handle({"type": "talk", "text": ""})["stage"] == "suggest"
    assert coach.handle({"type": "talk", "text": "それでいいよ"})["stage"] == "menu"


def test_start_prefers_the_question_sentence(make_coach):
    coach = make_coach([{"text": "前回は脚を鍛えました。今日は上半身にしない？よろしくね。"}])
    assert coach.handle({"type": "start"}) == {"stage": "suggest", "reply": "今日は上半身にしない？" + GUIDE}


def test_start_reply_with_digits_in_first_sentence_is_replaced(make_coach):
    coach = make_coach([{"text": "今日は脚を3周やろう。"}])
    assert coach.handle({"type": "start"}) == {"stage": "suggest", "reply": "今日はどこを鍛える？" + GUIDE}


def test_empty_transcript_repeats_question(make_coach):
    coach = make_coach([{"text": "今日は脚にしない？"}])
    coach.handle({"type": "start"})
    assert coach.handle({"type": "talk", "text": ""}) == {"stage": "suggest", "reply": "今日はどこを鍛える？" + GUIDE}


def test_get_history_returns_last_workout_and_last_by_area(coach, store):
    assert coach.get_history() == {"last_workout": None, "last_by_area": {}}
    upper = {"area": "上半身と体幹", "rounds": 3, "items": [{"name": "腕立て伏せ", "kind": "reps", "reps": 10}]}
    store.put_workout(LUNGE_10, datetime(2026, 11, 23, tzinfo=UTC))
    store.put_workout(SQUAT_15, datetime(2026, 11, 24, tzinfo=UTC))
    store.put_workout(upper, datetime(2026, 11, 25, tzinfo=UTC))
    assert coach.get_history() == {"last_workout": upper,
                                   "last_by_area": {"上半身と体幹": upper, "脚": SQUAT_15}}


def test_reply_without_set_menu_stays_in_suggest(make_coach):
    coach = make_coach([{"text": "今日は脚にする？"}, {"text": "じゃあ脚にしよう。"}])
    coach.handle({"type": "start"})
    r = coach.handle({"type": "talk", "text": "うーん"})
    assert r["stage"] == "suggest" and "menu" not in r


def test_menu_speech_uses_menu_numbers(make_coach):
    coach = make_coach([{"tools": [("set_menu", {"menu": SQUAT_15})], "text": "スクワット12回やろう"}])
    r = coach.handle({"type": "talk", "text": "それでいいよ"})
    assert r["stage"] == "menu" and r["menu"] == SQUAT_15
    assert "12回" not in r["reply"] and "スクワット15回" in r["reply"]
    assert r["reply"].endswith("準備ができたらスタートボタンを押してね")


def test_menu_reply_keeps_first_sentence_without_numbers(make_coach):
    coach = make_coach([{"tools": [("set_menu", {"menu": SQUAT_15})], "text": "いいね！スクワットを12回ね。"}])
    r = coach.handle({"type": "talk", "text": "それでいいよ"})
    assert r["reply"] == "いいね！脚を1周。スクワット15回。準備ができたらスタートボタンを押してね"


def test_menu_can_be_rebuilt(make_coach):
    coach = make_coach([
        {"tools": [("set_menu", {"menu": SQUAT_15})], "text": "これでどう？"},
        {"tools": [("set_menu", {"menu": LUNGE_10})], "text": "ランジに変えたよ。"},
    ])
    coach.handle({"type": "talk", "text": "それでいいよ"})
    r = coach.handle({"type": "talk", "text": "やっぱりランジがいい"})
    assert r["stage"] == "menu"
    assert [i["name"] for i in r["menu"]["items"]] == ["リバースランジ（右）", "リバースランジ（左）"]


def test_invalid_menu_is_not_kept(make_coach):
    coach = make_coach([{"tools": [("set_menu", {"menu": {"area": "脚", "rounds": 1,
                                                         "items": [{"name": "ベンチプレス", "kind": "reps", "reps": 10}]}})],
                         "text": "ベンチプレスはどう？"}])
    r = coach.handle({"type": "talk", "text": "胸がいい"})
    assert r["stage"] == "suggest" and "menu" not in r


def test_finish_saves_without_session_state(store, catalog):
    fresh = Coach(store, catalog, agent=FakeAgent([]))
    r = fresh.handle({"type": "finish", "menu": {"area": "脚", "rounds": 1,
        "items": [{"name": "スクワット", "kind": "reps", "reps": 15}]}})
    assert r["stage"] == "done" and store.last_workout()["items"][0]["reps"] == 15
    assert r["reply"] == "おつかれさま！"


def test_finish_keeps_sided_items_from_set_menu(make_coach, store):
    coach = make_coach([{"tools": [("set_menu", {"menu": LUNGE_10})], "text": "これでどう？"}])
    menu = coach.handle({"type": "talk", "text": "ランジがいい"})["menu"]
    coach.handle({"type": "finish", "menu": menu})
    assert [i["name"] for i in store.last_workout()["items"]] == ["リバースランジ（右）", "リバースランジ（左）"]


@pytest.mark.parametrize("menu", [None, "脚", {"area": "脚"}, {"area": "脚", "items": []}])
def test_finish_rejects_malformed_menu(coach, store, menu):
    with pytest.raises(ValueError):
        coach.handle({"type": "finish", "menu": menu})
    assert store.last_workout() is None


def test_non_string_text_is_rejected(coach):
    with pytest.raises(ValueError):
        coach.handle({"type": "talk", "text": 62.4})


def test_real_agent_has_two_tools(store, catalog):
    agent = Coach(store, catalog)._agent
    assert sorted(agent.tool_names) == ["get_history", "set_menu"]


def test_system_prompt_carries_guides(catalog):
    from coach import system_prompt
    text = system_prompt(catalog)
    assert '"guide": "15回"' in text
    assert "メニュー全体の目安: 3周" in text
