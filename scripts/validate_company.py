#!/usr/bin/env python3
"""Draw companies from the engine and compare them with the tables (design section 11).

For each tier and each weighting, scripts/draw_companies.cpp draws N companies per country
(default 20,000) and counts them. This script recomputes from the tables, independently of the
C++ engine, what those counts should be, and compares with a z test (5 standard errors):

- the shares of the ISIC sections (industries.tsv: enterprises or employment);
- the shares of the size bands (sizes.tsv, through the same cell resolution);
- the mean head count of the bands (sizes.tsv);
- the shares of the legal-form groups (groups.tsv x form_odds.tsv, over the forms open at the
  reference date, legal_forms.tsv);
- the share of firms aged up to five years where the country has a measure (countries.tsv).

Usage: python scripts/validate_company.py [--n 20000] [--tier lite|full] [--cxx clang++] [country ...]
Exit status 1 when any check fails.
"""
import argparse
import math
import os
import subprocess
import sys
import tempfile
from collections import defaultdict
from pathlib import Path

from companygen_data import BANDS, GROUPS, RESOURCES, ROOT, SECTIONS, read_tsv, section_of

REFERENCE = (2025, 7, 1)
Z_LIMIT = 5.0
MEAN_TOLERANCE = {"micro": 0.04, "small": 0.04, "medium": 0.06, "large": 0.35}


def tsv(path):
    return read_tsv(path)


class Tables:
    def __init__(self, tier):
        d = RESOURCES / tier
        self.countries = {r["country_code"]: r for r in tsv(d / "countries.tsv")}
        self.industries = defaultdict(dict)
        for r in tsv(d / "industries.tsv"):
            self.industries[r["country_code"]][r["division"]] = (float(r["enterprises"]), float(r["employment"]))
        self.sizes = defaultdict(dict)
        for r in tsv(d / "sizes.tsv"):
            self.sizes[(r["country_code"], r["isic"])][r["band"]] = (
                float(r["enterprise_share"]), float(r["employment_share"]), float(r["mean_persons"]))
        self.groups = {}
        for r in tsv(d / "groups.tsv"):
            self.groups[(r["country_code"], r["section"])] = [float(r[g]) for g in GROUPS]
        self.odds = {}
        for r in tsv(d / "form_odds.tsv"):
            self.odds[(r["section"], r["band"])] = [float(r[g]) for g in GROUPS]
        self.forms = defaultdict(list)
        for r in tsv(d / "legal_forms.tsv"):
            self.forms[r["country_code"]].append(r)

    def cell(self, cc, division):
        sec = section_of(division)
        for key in ((cc, division), (cc, sec), ("*", division), ("*", sec)):
            if key in self.sizes and len(self.sizes[key]) == 4:
                cell = self.sizes[key]
                ent = [cell[b][0] for b in BANDS]
                emp = [cell[b][1] for b in BANDS]
                means = [band_mean_in(b, cell[BANDS[b]][2]) for b in range(4)]
                if not sum(emp):          # shares of one kind only: the other follows from the mean head counts
                    emp = [e * m for e, m in zip(ent, means)]
                if not sum(ent):
                    ent = [e / m for e, m in zip(emp, means)]
                se, sm = sum(ent) or 1.0, sum(emp) or 1.0
                return [x / se for x in ent], [x / sm for x in emp], [cell[b][2] for b in BANDS]
        raise KeyError((cc, division))

    def base(self, cc, section, band):
        own = self.groups.get((cc, section))
        pooled = self.groups[("*", section)]
        if own:
            return own
        mult = self.groups.get((cc, "*"), [1.0] * 4)
        if band in ("micro", "small"):
            return [p * m for p, m in zip(pooled, mult)]
        return pooled

    def group_probs(self, cc, division, section, band):
        """P(group | country, division, band), as the engine computes it."""
        ref = "%04d-%02d-%02d" % REFERENCE
        large = band in ("medium", "large")
        forms = self.forms[cc]
        share_sum = [0.0] * 4
        for level in range(3):
            share_sum = [0.0] * 4
            for f in forms:
                ok = True
                if level < 2 and f["from"] and f["from"] > ref:
                    ok = False
                if level < 1 and ok:
                    only = f["only"].split(",") if f["only"] else []
                    barred = f["not"].split(",") if f["not"] else []
                    if only and section not in only:
                        ok = False
                    if section in barred or division in barred:
                        ok = False
                if ok:
                    share_sum[GROUPS.index(f["group"])] += float(f["share_large" if large else "share_small"])
            if sum(share_sum) > 0:
                break
        odds = self.odds.get((section, band)) or self.odds[("*", band)]
        base = self.base(cc, section, band)
        grp = [base[g] * odds[g] if share_sum[g] > 0 else 0.0 for g in range(4)]
        total = sum(grp)
        if total <= 0:
            grp = [1.0 if share_sum[g] > 0 else 0.0 for g in range(4)]
            total = sum(grp)
        return [x / total for x in grp] if total > 0 else grp


def z_test(observed, expected, n):
    sd = math.sqrt(max(expected * (1 - expected), 1e-12) / n)
    return (observed - expected) / sd if sd > 0 else 0.0


def compare(tables, cc, mode, cells, young, n, say):
    """Compare one country's draws with the tables."""
    ind = tables.industries[cc]
    weight = {d: (v[1] if mode == "employment" else v[0]) for d, v in ind.items()}
    total = sum(weight.values())
    exp_section = defaultdict(float)
    exp_band = [0.0] * 4
    exp_band_mean = [0.0] * 4
    exp_group = [0.0] * 4
    for d, w in weight.items():
        if w <= 0:
            continue
        p = w / total
        sec = section_of(d)
        exp_section[sec] += p
        ent, emp, mean = tables.cell(cc, d)
        share = emp if mode == "employment" else ent
        for b, band in enumerate(BANDS):
            if share[b] <= 0:
                continue
            exp_band[b] += p * share[b]
            exp_band_mean[b] += p * share[b] * band_mean(b, mean[b])
            probs = tables.group_probs(cc, d, sec, band)
            for g in range(4):
                exp_group[g] += p * share[b] * probs[g]
    obs_section, obs_band, obs_group = defaultdict(int), [0] * 4, [0] * 4
    obs_emp = [0] * 4
    for (division, band, group), (count, emp) in cells.items():
        obs_section[section_of(division)] += count
        b = BANDS.index(band)
        obs_band[b] += count
        obs_emp[b] += emp
        obs_group[GROUPS.index(group)] += count
    bad = 0
    for sec in SECTIONS:
        if sec in "TU":
            continue
        z = z_test(obs_section[sec] / n, exp_section[sec], n)
        if abs(z) > Z_LIMIT:
            say(f"{cc} {mode}: section {sec} drawn {obs_section[sec] / n:.4f}, expected {exp_section[sec]:.4f} (z {z:.1f})")
            bad += 1
    for b, band in enumerate(BANDS):
        z = z_test(obs_band[b] / n, exp_band[b], n)
        if abs(z) > Z_LIMIT:
            say(f"{cc} {mode}: band {band} drawn {obs_band[b] / n:.4f}, expected {exp_band[b]:.4f} (z {z:.1f})")
            bad += 1
        if b < 3 and obs_band[b] >= 500 and exp_band[b] > 0:   # the open top band is heavy-tailed: not compared
            expected_mean = exp_band_mean[b] / exp_band[b]
            drawn_mean = obs_emp[b] / obs_band[b]
            if abs(drawn_mean - expected_mean) > MEAN_TOLERANCE[band] * expected_mean:
                say(f"{cc} {mode}: mean head count in {band} drawn {drawn_mean:.2f}, expected {expected_mean:.2f}")
                bad += 1
    for g, name in enumerate(GROUPS):
        z = z_test(obs_group[g] / n, exp_group[g], n)
        if abs(z) > Z_LIMIT:
            say(f"{cc} {mode}: group {name} drawn {obs_group[g] / n:.4f}, expected {exp_group[g]:.4f} (z {z:.1f})")
            bad += 1
    return bad


DEFAULT_MEAN = (2.0, 19.0, 100.0, 900.0)


def band_mean_in(b, mean):
    """The mean head count of band b as the engine takes it from the source (the default when outside the band)."""
    if b < 3:
        return mean if band_lo(b) <= mean <= band_hi(b) else DEFAULT_MEAN[b]
    return mean if mean >= 250 else DEFAULT_MEAN[3]


def band_mean(b, mean):
    """The mean the engine fits: the source's, or the band's default when outside it, and at most the
    band's uniform mean (the engine's laws do not rise within a band)."""
    if not band_lo(b) <= mean <= band_hi(b):
        mean = DEFAULT_MEAN[b]
    return min(mean, (band_lo(b) + band_hi(b)) / 2) if b < 3 else mean


def band_lo(b):
    return (1, 10, 50, 250)[b]


def band_hi(b):
    return (9, 49, 249, 10**9)[b]


def build(cxx, binary):
    cmd = [cxx, "-std=c++23", "-O2", "-DNDEBUG", "-Wno-c2y-extensions", "-I", str(ROOT), str(ROOT / "scripts" / "draw_companies.cpp"),
           "-o", str(binary)]
    print(" ".join(cmd))
    subprocess.run(cmd, check=True)


def read_counts(out):
    cells = defaultdict(dict)
    for line in (out / "cells.tsv").read_text(encoding="utf-8").splitlines():
        cc, division, band, group, count, emp = line.split("\t")
        cells[cc][(division, band, group)] = (int(count), int(emp))
    young = {}
    for line in (out / "ages.tsv").read_text(encoding="utf-8").splitlines():
        cc, y, total = line.split("\t")
        young[cc] = int(y)
    return cells, young


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--n", type=int, default=20000)
    ap.add_argument("--tier", choices=("lite", "full"), action="append")
    ap.add_argument("--cxx", default=os.environ.get("CXX", "clang++"))
    ap.add_argument("--seed", type=int, default=20261111)
    ap.add_argument("countries", nargs="*")
    args = ap.parse_args()
    tiers = args.tier or ["lite", "full"]
    problems = []
    say = problems.append
    work = Path(tempfile.mkdtemp(prefix="company-validate-"))
    binary = work / ("draw_companies.exe" if os.name == "nt" else "draw_companies")
    build(args.cxx, binary)
    for tier in tiers:
        tables = Tables(tier)
        for mode in ("enterprises", "employment"):
            out = work / f"{tier}-{mode}"
            cmd = [str(binary), str(RESOURCES / tier), str(args.n), str(args.seed), mode, str(out), *args.countries]
            subprocess.run(cmd, check=True)
            cells, young = read_counts(out)
            failures = 0
            for cc in sorted(cells):
                failures += compare(tables, cc, mode, cells[cc], young[cc], args.n, say)
                if mode == "enterprises":
                    row = tables.countries[cc]
                    if row["young_share"] and row["age_evidence"] == "measured" and cc != "US":
                        share = young[cc] / args.n
                        exp = float(row["young_share"])
                        if abs(share - exp) > 0.03 + Z_LIMIT * math.sqrt(exp * (1 - exp) / args.n):
                            say(f"{cc}: firms up to five years old drawn {share:.3f}, measured {exp:.3f}")
                            failures += 1
            print(f"{tier} {mode}: {len(cells)} countries, {failures} failures")
    for p in problems[:80]:
        print(p)
    if len(problems) > 80:
        print(f"... and {len(problems) - 80} more")
    print(f"{len(problems)} problems")
    sys.exit(1 if problems else 0)


if __name__ == "__main__":
    main()
