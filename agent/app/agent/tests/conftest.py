import warnings
from pathlib import Path

import boto3
import pytest
from moto import mock_aws
from pydantic.warnings import PydanticDeprecatedSince20

# bedrock_agentcore の runtime/context.py が読み込み時に出す pydantic の警告だけを抑える。
# 一度読み込めばモジュールはキャッシュされるので、-W error のままテストを実行できる。
with warnings.catch_warnings():
    warnings.simplefilter("ignore", PydanticDeprecatedSince20)
    import bedrock_agentcore.runtime  # noqa: F401

from catalog import load_catalog
from store import Store

TABLE = "healthwatch-test"


@pytest.fixture
def catalog():
    return load_catalog(Path(__file__).parent.parent / "catalog.example.json")


@pytest.fixture
def store():
    with mock_aws():
        client = boto3.client("dynamodb", region_name="ap-northeast-1")
        client.create_table(
            TableName=TABLE,
            KeySchema=[{"AttributeName": "pk", "KeyType": "HASH"},
                       {"AttributeName": "sk", "KeyType": "RANGE"}],
            AttributeDefinitions=[{"AttributeName": "pk", "AttributeType": "S"},
                                  {"AttributeName": "sk", "AttributeType": "S"}],
            BillingMode="PAY_PER_REQUEST",
        )
        yield Store(TABLE, client)
