from datetime import UTC, datetime


def test_two_workouts_same_day_both_kept_and_last_wins(store):
    store.put_workout({"area": "脚"}, datetime(2026, 11, 26, 8, tzinfo=UTC))
    store.put_workout({"area": "上半身と体幹"}, datetime(2026, 11, 26, 20, tzinfo=UTC))
    assert store.last_workout()["area"] == "上半身と体幹"


def test_menu_numbers_survive_round_trip(store):
    menu = {"area": "脚", "rounds": 2, "items": [
        {"name": "スクワット", "kind": "reps", "reps": 10},
        {"name": "プランク", "kind": "time", "intervals": [{"level": 1, "sec": 30}]},
    ], "factor": 1.5}
    store.put_workout(menu, datetime(2026, 11, 26, tzinfo=UTC))
    got = store.last_workout()
    assert got == menu
    assert isinstance(got["rounds"], int)
    assert isinstance(got["factor"], float)


def test_empty_history(store):
    assert store.last_workout() is None


def test_last_by_area_keeps_newest_per_area(store):
    store.put_workout({"area": "脚", "rounds": 2}, datetime(2026, 11, 23, tzinfo=UTC))
    store.put_workout({"area": "脚", "rounds": 3}, datetime(2026, 11, 25, tzinfo=UTC))
    store.put_workout({"area": "上半身と体幹", "rounds": 3}, datetime(2026, 11, 24, tzinfo=UTC))
    assert store.last_by_area() == {"脚": {"area": "脚", "rounds": 3}, "上半身と体幹": {"area": "上半身と体幹", "rounds": 3}}
