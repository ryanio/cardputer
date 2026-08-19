#!/usr/bin/env python3
"""Prove the five sources still answer the way the firmware expects.

Nothing here is a mock. Every check makes the real request, over TLS trusted by
the exact roots in src/ca_roots.h, and asserts the fields and invariants the
device reads. A source that changes shape fails this before it fails on a unit
somebody is holding.

    tools/apicheck/check.py                 everything
    tools/apicheck/check.py bankr coral     one or more sources
    tools/apicheck/check.py --save          refresh the simulator fixtures too
    tools/apicheck/check.py --system-roots  trust the OS instead of ca_roots.h

Standard library only, and it runs on the 3.9 that ships with macOS.
"""

import argparse
import json
import os
import re
import ssl
import sys
import tempfile
import time
import urllib.error
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CA_ROOTS_H = os.path.join(ROOT, "src", "ca_roots.h")
FIXTURE_DIR = os.path.join(ROOT, "sim", "fixtures")

USER_AGENT = "cardputer-flint/0.1.0 (apicheck)"
TIMEOUT_S = 20

NUM = (int, float)

# Terminal colors, off when the output is a pipe or CI asked for plain text.
COLOR = sys.stdout.isatty() and os.environ.get("NO_COLOR") is None


def paint(text, code):
    return "\033[%sm%s\033[0m" % (code, text) if COLOR else text


class Field:
    """One value the firmware reads, and what it is allowed to be.

    The path walks dicts with dots and every element of a list with `[]`, so
    `profiles[].tokenAddress` checks all of them. Three kinds of slack, each
    one earned by something a source actually does:

      nullable   the key is there and the value is null. gwei sends
                 ethPriceUsd: null when no price could be fetched.
      sometimes  present on at least one element, absent on others. One Bankr
                 profile out of 113 carries no token at all.
      optional   may be missing everywhere. Only for things nothing reads.
    """

    def __init__(self, path, kind, nullable=False, sometimes=False, optional=False, note=""):
        self.path = path
        self.kind = kind if isinstance(kind, tuple) else (kind,)
        self.nullable = nullable
        self.sometimes = sometimes
        self.optional = optional
        self.note = note

    def kind_name(self):
        names = {int: "int", float: "float", str: "string", list: "list", dict: "object",
                 bool: "bool"}
        return " or ".join(names.get(k, k.__name__) for k in self.kind)

    def check(self, data):
        seen = 0
        missing = []
        problems = []
        for where, value, present in walk(data, self.path):
            if not present:
                missing.append(where)
                continue
            seen += 1
            if value is None:
                if not self.nullable:
                    problems.append("%s is null" % where)
                continue
            # bool is an int in Python and never what a numeric field means.
            if isinstance(value, bool) and bool not in self.kind:
                problems.append("%s is a bool, expected %s" % (where, self.kind_name()))
            elif not isinstance(value, self.kind):
                problems.append("%s is %s, expected %s"
                                % (where, type(value).__name__, self.kind_name()))
        if self.optional:
            return problems
        if self.sometimes:
            if seen == 0:
                problems.append("%s is missing from every one of them" % self.path)
            return problems
        for where in missing:
            problems.append("%s is missing" % where)
        return problems


def walk(data, path):
    """Yield (readable path, value, present) for every leaf a path reaches."""
    nodes = [("", data, True)]
    for part in path.split("."):
        each = part.endswith("[]")
        key = part[:-2] if each else part
        stepped = []
        for where, node, present in nodes:
            label = key if not where else "%s.%s" % (where, key)
            if not present:
                stepped.append((label, None, False))
                continue
            if not isinstance(node, dict) or key not in node:
                stepped.append((label, None, False))
                continue
            value = node[key]
            if not each:
                stepped.append((label, value, True))
            elif not isinstance(value, list):
                stepped.append((label, value, True))  # the list check catches it
            else:
                for i, item in enumerate(value):
                    stepped.append(("%s[%d]" % (label, i), item, True))
        nodes = stepped
    return nodes


class Check:
    def __init__(self, source, url, fields=(), rules=(), fixture=None, tolerate=()):
        self.source = source
        self.url = url
        self.fields = fields
        self.rules = rules
        self.fixture = fixture  # sim/fixtures name, when --save should keep it
        self.tolerate = tolerate  # HTTP statuses that are a skip, not a failure


def get(url, context):
    request = urllib.request.Request(url, headers={
        "User-Agent": USER_AGENT,
        "Accept": "application/json",
    })
    started = time.time()
    with urllib.request.urlopen(request, timeout=TIMEOUT_S, context=context) as response:
        body = response.read()
    return body, int((time.time() - started) * 1000)


def ca_bundle_path():
    """The roots out of ca_roots.h, written where an SSL context can read them.

    Verifying against these and nothing else is the point: it is the same trust
    the device has, so a chain that rotates onto a root we do not ship fails
    here rather than on a unit in somebody's pocket.
    """
    with open(CA_ROOTS_H) as handle:
        source = handle.read()
    pems = re.findall(r"-----BEGIN CERTIFICATE-----.*?-----END CERTIFICATE-----", source,
                      re.DOTALL)
    if not pems:
        raise SystemExit("no certificates found in %s" % CA_ROOTS_H)
    handle = tempfile.NamedTemporaryFile("w", suffix=".pem", delete=False)
    handle.write("\n".join(pems) + "\n")
    handle.close()
    return handle.name, len(pems)


# ---------------------------------------------------------------- invariants

def rule(name):
    def wrap(fn):
        fn.rule_name = name
        return fn
    return wrap


@rule("three speeds, ordered fast normal cheap")
def gas_speeds(data):
    order = [s.get("speed") for s in data.get("speeds", [])]
    if order != ["fast", "normal", "cheap"]:
        return "speeds are %s" % order


@rule("history is thinned to about a point a minute")
def history_spacing(data):
    points = data.get("points", [])
    if len(points) < 3:
        return "only %d points" % len(points)
    gaps = [points[i + 1]["t"] - points[i]["t"] for i in range(len(points) - 1)]
    typical = sorted(gaps)[len(gaps) // 2]
    if not 20_000 <= typical <= 300_000:
        return "median gap is %dms, so a 240px sparkline would need downsampling" % typical


@rule("most history points carry a tip, not just a base fee")
def history_tips(data):
    points = data.get("points", [])
    withTip = [p for p in points if isinstance(p.get("tip"), NUM)]
    # The device draws the tip as its own series. Points written before it was
    # recorded are legitimately without one, so this catches the field going
    # away rather than an old point missing it.
    if len(points) and len(withTip) * 2 < len(points):
        return "only %d of %d points carry a tip" % (len(withTip), len(points))


@rule("history points arrive in time order")
def history_ordered(data):
    points = data.get("points", [])
    times = [p.get("t") for p in points if isinstance(p.get("t"), int)]
    if any(times[i] > times[i + 1] for i in range(len(times) - 1)):
        return "out of order, and the sparkline plots them along a time axis, so it would zigzag"


@rule("colors are a form ui::parseColor takes")
def color_shape(data):
    # Both forms are real and hex is the more common of the two: a check
    # written from one bot said hsl, and two of every three bots sampled came
    # back as #rrggbb. The device takes either now.
    hsl = r"^hsl\(\s*-?[\d.]+\s*,\s*[\d.]+%\s*,\s*[\d.]+%\s*\)$"
    hexed = r"^#(?:[0-9a-fA-F]{3}|[0-9a-fA-F]{6})$"
    colors = data.get("bot", {}).get("unicode", {}).get("colors", {})
    for key in ("background", "text"):
        value = str(colors.get(key, ""))
        if not re.match(hsl, value) and not re.match(hexed, value):
            return "%s is %r, which is neither hsl() nor #rrggbb" % (key, value)


@rule("rarity sort runs from the most ordinary towards rank 1")
def rarity_ascends(data):
    ranks = [b.get("rarityRank") for b in data.get("bots", [])]
    ranks = [r for r in ranks if isinstance(r, int)]
    if len(ranks) < 2:
        return "fewer than two ranked bots came back"
    if ranks != sorted(ranks, reverse=True):
        return "ranks are not descending: %s" % ranks
    return None


@rule("the tail of the rarity sort is the rarest end")
def rarity_tail(data):
    ranks = [b.get("rarityRank") for b in data.get("bots", [])]
    ranks = [r for r in ranks if isinstance(r, int)]
    # The cursor is counted back from a total that only burns can move, so the
    # last page lands on rank 1 or a little short of it. Far from 1 means the
    # collection changed size and the fixture is stale.
    if not ranks or min(ranks) > 6:
        return "the last page of the rarity sort stops at rank %s" % (min(ranks) if ranks else "?")
    return None


@rule("every minted artifact names the bot it came off")
def recent_bots(data):
    items = data.get("items", [])
    if not items:
        return "no recent mints at all"
    for i, item in enumerate(items):
        token = item.get("botTokenId")
        if not isinstance(token, int) or token < 1:
            return "items[%d].botTokenId is %r" % (i, token)
    return None


@rule("the art still fits the four by seven grid the panel draws")
def art_grid(data):
    lines = data.get("bot", {}).get("unicode", {}).get("textContent", [])
    if not lines:
        return "no lines"
    if len(lines) > 4:
        return "%d lines, and four cells of 32 already fill 128 of the 135 rows" % len(lines)
    widest = max(len(line) for line in lines)
    if widest > 7:
        return "widest line is %d glyphs, and seven cells of 32 already fill 224 of 240" % widest


@rule("every ability states one cost, a cooldown or a resource")
def ability_cost(data):
    for ability in data.get("story", {}).get("arc", {}).get("abilities", []):
        if not ability.get("cooldown") and not ability.get("resource"):
            return "%s states neither" % ability.get("name")


@rule("the score is 0 to 100 and carries its caveats")
def score_sane(data):
    score = data.get("score")
    if not isinstance(score, NUM) or not 0 <= score <= 100:
        return "score is %r" % score
    if not data.get("explanation", {}).get("caveats"):
        return "no caveats, and CLAUDE.md says every score shows them"


@rule("the daily round hides nothing the device needs to play offline")
def daily_playable(data):
    if not data.get("clues"):
        return "no clues"
    if not data.get("answer", {}).get("explanation", {}).get("caveats"):
        return "the answer carries no caveats"
    if not re.match(r"^\d{4}-\d{2}-\d{2}$", str(data.get("date"))):
        return "date is %r" % data.get("date")


@rule("the daily round states top 10 concentration as a share of one")
def daily_share_scale(data):
    share = data.get("clues", {}).get("top10HolderShare")
    if share is not None and not 0 <= share <= 1:
        return ("top10HolderShare is %s, and the round view multiplies it by 100, so this would "
                "draw a concentration in the thousands of percent" % share)


@rule("the token endpoint states the same thing as a percent")
def token_share_scale(data):
    pct = data.get("holders", {}).get("topHoldersExInfraPct")
    if pct is not None and not 0 <= pct <= 100:
        return ("topHoldersExInfraPct is %s. The two endpoints disagree about scale on purpose "
                "and the views correct for it, so a change here draws a wrong number" % pct)


@rule("profiles come back ordered by market cap, descending")
def bankr_ordered(data):
    caps = [p.get("marketCapUsd") for p in data.get("profiles", [])]
    caps = [c for c in caps if isinstance(c, NUM)]
    if any(caps[i] < caps[i + 1] for i in range(len(caps) - 1)):
        return "the leaderboard is not sorted, so the view cannot trust the order"


@rule("every token address is a chain Coral can score, or is flagged")
def bankr_chains(data):
    chains = {p.get("tokenChainId") for p in data.get("profiles", [])}
    unknown = chains - {"base", "robinhood"}
    if unknown:
        return ("new chain %s: the view sends base tokens to Coral and says so for the rest, "
                "so decide which this is" % sorted(unknown))


@rule("at least one profile carries a scoreable token")
def bankr_scoreable(data):
    for p in data.get("profiles", []):
        if p.get("tokenChainId") == "base" and p.get("tokenAddress", "").startswith("0x"):
            return None
    return "none of them, so the Coral cross has nothing to score"


@rule("the wrapper flag is the thing to check, not the status")
def womp_success(data):
    if data.get("success") is not True:
        return "success is %r" % data.get("success")


@rule("images come from the host our roots cover")
def womp_host(data):
    for womp in data.get("womps", []):
        url = womp.get("image_url", "")
        if not url.startswith("https://media.crvox.com/"):
            return "image_url is %s, and the ISRG root only covers media.crvox.com" % url


# -------------------------------------------------------------------- checks

def build_checks():
    return [
        Check(
            "gwei", "https://gwei.ryanio.com/api/gas",
            fixture="gwei-gas",
            fields=[
                Field("baseFeeGwei", NUM),
                Field("speeds", list),
                Field("speeds[].speed", str),
                Field("speeds[].label", str),
                Field("speeds[].eta", str),
                Field("speeds[].totalGwei", NUM),
                Field("speeds[].usdPerTransfer", NUM, nullable=True),
                Field("ethPriceUsd", NUM, nullable=True, note="null when no price landed"),
                Field("blockNumber", int),
                Field("updatedAt", int),
            ],
            rules=[gas_speeds],
        ),
        Check(
            "gwei", "https://gwei.ryanio.com/api/gas/history",
            fixture="gwei-history",
            fields=[
                Field("points", list),
                Field("points[].t", int),
                Field("points[].gwei", NUM),
                Field("points[].tip", NUM, nullable=True),
                Field("low24h", NUM, nullable=True),
                Field("high24h", NUM, nullable=True),
            ],
            rules=[history_spacing, history_ordered, history_tips],
        ),
        Check(
            "glyphbots", "https://www.glyphbots.com/api/bot/1",
            fixture="glyphbots-bot",
            fields=[
                Field("bot.tokenId", int),
                Field("bot.name", str),
                Field("bot.rarityRank", int, nullable=True),
                Field("bot.unicode.textContent", list),
                Field("bot.unicode.colors.background", str),
                Field("bot.unicode.colors.text", str),
                Field("bot.traits[].trait_type", str),
                Field("bot.traits[].value", str),
                Field("bot.burnedAt", str, nullable=True),
                Field("bot.royalties.mintCount", int),
            ],
            rules=[color_shape, art_grid],
        ),
        Check(
            "glyphbots", "https://www.glyphbots.com/api/bot/4242",
            fixture="glyphbots-bot-4242",
            fields=[
                Field("bot.tokenId", int),
                Field("bot.name", str),
                Field("bot.unicode.textContent", list),
                Field("bot.unicode.colors.background", str),
                Field("bot.unicode.colors.text", str),
            ],
            rules=[color_shape, art_grid],
        ),
        Check(
            "glyphbots", "https://www.glyphbots.com/api/bot/1/story",
            fixture="glyphbots-story",
            fields=[
                Field("story.arc.title", str),
                Field("story.arc.role", str),
                Field("story.arc.faction", str),
                Field("story.arc.mission.objective", str),
                Field("story.arc.mission.threat", str),
                Field("story.arc.abilities[].name", str),
                Field("story.arc.abilities[].effect", str),
                # Two of the three abilities cost time and the third costs a
                # resource, so an ability has one of these fields, never both.
                Field("story.arc.abilities[].cooldown", str, sometimes=True),
                Field("story.arc.abilities[].resource", str, sometimes=True),
            ],
            rules=[ability_cost],
        ),
        # Browsing. The list is six wide on a panel this size, so that is what
        # gets asked for and what the fixtures carry.
        Check(
            "glyphbots", "https://www.glyphbots.com/api/bots/search?limit=6",
            fixture="glyphbots-search-new",
            fields=[
                Field("total", int),
                Field("bots[].tokenId", int),
                Field("bots[].name", str),
                Field("bots[].rarityRank", int, nullable=True),
            ],
        ),
        Check(
            "glyphbots", "https://www.glyphbots.com/api/bots/search?limit=6&sort=rarity",
            fixture="glyphbots-search-common",
            fields=[
                Field("total", int),
                Field("bots[].tokenId", int),
                Field("bots[].name", str),
                Field("bots[].rarityRank", int),
            ],
            rules=[rarity_ascends],
        ),
        Check(
            "glyphbots",
            "https://www.glyphbots.com/api/bots/search?limit=6&sort=rarity&cursor=11105",
            fixture="glyphbots-search-rare",
            fields=[
                Field("total", int),
                Field("bots[].tokenId", int),
                Field("bots[].name", str),
                Field("bots[].rarityRank", int),
            ],
            rules=[rarity_ascends, rarity_tail],
        ),
        Check(
            "glyphbots", "https://www.glyphbots.com/api/artifacts/recent",
            fixture="glyphbots-recent",
            fields=[
                Field("items[].botTokenId", int),
                Field("items[].title", str),
                Field("items[].mintedAt", str),
                Field("items[].mintQuantity", int),
            ],
            rules=[recent_bots],
        ),
        Check(
            "coral", "https://api.0xcoral.com/api/v1/resolve?q=MEME",
            fixture="coral-resolve",
            fields=[
                Field("query", str),
                Field("resolved.address", str),
                Field("resolved.chain", str),
                Field("resolved.symbol", str),
            ],
        ),
        Check(
            "coral",
            "https://api.0xcoral.com/api/v1/tokens/base/"
            "0x9f86db9fc6f7c9408e8fda3ff8ce4e78ac7a6b07",
            fixture="coral-token",
            fields=[
                Field("symbol", str),
                Field("chain", str),
                Field("market.marketCapUsd", NUM, nullable=True),
                Field("market.liquidityUsd", NUM, nullable=True),
                Field("market.volume24hUsd", NUM, nullable=True),
                Field("market.priceChange24hPct", NUM, nullable=True),
                Field("market.priceUsd", NUM, nullable=True),
                Field("holders.count", int, nullable=True),
                Field("holders.topHoldersExInfraPct", NUM, nullable=True),
            ],
            rules=[token_share_scale],
        ),
        Check(
            "coral",
            "https://api.0xcoral.com/api/v1/score/base/"
            "0xc52aedec3374422d7510e294cfaa90799595cba3",
            fixture="coral-score",
            # A full lookup that self rate limits, and the view spinners through
            # one at a time. Being told to slow down is the API working.
            tolerate=(429,),
            fields=[
                Field("score", NUM),
                Field("verdict", str),
                Field("confidence", NUM),
                Field("confidenceLabel", str),
                Field("explanation.headline", str),
                Field("explanation.bullets", list),
                Field("explanation.caveats", list),
            ],
            rules=[score_sane],
        ),
        Check(
            "coral", "https://api.0xcoral.com/api/v1/guess/daily",
            fixture="coral-daily",
            fields=[
                Field("date", str),
                Field("clues.holderCount", int, nullable=True),
                Field("clues.liquidityUsd", NUM, nullable=True),
                Field("clues.marketCapUsd", NUM, nullable=True),
                Field("clues.top10HolderShare", NUM, nullable=True),
                Field("clues.uniqueBuyers24h", int, nullable=True),
                Field("clues.uniqueSellers24h", int, nullable=True),
                Field("answer.score", NUM),
                Field("answer.verdict", str),
                Field("answer.confidenceLabel", str),
                Field("answer.explanation.headline", str),
                Field("answer.explanation.bullets", list),
                Field("answer.explanation.caveats", list),
                Field("token.chain", str),
                Field("token.address", str),
            ],
            rules=[daily_playable, daily_share_scale],
        ),
        Check(
            "coral", "https://api.0xcoral.com/api/v1/tokens/index?limit=8",
            fixture="coral-index",
            fields=[
                Field("tokens[].chain", str),
                Field("tokens[].address", str),
            ],
        ),
        Check(
            "bankr", "https://api.bankr.bot/agent-profiles?limit=12",
            fixture="bankr-profiles",
            fields=[
                Field("total", int),
                Field("limit", int),
                Field("offset", int),
                Field("profiles", list),
                Field("profiles[].slug", str),
                Field("profiles[].projectName", str),
                Field("profiles[].tokenChainId", str),
                # One profile of the 113 has no token at all, so the view draws
                # a row for it without a score rather than skipping it.
                Field("profiles[].tokenSymbol", str, sometimes=True),
                Field("profiles[].tokenAddress", str, sometimes=True),
                Field("profiles[].marketCapUsd", NUM, sometimes=True),
                Field("profiles[].vol24hUsd", NUM, sometimes=True),
                Field("profiles[].weeklyRevenueWeth", str, sometimes=True,
                      note="a decimal string, not a number"),
                Field("profiles[].description", str, sometimes=True),
                Field("profiles[].twitterUsername", str, sometimes=True),
                Field("profiles[].productsCount", int),
            ],
            rules=[bankr_ordered, bankr_chains, bankr_scoreable],
        ),
        Check(
            "bankr", "https://api.bankr.bot/agent-profiles/surplus-intelligence",
            fixture="bankr-profile",
            fields=[
                Field("slug", str),
                Field("projectName", str),
                Field("tokenChainId", str),
                Field("tokenAddress", str),
            ],
        ),
        Check(
            "voxels", "https://www.voxels.com/api/womps/81301.json",
            fixture="voxels-womp-81301",
            fields=[
                Field("success", bool),
                Field("womp.id", int),
                Field("womp.image_url", str),
                Field("womp.author.name", str, nullable=True),
                Field("womp.coords", str, nullable=True),
                Field("womp.parcel_address", str, nullable=True),
                Field("womp.parcel_island", str, nullable=True),
                Field("womp.created_at", str),
            ],
            # The list endpoint ignores offset, so browsing walks ids down and
            # this is the call that makes that possible.
            rules=[womp_success],
        ),
        Check(
            "voxels", "https://www.voxels.com/api/womps.json?limit=3",
            fixture="voxels-womps",
            fields=[
                Field("success", bool),
                Field("womps[].id", int),
                Field("womps[].author.name", str, nullable=True),
                Field("womps[].image_url", str),
                Field("womps[].coords", str, nullable=True),
                Field("womps[].parcel_address", str, nullable=True),
                Field("womps[].parcel_island", str, nullable=True),
                Field("womps[].created_at", str),
            ],
            rules=[womp_success, womp_host],
        ),
    ]


# --------------------------------------------------------------------- runner

def run(checks, context, save):
    failures = 0
    skips = 0
    source = None
    for check in checks:
        if check.source != source:
            source = check.source
            print("\n" + paint(source, "1"))

        label = check.url.split("//", 1)[1].split("/", 1)[1] or "/"
        if len(label) > 52:
            label = label[:49] + "..."

        try:
            body, ms = get(check.url, context)
        except urllib.error.HTTPError as err:
            if err.code in check.tolerate:
                print("  %s %-52s http %d, which is the API working" %
                      (paint("skip", "33"), label, err.code))
                skips += 1
                continue
            print("  %s %-52s http %d" % (paint("fail", "31"), label, err.code))
            failures += 1
            continue
        except urllib.error.URLError as err:
            reason = err.reason
            hint = ""
            if isinstance(reason, ssl.SSLCertVerificationError):
                hint = "  <- this chain no longer verifies against src/ca_roots.h"
            print("  %s %-52s %s%s" % (paint("fail", "31"), label, reason, hint))
            failures += 1
            continue

        try:
            data = json.loads(body)
        except ValueError as err:
            print("  %s %-52s not json: %s" % (paint("fail", "31"), label, err))
            failures += 1
            continue

        problems = []
        for field in check.fields:
            problems += field.check(data)
        for fn in check.rules:
            found = fn(data)
            if found:
                problems.append("%s: %s" % (fn.rule_name, found))

        if problems:
            print("  %s %-52s %d fields, %dms" %
                  (paint("fail", "31"), label, len(check.fields), ms))
            for problem in problems:
                print("       %s" % problem)
            failures += 1
        else:
            print("  %s %-52s %d fields, %d rules, %dms" %
                  (paint("ok  ", "32"), label, len(check.fields), len(check.rules), ms))
            if save and check.fixture:
                write_fixture(check.fixture, data)

    return failures, skips


def write_fixture(name, data):
    os.makedirs(FIXTURE_DIR, exist_ok=True)
    path = os.path.join(FIXTURE_DIR, name + ".json")
    with open(path, "w") as handle:
        json.dump(data, handle, separators=(",", ":"), ensure_ascii=False)
        handle.write("\n")
    print("       saved %s" % os.path.relpath(path, ROOT))


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("sources", nargs="*",
                        help="gwei, glyphbots, coral, bankr, voxels. Default is all of them.")
    parser.add_argument("--save", action="store_true",
                        help="write each passing response into sim/fixtures")
    parser.add_argument("--system-roots", action="store_true",
                        help="trust the OS store instead of the four in src/ca_roots.h")
    args = parser.parse_args()

    checks = build_checks()
    if args.sources:
        wanted = set(args.sources)
        known = {c.source for c in checks}
        unknown = wanted - known
        if unknown:
            raise SystemExit("no such source: %s. known: %s"
                             % (", ".join(sorted(unknown)), ", ".join(sorted(known))))
        checks = [c for c in checks if c.source in wanted]

    if args.system_roots:
        context = ssl.create_default_context()
        print("trusting the system store")
    else:
        bundle, count = ca_bundle_path()
        context = ssl.create_default_context(cafile=bundle)
        print("trusting the %d roots in src/ca_roots.h, the same ones the device has" % count)

    failures, skips = run(checks, context, args.save)

    print()
    if failures:
        print(paint("%d check%s failed" % (failures, "" if failures == 1 else "s"), "31;1"))
        print("A source changed shape. Fix the firmware to match, or fix the check if the "
              "old shape was never real.")
        return 1
    print(paint("every source answers the way the firmware reads it", "32;1")
          + ("" if not skips else ", %d skipped" % skips))
    return 0


if __name__ == "__main__":
    sys.exit(main())
