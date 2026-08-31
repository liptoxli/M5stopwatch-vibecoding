#!/usr/bin/env python3
"""Prepare private sync configuration locally. No upload, install or credential output."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import secrets
from urllib.parse import urlsplit
import uuid


def prepare(endpoint, output, devices, account_id):
    url = urlsplit(endpoint)
    if url.scheme != "https" or not url.hostname or url.username or url.password or url.query or url.fragment:
        raise ValueError("Use an HTTPS endpoint without embedded credentials, query or fragment")
    if not isinstance(account_id, str) or not account_id or not 1 <= devices <= 20:
        raise ValueError("A logged-in account and 1-20 devices are required")
    output = Path(output).expanduser().resolve()
    repo = Path(__file__).resolve().parents[2]
    if output == repo or repo in output.parents:
        raise ValueError("Choose a private output directory outside the repository")
    # Never replace an existing key, device ID or configuration.
    previous_umask = os.umask(0o077)
    try:
        output.mkdir(mode=0o700, parents=True, exist_ok=False)
        scope = hashlib.sha256(("m5-codex-account-v1:" + account_id).encode()).hexdigest()
        writer, reader = secrets.token_urlsafe(48), secrets.token_urlsafe(48)
        for index in range(1, devices + 1):
            config = {"endpoint": endpoint, "token": writer, "account_scope": scope, "device_id": str(uuid.uuid4())}
            (output / f"device-{index}-cloud-sync.json").write_text(json.dumps(config, indent=2) + "\n")
        (output / "server.env").write_text(
            f"CODEX_SYNC_ACCOUNT_SCOPE={scope}\nCODEX_SYNC_TOKEN={writer}\nCODEX_SYNC_READ_TOKEN={reader}\n"
            "CODEX_SYNC_DATA_DIR=./data\nPORT=4199\n")
    finally:
        os.umask(previous_umask)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--endpoint", required=True)
    parser.add_argument("--output", required=True, help="new private directory outside the repository")
    parser.add_argument("--devices", type=int, default=2)
    args = parser.parse_args()
    try:
        auth = json.loads((Path.home() / ".codex/auth.json").read_text())
        prepare(args.endpoint, args.output, args.devices, auth["tokens"]["account_id"])
    except (OSError, ValueError, KeyError, TypeError):
        raise SystemExit("STOP: check local Codex login, HTTPS endpoint and a new private output directory; nothing was uploaded.") from None
    print("Private configuration prepared. No credentials printed and no app or server changed.")


if __name__ == "__main__":
    main()
