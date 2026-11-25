"""Write fleet-release.json, the signed description of a Fleet release that Fleet's updater reads."""
import argparse
import hashlib
import json
import os
import sys
from pathlib import Path

RELEASE_FORMAT = 1
RELEASE_DOMAIN = "unbound-release-v1"
PLATFORMS = ("windows_x64", "linux_x64", "darwin")
KEY_VARIABLE = "FLEET_RELEASE_SIGNING_KEY"


def describe(version, page, release_base, assets):
    entries = {}
    for platform, path in assets.items():
        if platform not in PLATFORMS:
            raise ValueError(f"'{platform}' is not one of {', '.join(PLATFORMS)}")
        entries[platform] = {"file": path.name, "url": f"{release_base}/{path.name}", "size": path.stat().st_size,
                             "hash": hashlib.blake2b(path.read_bytes(), digest_size=64).hexdigest()}
    return {"format": RELEASE_FORMAT, "version": version, "page": page, "assets": entries}


def sign(payload):
    from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey

    seed = os.environ.get(KEY_VARIABLE, "").strip()
    if len(seed) != 64:
        raise ValueError(f"{KEY_VARIABLE} must hold the 32-byte private key as hex")
    key = Ed25519PrivateKey.from_private_bytes(bytes.fromhex(seed))
    signature = key.sign(f"{RELEASE_DOMAIN}\n{payload}".encode("utf-8"))
    return json.dumps({"payload": payload, "signature": signature.hex()})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", required=True)
    parser.add_argument("--page", required=True)
    parser.add_argument("--release-base", required=True)
    parser.add_argument("--asset", action="append", default=[], metavar="PLATFORM=PATH")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    assets = {}
    for entry in args.asset:
        platform, _, path = entry.partition("=")
        assets[platform] = Path(path)
    payload = json.dumps(describe(args.version, args.page, args.release_base.rstrip("/"), assets), indent=2)
    args.output.write_text(sign(payload) + "\n", encoding="utf-8")
    print(payload)


if __name__ == "__main__":
    try:
        main()
    except ValueError as error:
        sys.exit(f"error: {error}")
