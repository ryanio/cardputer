#!/usr/bin/env python3
"""Refill the simulator's reef: the Coral index and one payload per token.

The Reef view walks the tokens Coral has graded most recently and reads each
one whole, because a token payload carries the market facts, the inline score
and whoever called it first. That is one fetch per token, and the simulator
routes every one of them by its own address rather than by a wildcard, so a
demo can never show one token's numbers under another token's name.

Keeping thirty of those routes by hand is not work worth doing, so this writes
them. Routes it owns are marked "reef": true and are replaced whole on every
run. Every other route in the manifest is left alone.

    tools/fixtures/reef.py              refill from live, then bundle
    tools/fixtures/reef.py --limit 12   a smaller corpus

tools/apicheck/check.py is still what says whether the shape is the one the
firmware reads. This only captures.
"""

import argparse
import json
import os
import re
import ssl
import subprocess
import sys
import time
import urllib.error
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
FIXTURE_DIR = os.path.join(ROOT, "sim", "fixtures")
MANIFEST = os.path.join(FIXTURE_DIR, "manifest.json")
CA_ROOTS_H = os.path.join(ROOT, "src", "ca_roots.h")

INDEX_URL = "https://api.0xcoral.com/api/v1/tokens/index?limit=%d"
TOKEN_URL = "https://api.0xcoral.com/api/v1/tokens/%s/%s"
USER_AGENT = "flint-fixtures/1.0 (+https://github.com/ryanio/cardputer)"
TIMEOUT_S = 30

# Coral grades on request, so a burst of these wakes the container for each
# one. A pause between them is politeness, and going faster earns a 429, which
# is the API working rather than a fault. Being told to slow down means
# waiting, so the waits below are long on purpose. A capture that still comes
# back short is one to run again later, not one to hammer.
GAP_S = 1.5
BACKOFF_S = (20, 60, 120)


def context():
    """The roots out of src/ca_roots.h, which is the trust the device has."""
    with open(CA_ROOTS_H) as handle:
        pems = re.findall(r"-----BEGIN CERTIFICATE-----.*?-----END CERTIFICATE-----",
                          handle.read(), re.DOTALL)
    if not pems:
        raise SystemExit("no certificates found in %s" % CA_ROOTS_H)
    import tempfile
    bundle = tempfile.NamedTemporaryFile("w", suffix=".pem", delete=False)
    bundle.write("\n".join(pems) + "\n")
    bundle.close()
    return ssl.create_default_context(cafile=bundle.name)


def get(url, ssl_context, patient=False):
    request = urllib.request.Request(url, headers={
        "User-Agent": USER_AGENT,
        "Accept": "application/json",
    })
    for wait in BACKOFF_S if patient else ():
        try:
            with urllib.request.urlopen(request, timeout=TIMEOUT_S,
                                        context=ssl_context) as response:
                return json.loads(response.read())
        except urllib.error.HTTPError as err:
            if err.code != 429:
                raise
            print("       429, waiting %ds" % wait)
            time.sleep(wait)
    with urllib.request.urlopen(request, timeout=TIMEOUT_S, context=ssl_context) as response:
        return json.loads(response.read())


def write(name, data):
    path = os.path.join(FIXTURE_DIR, name + ".json")
    with open(path, "w") as handle:
        json.dump(data, handle, separators=(",", ":"), ensure_ascii=False)
        handle.write("\n")
    return os.path.getsize(path)


def slug(address):
    """A fixture name from the head of the address, the way the pinned ones read."""
    return "coral-token-" + re.sub(r"[^a-z0-9]", "", address.lower())[:10]


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--limit", type=int, default=24,
                        help="how many tokens to capture, default 24")
    parser.add_argument("--no-bundle", action="store_true",
                        help="skip regenerating sim/src/fixtures.h")
    args = parser.parse_args()

    ssl_context = context()

    with open(MANIFEST) as handle:
        manifest = json.load(handle)
    kept = [r for r in manifest["routes"] if not r.get("reef")]
    pinned = {r["fixture"] for r in kept}

    index = get(INDEX_URL % args.limit, ssl_context)
    tokens = index.get("tokens", [])
    if not tokens:
        raise SystemExit("the index came back empty, so there is nothing to capture")
    print("index: %d tokens, %d bytes" % (len(tokens), write("coral-index", index)))

    routes = []
    seen = set()
    callers = 0
    for i, token in enumerate(tokens):
        chain, address = token["chain"], token["address"]
        name = slug(address)
        if name in pinned or name in seen:
            # A token already routed by hand, or a head-of-address collision.
            # Either way the corpus has one fewer entry rather than two rows
            # answering for the same URL.
            print("  skip %s, already routed" % name)
            continue
        seen.add(name)
        if i:
            time.sleep(GAP_S)
        try:
            body = get(TOKEN_URL % (chain, address), ssl_context, patient=True)
        except Exception as err:  # noqa: BLE001 - a source that will not answer is a skip
            print("  skip %s/%s: %s" % (chain, address[:10], err))
            continue
        caller = body.get("firstCaller") or {}
        if caller.get("handle"):
            callers += 1
        size = write(name, body)
        routes.append({
            "match": "api.0xcoral.com/api/v1/tokens/%s/%s*" % (chain, address[:8]),
            "fixture": name,
            "reef": True,
        })
        print("  %-14s %-9s %5d bytes  %s" % (body.get("symbol") or "?", chain, size,
                                              caller.get("handle") or ""))

    if not routes:
        raise SystemExit("nothing captured, so the manifest is left as it was")

    # The score routes sit last among the Coral rows and nothing here matches
    # them, so appending after the kept rows keeps first-match-wins honest.
    manifest["routes"] = kept + routes
    # A capture that reaches fewer tokens than the last one leaves its
    # predecessors on disk, unrouted, where bundle.py never looks and a reader
    # would take them for corpus. Sweep them.
    routed = {r["fixture"] for r in manifest["routes"]}
    for entry in sorted(os.listdir(FIXTURE_DIR)):
        name, ext = os.path.splitext(entry)
        if ext == ".json" and name.startswith("coral-token-") and name not in routed:
            os.remove(os.path.join(FIXTURE_DIR, entry))
            print("  swept %s, no longer routed" % name)

    with open(MANIFEST, "w") as handle:
        json.dump(manifest, handle, indent=2, ensure_ascii=False)
        handle.write("\n")
    print("\n%d tokens, %d of them called by somebody, %d routes written"
          % (len(routes), callers, len(routes)))

    if not args.no_bundle:
        subprocess.check_call([sys.executable, os.path.join(ROOT, "tools", "fixtures",
                                                            "bundle.py")])
    return 0


if __name__ == "__main__":
    sys.exit(main())
