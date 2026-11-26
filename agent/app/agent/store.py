from datetime import UTC, datetime
from decimal import Decimal

import boto3
from boto3.dynamodb.types import TypeDeserializer, TypeSerializer

_ser = TypeSerializer()
_deser = TypeDeserializer()


def _to_ddb(v):
    if isinstance(v, float):
        return Decimal(str(v))
    if isinstance(v, dict):
        return {k: _to_ddb(x) for k, x in v.items()}
    if isinstance(v, (list, tuple)):
        return [_to_ddb(x) for x in v]
    return v


def _from_ddb(v):
    if isinstance(v, Decimal):
        return int(v) if v == v.to_integral_value() else float(v)
    if isinstance(v, dict):
        return {k: _from_ddb(x) for k, x in v.items()}
    if isinstance(v, list):
        return [_from_ddb(x) for x in v]
    return v


class Store:
    """メニューをDynamoDBテーブルに保存する。

    pk は "workout"。sk はUTCのISO 8601（ミリ秒まで）。
    メニューは M 型（map）で保存し、数値は Decimal にして読み出しで int / float に戻す。
    """

    def __init__(self, table_name: str, client=None):
        self._table = table_name
        self._client = client or boto3.client("dynamodb")

    def put_workout(self, menu: dict, at: datetime) -> None:
        self._put("workout", at, {"menu": _ser.serialize(_to_ddb(menu))})

    def last_workout(self) -> dict | None:
        items = self._latest("workout", 1)
        return _from_ddb(_deser.deserialize(items[0]["menu"])) if items else None

    def last_by_area(self, limit: int = 30) -> dict[str, dict]:
        """直近 limit 回のメニューから、部位ごとに最後のメニューを返す。"""
        out: dict[str, dict] = {}
        for item in self._latest("workout", limit):
            menu = _from_ddb(_deser.deserialize(item["menu"]))
            out.setdefault(menu.get("area", ""), menu)
        return out

    def _put(self, pk: str, at: datetime, attrs: dict) -> None:
        sk = at.astimezone(UTC).isoformat(timespec="milliseconds").replace("+00:00", "Z")
        self._client.put_item(
            TableName=self._table,
            Item={"pk": {"S": pk}, "sk": {"S": sk}, **attrs},
        )

    def _latest(self, pk: str, limit: int) -> list[dict]:
        res = self._client.query(
            TableName=self._table,
            KeyConditionExpression="pk = :pk",
            ExpressionAttributeValues={":pk": {"S": pk}},
            ScanIndexForward=False,
            Limit=limit,
        )
        return res["Items"]
