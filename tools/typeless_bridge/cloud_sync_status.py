#!/usr/bin/env python3
"""Read-only cloud sync status. Never prints tokens, account IDs or activity timestamps."""
import hashlib
import json
from pathlib import Path
from urllib.request import HTTPRedirectHandler, Request, build_opener


class NoRedirect(HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


def main():
    support = Path.home() / "Library/Application Support/M5StopWatch/StopWatchBleBridge"
    config = json.loads((support / "cloud-sync.json").read_text())
    account = json.loads((Path.home() / ".codex/auth.json").read_text())["tokens"]["account_id"]
    expected = hashlib.sha256(f"m5-codex-account-v1:{account}".encode()).hexdigest()
    if config["account_scope"] != expected or not config["endpoint"].startswith("https://"):
        raise SystemExit("STOP: account or endpoint mismatch")
    request = Request(config["endpoint"], headers={"Authorization": "Bearer " + config["token"],
                                                  "User-Agent": "StopWatch-BLE-Bridge/1.4.0", "Accept": "application/json"})
    with build_opener(NoRedirect()).open(request, timeout=10) as response:
        shared = json.load(response)["shared_usage"]
    daily = shared["daily_tracking"]
    output = {"cloud_access": "ok", "as_of": shared["as_of"], "remaining": shared["weekly"]["left_pct"],
              "today_used_points": daily["used_since_start_pct_points"], "day": daily["day_key"],
              "quality": daily["quality"], "cells": len(shared["activity_buckets"]), "stale": shared["stale"]}
    state = support / "cloud-sync-state.json"
    if state.exists():
        cache = json.loads(state.read_text())
        output["pending_observations"] = len(cache["observations"])
        output["cached_remaining"] = (cache.get("snapshot") or {}).get("weekly", {}).get("left_pct")
    print(json.dumps(output, ensure_ascii=False))


if __name__ == "__main__":
    main()
