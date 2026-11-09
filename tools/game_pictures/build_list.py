#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 CheezyEmu contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Builds dist/game_pictures.json: a SteamGridDB picture link for every Switch 2 title ID.

CheezyEmu downloads that list and the pictures it names, so players need no SteamGridDB key.
The title IDs and names come from switchbrew's Switch 2 title list. Only this script needs a key:

    SGDB_API_KEY=... python tools/game_pictures/build_list.py

tools/game_pictures/overrides.json fixes a wrong or missing match by hand:
    {"0400486024DEC000": "https://cdn2.steamgriddb.com/icon/....png"}   a picture link, or
    {"0400486024DEC000": {"sgdb_game": 12345}}                          the SteamGridDB game to use, or
    {"0400486024DEC000": null}                                          no picture.
Entries from the previous list are kept when SteamGridDB can't be reached for a title.
"""

import datetime
import json
import os
import re
import sys
import time
import unicodedata
import urllib.error
import urllib.parse
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", ".."))
OUTPUT = os.path.join(REPO, "dist", "game_pictures.json")
OVERRIDES = os.path.join(HERE, "overrides.json")
TITLE_LIST = "https://switchbrew.org/w/index.php?title=Switch_2:_Title_list/Games&action=raw"
SGDB = "https://www.steamgriddb.com/api/v2"
USER_AGENT = "CheezyEmu-game-pictures/1.0"


def get(url, key=None, retries=4):
    headers = {"User-Agent": USER_AGENT}
    if key:
        headers["Authorization"] = "Bearer " + key
    for attempt in range(retries):
        try:
            with urllib.request.urlopen(urllib.request.Request(url, headers=headers),
                                        timeout=30) as response:
                return response.read()
        except urllib.error.HTTPError as e:
            if e.code == 404:
                return None
            if e.code in (401, 403):
                sys.exit("SteamGridDB rejected the API key (HTTP %d)" % e.code)
            if attempt == retries - 1:
                raise
        except (urllib.error.URLError, TimeoutError):
            if attempt == retries - 1:
                raise
        time.sleep(5 * (attempt + 1))
    return None


def parse_titles(wikitext):
    """(title id, name) for each row of switchbrew's table."""
    titles = []
    for line in wikitext.splitlines():
        m = re.match(r"^\|\s*(04[0-9A-Fa-f]{14})\s*\|\|\s*(.*?)\s*\|\|", line)
        if m:
            name = re.sub(r"\[\[[^|\]]*\|([^\]]*)\]\]", r"\1", m.group(2))
            name = re.sub(r"\[\[([^\]]*)\]\]", r"\1", name)
            name = re.sub(r"<[^>]+>", "", name).strip()
            if name:
                titles.append((m.group(1).upper(), name))
    return titles


def clean_name(name):
    name = re.sub(r"[™®©]", "", name)
    name = name.replace("–", "-").replace("—", "-")
    return re.sub(r"\s+", " ", name).strip()


def search_names(name):
    """The name, then without edition or demo suffixes."""
    names = [clean_name(name)]
    stripped = re.sub(r"\s*[-:]?\s*(Nintendo Switch 2 Edition.*|DEMO|Demo|Trial|Free Trial)$",
                      "", names[0]).strip()
    stripped = re.sub(r"\s*\+\s*.*Nintendo Switch 2 Edition.*$", "", stripped).strip()
    if stripped and stripped not in names:
        names.append(stripped)
    return names


def slug(text):
    text = unicodedata.normalize("NFKD", text).encode("ascii", "ignore").decode()
    return "-".join(re.findall(r"[a-z0-9]+", text.lower()))


def score(candidate, query):
    c, q = slug(candidate), slug(query)
    if not c or not q:
        return -1000
    if c == q:
        return 1000
    if c.startswith(q + "-") or q.startswith(c + "-"):
        return 750 - abs(len(c) - len(q))
    cw, qw = set(c.split("-")), set(q.split("-"))
    return len(cw & qw) * 120 - len(cw) * 10


def find_game(name, key):
    """The SteamGridDB game for a name: the closest name, the newest release among equals."""
    for query in search_names(name):
        body = get(SGDB + "/search/autocomplete/" + urllib.parse.quote(query, safe=""), key)
        data = json.loads(body or b"{}").get("data") or []
        best = None
        for item in data:
            s = score(item.get("name", ""), query)
            release = item.get("release_date") or 0
            if s >= 750 and (best is None or (s, release) > (best[0], best[1])):
                best = (s, release, item)
        if best:
            return best[2]
    return None


def first_icon(game_id, key):
    for query in ("/icons/game/%d?dimensions=256,512&mimes=image/png" % game_id,
                  "/icons/game/%d?mimes=image/png" % game_id):
        body = get(SGDB + query, key)
        for item in json.loads(body or b"{}").get("data") or []:
            if item.get("url"):
                return item["url"]
    return None


def main():
    key = os.environ.get("SGDB_API_KEY", "").strip()
    if not key:
        sys.exit("Set SGDB_API_KEY to a SteamGridDB API key")
    titles = parse_titles(get(TITLE_LIST).decode("utf-8"))
    if len(titles) < 50:
        sys.exit("switchbrew's title list looks broken (%d titles), not updating" % len(titles))
    previous = {}
    if os.path.exists(OUTPUT):
        previous = json.load(open(OUTPUT, encoding="utf-8")).get("pictures", {})
    overrides = {}
    if os.path.exists(OVERRIDES):
        overrides = {k.upper(): v for k, v in json.load(open(OVERRIDES, encoding="utf-8")).items()}

    pictures = {}
    by_name = {}
    found = missing = kept = 0
    for title_id, name in titles:
        if title_id in pictures:
            continue
        override = overrides.get(title_id, "")
        if override is None:
            continue
        if isinstance(override, str) and override:
            pictures[title_id] = {"name": clean_name(name), "url": override}
            found += 1
            continue
        try:
            if isinstance(override, dict) and override.get("sgdb_game"):
                game = {"id": int(override["sgdb_game"]), "name": clean_name(name)}
            else:
                cache_key = slug(clean_name(name))
                if cache_key not in by_name:
                    by_name[cache_key] = find_game(name, key)
                game = by_name[cache_key]
            url = first_icon(game["id"], key) if game else None
        except Exception as e:  # network trouble: keep what the last list had
            print("  %s %s: %s" % (title_id, name, e), file=sys.stderr)
            if title_id in previous:
                pictures[title_id] = previous[title_id]
                kept += 1
            continue
        if url:
            pictures[title_id] = {"name": clean_name(name), "url": url, "sgdb_game": game["id"]}
            found += 1
        else:
            missing += 1
            print("  no picture: %s %s" % (title_id, name))
        time.sleep(0.25)

    out = {
        "version": 1,
        "generated": datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "source": "SteamGridDB (https://www.steamgriddb.com), title list from switchbrew.org",
        "pictures": dict(sorted(pictures.items())),
    }
    os.makedirs(os.path.dirname(OUTPUT), exist_ok=True)
    with open(OUTPUT, "w", encoding="utf-8", newline="\n") as f:
        json.dump(out, f, indent=1, ensure_ascii=False)
        f.write("\n")
    print("%d titles: %d pictures, %d without, %d kept from the last list" %
          (len(titles), found, missing, kept))


if __name__ == "__main__":
    main()
