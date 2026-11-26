# PlatformIO pre-script: writes src/env_config.h from the repository's .env
# (WIFI_SSID, WIFI_PASS and the HW_* agent settings). The header is git-ignored.
import json
from pathlib import Path
from urllib.parse import quote

Import("env")  # noqa: F821 (provided by PlatformIO)

project = Path(env.subst("$PROJECT_DIR"))  # noqa: F821
values = {}
dotenv = project.parent / ".env"
if dotenv.exists():
    for line in dotenv.read_text().splitlines():
        if "=" in line and not line.lstrip().startswith("#"):
            k, v = line.split("=", 1)
            v = v.strip()
            if len(v) >= 2 and v[0] == v[-1] and v[0] in "\"'":
                v = v[1:-1]
            values[k.strip()] = v

# Cognito user for InitiateAuth and the AgentCore Runtime invocation URL
keys = ["WIFI_SSID", "WIFI_PASS", "HW_USER_POOL_CLIENT_ID", "HW_USERNAME", "HW_PASSWORD", "HW_RUNTIME_ARN"]
missing = [k for k in keys if not values.get(k)]
if missing:
    print(f"env_config: missing in {dotenv}: {', '.join(missing)} (see .env.example)")
arn = values.get("HW_RUNTIME_ARN", "")
hw_url = (
    f"https://bedrock-agentcore.ap-northeast-1.amazonaws.com/runtimes/{quote(arn, safe='')}/invocations?qualifier=DEFAULT"
    if arn
    else ""
)
defines = {
    "WIFI_SSID": values.get("WIFI_SSID", ""),
    "WIFI_PASS": values.get("WIFI_PASS", ""),
    "HW_RUNTIME_URL": hw_url,
    "HW_CLIENT_ID": values.get("HW_USER_POOL_CLIENT_ID", ""),
    "HW_USERNAME": values.get("HW_USERNAME", ""),
    "HW_PASSWORD": values.get("HW_PASSWORD", ""),
}
lines = ["// Generated from .env by scripts/env_config.py. Do not edit."]
lines += ["#pragma once"]
lines += [f"#define {k} {json.dumps(v, ensure_ascii=False)}" for k, v in defines.items()]
text = "\n".join(lines) + "\n"
out = project / "src" / "env_config.h"
if not out.exists() or out.read_text() != text:
    out.write_text(text)
