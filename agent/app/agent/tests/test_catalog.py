import pytest

from catalog import clamp_menu


def test_unknown_exercise_is_dropped(catalog):
    menu = clamp_menu({"area": "脚", "rounds": 2, "items": [
        {"name": "ベンチプレス", "kind": "reps", "reps": 10},
        {"name": "スクワット", "kind": "reps", "reps": 15}]}, catalog)
    assert [i["name"] for i in menu.to_json()["items"]] == ["スクワット"]


def test_speech_uses_menu_numbers(catalog):
    m = clamp_menu({"area": "脚", "rounds": 2, "items": [
        {"name": "スクワット", "kind": "reps", "reps": 15}]}, catalog)
    assert "2周" in m.speech() and "スクワット15回" in m.speech()


def test_speech_time_item_uses_interval_total(catalog):
    m = clamp_menu({"area": "有酸素", "rounds": 1, "items": [
        {"name": "エアロバイク", "intervals": [
            {"level": "hard", "sec": 600}, {"level": "easy", "sec": 300}]}]}, catalog)
    assert "エアロバイク15分" in m.speech()


def test_sides_are_expanded(catalog):
    m = clamp_menu({"area": "脚", "rounds": 1, "items": [
        {"name": "リバースランジ", "kind": "reps", "reps": 10}]}, catalog).to_json()
    assert [i["name"] for i in m["items"]] == ["リバースランジ（右）", "リバースランジ（左）"]


def test_unknown_interval_level_is_dropped(catalog):
    m = clamp_menu({"area": "有酸素", "rounds": 1, "items": [
        {"name": "エアロバイク", "kind": "reps", "intervals":
         [{"level": "hard", "sec": 120}, {"level": "max", "sec": 30}, {"level": "easy", "sec": 60}]}]}, catalog).to_json()
    assert m["items"][0]["kind"] == "time"
    assert m["items"][0]["intervals"] == [{"level": "hard", "sec": 120}, {"level": "easy", "sec": 60}]


def test_to_json_shape(catalog):
    m = clamp_menu({"area": "脚", "rounds": 3, "items": [
        {"name": "スクワット", "reps": 15}]}, catalog).to_json()
    assert m == {"area": "脚", "rounds": 3,
                 "items": [{"name": "スクワット", "kind": "reps", "reps": 15}]}


def test_no_valid_items_returns_none(catalog):
    assert clamp_menu({"area": "脚", "rounds": 1, "items": [{"name": "懸垂", "kind": "reps", "reps": 5}]}, catalog) is None



BIKE = {"name": "エアロバイク", "intervals": [{"level": "hard", "sec": 600}, {"level": "easy", "sec": 300}]}


def test_after_is_clamped_like_items(catalog):
    m = clamp_menu({"area": "脚", "rounds": 3, "items": [{"name": "スクワット", "reps": 15}],
                    "after": [{"name": "懸垂", "reps": 5}, BIKE, {"name": "リバースランジ", "reps": 10}]}, catalog).to_json()
    assert [i["name"] for i in m["after"]] == ["エアロバイク", "リバースランジ（右）", "リバースランジ（左）"]
    assert m["after"][0]["kind"] == "time"


def test_empty_after_is_omitted(catalog):
    m = clamp_menu({"area": "脚", "rounds": 1, "items": [{"name": "スクワット", "reps": 15}], "after": []}, catalog).to_json()
    assert "after" not in m


def test_after_only_menu_is_valid(catalog):
    m = clamp_menu({"area": "有酸素", "rounds": 1, "items": [], "after": [BIKE]}, catalog)
    assert m.to_json()["items"] == [] and len(m.to_json()["after"]) == 1


def test_speech_appends_after(catalog):
    m = clamp_menu({"area": "脚", "rounds": 3, "items": [{"name": "スクワット", "reps": 15}], "after": [BIKE]}, catalog)
    assert m.speech() == "脚を3周。スクワット15回。最後にエアロバイク15分"


def test_both_empty_returns_none(catalog):
    assert clamp_menu({"area": "脚", "rounds": 1, "items": [{"name": "懸垂", "reps": 5}], "after": [{"name": "懸垂", "reps": 5}]}, catalog) is None


def test_speech_after_only_omits_rounds_and_last(catalog):
    m = clamp_menu({"area": "有酸素", "rounds": 1, "items": [], "after": [BIKE]}, catalog)
    assert m.speech() == "有酸素。エアロバイク15分"


def test_guide_is_optional(tmp_path):
    from catalog import load_catalog
    path = tmp_path / "c.json"
    path.write_text('{"exercises": [{"name": "スクワット", "area": "脚", "kind": "reps", "sides": false}]}', encoding="utf-8")
    catalog = load_catalog(path)
    assert catalog.guide == ""
    assert catalog.exercises["スクワット"].guide == ""


def test_copied_side_names_are_merged(catalog):
    menu = clamp_menu({"area": "脚", "rounds": 3, "items": [
        {"name": "リバースランジ（右）", "kind": "reps", "reps": 12},
        {"name": "リバースランジ（左）", "kind": "reps", "reps": 12},
        {"name": "スクワット", "kind": "reps", "reps": 15}]}, catalog)
    assert [i["name"] for i in menu.to_json()["items"]] == ["リバースランジ（右）", "リバースランジ（左）", "スクワット"]


def test_one_side_name_is_expanded(catalog):
    menu = clamp_menu({"area": "脚", "rounds": 1, "items": [
        {"name": "リバースランジ（左）", "kind": "reps", "reps": 10}]}, catalog)
    assert [i["name"] for i in menu.to_json()["items"]] == ["リバースランジ（右）", "リバースランジ（左）"]


def test_repeated_base_name_is_kept_twice(catalog):
    menu = clamp_menu({"area": "脚", "rounds": 1, "items": [
        {"name": "リバースランジ", "kind": "reps", "reps": 10},
        {"name": "リバースランジ", "kind": "reps", "reps": 10}]}, catalog)
    assert len(menu.to_json()["items"]) == 4


def test_minutes_repeat_the_interval_pattern(catalog):
    menu = clamp_menu({"area": "有酸素", "rounds": 1, "items": [], "after": [
        {"name": "エアロバイク", "kind": "time", "minutes": 8,
         "intervals": [{"level": "hard", "sec": 120}, {"level": "easy", "sec": 60}]}]}, catalog)
    ivs = menu.to_json()["after"][0]["intervals"]
    assert [(i["level"], i["sec"]) for i in ivs] == [
        ("hard", 120), ("easy", 60), ("hard", 120), ("easy", 60), ("hard", 120)]
    assert sum(i["sec"] for i in ivs) == 480
