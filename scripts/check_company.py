#!/usr/bin/env python3
"""Check both tiers of resources/ and print the coverage by evidence (population-weighted).

Exit status 1 when any check fails. Run after prepare_tables.py.
"""
import re
import sys
from collections import Counter, defaultdict

from companygen_data import (AGE_CLASSES, BAND_LIMITS, BANDS, CURATED, DIVISIONS, GROUPS, RESOURCES, SECTIONS,
                             countries, read_tsv, section_of)
from prepare_words import PATTERNS, is_latin

SECTS = [s for s in SECTIONS if s not in "TU"]
TOL = 0.02   # shares are rounded to three or four significant digits


def check_tier(tier, info):
    problems = []
    say = problems.append
    t = lambda name: read_tsv(RESOURCES / tier / name)

    cty = {r["country_code"]: r for r in t("countries.tsv")}
    if set(cty) != set(info):
        say(f"countries.tsv: missing {sorted(set(info) - set(cty))}, extra {sorted(set(cty) - set(info))}")
    words = defaultdict(lambda: defaultdict(list))
    for r in t("words.tsv"):
        words[r["language"]][(r["kind"], r["key"])].append(r)
    cultures = {r["culture"] for r in t("surnames.tsv")}
    for cc, r in cty.items():
        if int(r["enterprises"]) <= 0:
            say(f"{cc}: no enterprises")
        langs = [p.split(":")[0] for p in r["languages"].split(";")]
        for lang in langs:
            if lang not in words:
                say(f"{cc}: no words for {lang}")
            elif not words[lang].get(("surname", "*")) and r["culture"] not in cultures:
                say(f"{cc}: {lang} has no surnames and culture {r['culture']} none either")
        for b in BANDS:
            x = float(r[f"no_domain_{b}"])
            if not 0 <= x <= 1:
                say(f"{cc}: no_domain_{b} {x}")

    # industries: every country, valid divisions
    ind = defaultdict(dict)
    for r in t("industries.tsv"):
        if r["division"] not in DIVISIONS:
            say(f"industries.tsv: {r['country_code']} division {r['division']}")
        ind[r["country_code"]][r["division"]] = (int(r["enterprises"]), int(r["employment"]))
    for cc in info:
        if sum(e for e, _ in ind[cc].values()) <= 0 or sum(p for _, p in ind[cc].values()) <= 0:
            say(f"industries.tsv: {cc} has no weight")

    # sizes: the pooled rows cover every drawn key; shares sum to one; means inside their band
    cells = defaultdict(dict)
    for r in t("sizes.tsv"):
        cells[(r["country_code"], r["isic"])][r["band"]] = r
    for (cc, key), v in cells.items():
        if set(v) != set(BANDS):
            say(f"sizes.tsv: {cc} {key} bands {sorted(v)}")
            continue
        if abs(sum(float(x["enterprise_share"]) for x in v.values()) - 1) > TOL:
            say(f"sizes.tsv: {cc} {key} enterprise shares sum to {sum(float(x['enterprise_share']) for x in v.values())}")
        for b, x in v.items():
            lo, hi = BAND_LIMITS[b]
            m = float(x["mean_persons"])
            if m < lo * 0.999 or (hi and m > hi * 1.001):
                say(f"sizes.tsv: {cc} {key} {b} mean {m}")
    pooled = {k for (cc, k) in cells if cc == "*"}
    want = set(SECTS) if tier == "lite" else {d for d in DIVISIONS if section_of(d) not in "TU"}
    if not want <= pooled | (set(SECTS) if tier == "full" else set()):
        missing = sorted(want - pooled)
        if tier == "lite" or any(m.isdigit() for m in missing):
            say(f"sizes.tsv: no pooled rows for {missing[:10]}")

    # ages: shares sum to one
    for r in t("ages.tsv"):
        s = sum(float(r[f"age_{a}"]) for a in AGE_CLASSES)
        if abs(s - 1) > TOL:
            say(f"ages.tsv: {r['country_code']} {r['section']} {r['band']} sums to {s}")

    # legal forms and groups
    forms = defaultdict(list)
    for r in t("legal_forms.tsv"):
        forms[(r["country_code"], r["group"])].append(r)
        name = r["name"] or r["abbreviation"]
        if not name:
            say(f"legal_forms.tsv: {r['country_code']} {r['code']} has no name")
        if not is_latin(r["name"]) and not r["latin_name"] and not (r["latin_abbreviation"] and is_latin(r["latin_abbreviation"])):
            say(f"legal_forms.tsv: {r['country_code']} {r['name']} has no Latin form")
        for col in ("only", "not"):
            for k in filter(None, r[col].split(",")):
                if k not in SECTIONS and k not in DIVISIONS:
                    say(f"legal_forms.tsv: {r['country_code']} {r['code']} {col} {k}")
        if r["from"] and r["until"] and r["from"] > r["until"]:
            say(f"legal_forms.tsv: {r['country_code']} {r['code']} from {r['from']} after until {r['until']}")
        for col in ("from", "until"):
            if r[col] and not re.fullmatch(r"\d{4}-\d{2}-\d{2}", r[col]):
                say(f"legal_forms.tsv: {r['country_code']} {r['code']} {col} {r[col]!r}")
    for (cc, g), fs in forms.items():
        for col in ("share_small", "share_large"):
            s = sum(float(f[col]) for f in fs)
            if abs(s - 1) > TOL:
                say(f"legal_forms.tsv: {cc} {g} {col} sums to {s:.4f}")
    groups = defaultdict(dict)
    for r in t("groups.tsv"):
        groups[r["country_code"]][r["section"]] = {g: float(r[g]) for g in GROUPS}
    for cc in info:
        rows = groups.get(cc, {})
        if "*" not in rows:
            say(f"groups.tsv: {cc} has no multipliers")
            continue
        have = {g for g in GROUPS if forms.get((cc, g))}
        for s, v in rows.items():
            for g, x in v.items():
                if x > 0 and g not in have:
                    say(f"groups.tsv: {cc} {s} weighs {g}, which has no form")
        if not any(x > 0 for x in rows["*"].values()):
            say(f"groups.tsv: {cc} multipliers all zero")
    for s in SECTS:
        if s not in groups.get("*", {}):
            say(f"groups.tsv: no pooled row for {s}")
    odds = {(r["section"], r["band"]) for r in t("form_odds.tsv")}
    for b in BANDS:
        if ("*", b) not in odds:
            say(f"form_odds.tsv: no row for * {b}")

    # patterns: the default rows, shares sum to one
    pat = t("patterns.tsv")
    if not any(r["language"] == "*" for r in pat):
        say("patterns.tsv: no default rows")
    for r in pat:
        s = sum(float(r[p]) for p in PATTERNS)
        if abs(s - 1) > TOL:
            say(f"patterns.tsv: {r['language']} {r['group']} {r['band']} sums to {s}")

    # domains: every country, shares sum to one, plain suffixes
    dom = defaultdict(float)
    for r in t("domains.tsv"):
        dom[r["country_code"]] += float(r["share"])
        if not re.fullmatch(r"[^\s.]+(\.[^\s.]+)*", r["tld"]):
            say(f"domains.tsv: {r['country_code']} suffix {r['tld']!r}")
    for cc in info:
        if abs(dom.get(cc, 0) - 1) > TOL:
            say(f"domains.tsv: {cc} sums to {dom.get(cc, 0)}")

    # surnames and places: Latin forms
    for r in t("surnames.tsv"):
        if not is_latin(r["surname"]) and not r["latin"]:
            say(f"surnames.tsv: {r['culture']} {r['surname']} has no Latin form")
    for r in t("places.tsv"):
        if not is_latin(r["place"]):
            say(f"places.tsv: {r['country_code']} {r['place']} is not Latin")
    return problems, cty


def coverage(cty, info):
    pop = {cc: int(info[cc]["population"] or 0) for cc in info}
    total = sum(pop.values())
    fields = [c for c in next(iter(cty.values())) if c.endswith("_evidence")]
    for f in fields:
        acc = Counter()
        for cc, r in cty.items():
            acc[r[f]] += pop[cc]
        print(f"  {f[:-9]:<11}", "  ".join(f"{k} {v / total:.1%}" for k, v in acc.most_common()))


def main():
    sys.stdout.reconfigure(encoding="utf-8")
    info = countries()
    bad = 0
    for tier in ("full", "lite"):
        problems, cty = check_tier(tier, info)
        size = sum(p.stat().st_size for p in (RESOURCES / tier).glob("*.tsv"))
        print(f"{tier}: {size:,} bytes, {len(problems)} problems")
        for p in problems[:40]:
            print("  " + p)
        if len(problems) > 40:
            print(f"  ... and {len(problems) - 40} more")
        bad += len(problems)
    print("coverage by evidence (share of population):")
    coverage(cty, info)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
