#!/usr/bin/env python3
"""Write every resource table, both tiers, from the counts in raw_data/work and the curated tables.

Run fetch_raw.py, read_sources.py and read_registers.py first. The economy (prepare_economy.py) and
the legal forms (prepare_forms.py) are fitted here, so the C++ side only draws. A rebuild from the
same downloads is byte-identical: every table is sorted and every number rounded to a fixed number
of significant digits.

full keeps divisions, source texts and long word lists; lite, which is compiled in, keeps sections
in sizes.tsv, fewer words, surnames and places, and three significant digits.
"""
import math
import re
import sys
import unicodedata
from collections import Counter, defaultdict

import prepare_economy
import prepare_forms
from companygen_data import (AGE_CLASSES, BAND_LIMITS, BANDS, CURATED, GROUPS, RESOURCES, SIBLINGS, SECTIONS,
                             countries, load_work, read_tsv, section_of, write_table)
from prepare_words import PATTERNS, is_latin, load_words, words_rows

SECTS = [s for s in SECTIONS if s not in "TU"]
TIERS = {"full": 4, "lite": 3}          # significant digits
# lite keeps this many entries per word-list row (None: all)
LITE_WORDS = {"trade": 3, "adjective": 12, "region": 8, "place": 8, "surname": 30, "family": 4, "joiner": 2,
              "coined_head": 12, "coined_tail": 12, "template": None, "style": None}
# lite leaves out divisions under this weight per million in both enterprises and employment
LITE_DIVISION_FLOOR = 50
# lite leaves out forms under this share of their group among both small and large firms
LITE_FORM_SHARE = 0.01
SURNAMES = {"full": 1000, "lite": 30}
PLACES = {"full": 40, "lite": 8}
# A band's mean head count when the cell's own is missing or outside the band (SBS cells that are
# confidential, or count persons employed without the owner): the pooled mean of the division, else
# these (the pooled means of every division, rounded).
BAND_MEAN = {"micro": 2.0, "small": 19.0, "medium": 100.0, "large": 900.0}
# TLD shares where curated/domains.tsv has no row, by region ("cc" is the country's ccTLD)
REGION_TLDS = {
    "Europe": "cc:0.62;com:0.3;net:0.04;org:0.04",
    "Americas": "cc:0.35;com:0.58;net:0.04;org:0.03",
    "Asia": "cc:0.3;com:0.62;net:0.05;org:0.03",
    "Africa": "cc:0.25;com:0.67;net:0.04;org:0.04",
    "Oceania": "cc:0.4;com:0.54;net:0.03;org:0.03",
}
# The share of firms without a website by size band and income group (curated estimates, guided by
# Eurostat's isoc_ciweb: about 78% of EU firms with 10-49 persons, 91% with 50-249 and 95% with 250+
# had a website in 2023, as recalled, not downloaded; micro firms and poorer countries lower)
NO_DOMAIN = {
    "High income": (0.50, 0.22, 0.09, 0.04),
    "Upper middle income": (0.70, 0.40, 0.20, 0.08),
    "Lower middle income": (0.85, 0.60, 0.35, 0.15),
    "Low income": (0.93, 0.75, 0.50, 0.25),
}


def band_order(b):
    """Bands in size order; "*" (every band, the UK register's ages) last."""
    return BANDS.index(b) if b in BANDS else len(BANDS)


def per_million(x, n):
    """A share as an integer weight per million, rounded to n significant digits first."""
    return round(sig(x, n) * 1e6)


def sig(x, n):
    """x rounded to n significant digits (0 stays 0)."""
    if not x:
        return 0.0
    return round(x, n - 1 - int(math.floor(math.log10(abs(x)))))


def form_slug(text):
    """A key from a form's name: lower-case ASCII letters and digits, joined by underscores."""
    plain = unicodedata.normalize("NFKD", text).encode("ascii", "ignore").decode()
    return re.sub(r"[^a-z0-9]+", "_", plain.lower()).strip("_") or "form"


def write_both(name, comment, columns, rows_for):
    """rows_for(tier, digits) -> rows; the full tier's source columns are dropped from lite."""
    for tier, n in TIERS.items():
        cols = columns if tier == "full" else [c for c in columns if not c.endswith("source")]
        note = comment if tier == "full" or cols == columns else comment + "; sources in the full tier"
        write_table(tier, name, note, cols, rows_for(tier, n), digits=12)


# ------------------------------------------------------------------ economy

def fix_mean(m, band, pooled):
    lo, hi = BAND_LIMITS[band]
    ok = lambda v: v is not None and v >= lo and (hi is None or v <= hi)
    if ok(m):
        return m
    if ok(pooled):
        return pooled
    return BAND_MEAN[band]


def size_tables(econ):
    rows, _ = econ["sizes"]
    pooled = {(r["isic"], r["band"]): r["mean_persons"] for r in rows if r["country_code"] == "*"}
    out = []
    for r in rows:
        r = dict(r)
        r["mean_persons"] = fix_mean(r["mean_persons"], r["band"], pooled.get((r["isic"], r["band"])))
        out.append(r)
    return out


def sections_of_sizes(rows, ent_all):
    """lite: division rows summed to sections, weighted by the country's enterprises."""
    pooled_ent = Counter()
    for cc, v in ent_all.items():
        tot = sum(v.values()) or 1.0
        for d, x in v.items():
            pooled_ent[d] += x / tot
    acc = defaultdict(lambda: [0.0, 0.0])
    for r in rows:
        cc, key = r["country_code"], r["isic"]
        if not key.isdigit():   # already a section row
            w = 1.0
            s = key
        else:
            w = (pooled_ent if cc == "*" else ent_all.get(cc, {})).get(key, 0.0)
            s = section_of(key)
            if w <= 0:
                continue
        e = w * r["enterprise_share"]
        acc[(cc, s, r["band"])][0] += e
        acc[(cc, s, r["band"])][1] += e * r["mean_persons"]
    out = []
    keys = sorted({(cc, s) for cc, s, _ in acc})
    for cc, s in keys:
        tot_e = sum(acc[(cc, s, b)][0] for b in BANDS)
        tot_p = sum(acc[(cc, s, b)][1] for b in BANDS)
        if tot_e <= 0:
            continue
        for b in BANDS:
            e, p = acc[(cc, s, b)]
            out.append({"country_code": cc, "isic": s, "band": b, "enterprise_share": e / tot_e,
                        "employment_share": p / tot_p if tot_p else 0.0,
                        "mean_persons": p / e if e else BAND_MEAN[b]})
    return out


# ------------------------------------------------------------------ names

def name_languages():
    out = {}
    for r in read_tsv(CURATED / "name_languages.tsv"):
        langs = []
        for part in r["languages"].split(";"):
            code, _, share = part.partition(":")
            langs.append((code, float(share or 1)))
        out[r["country"]] = (langs, r["culture"])
    return out


def surname_rows(words, names, limit):
    """The surnames each culture needs: those of a country one of whose name languages has no
    surnames of its own (name-generator's lists, decision 7, or curated/surnames.tsv)."""
    needed = set()
    for cc, (langs, culture) in names.items():
        if any(not words[lang]["entries"].get(("surname", "*")) for lang, _ in langs):
            needed.add(culture)
    curated = defaultdict(list)
    for r in read_tsv(CURATED / "surnames.tsv"):
        curated[r["culture"]].append((r["surname"], r["latin"], float(r["weight"] or 1)))
    rows = []
    for culture in sorted(needed):
        if culture in curated:
            entries = curated[culture]
        else:
            path = SIBLINGS / "name-generator" / "resources" / "full" / culture / "sur.names"
            lines = path.read_text(encoding="utf-8").splitlines()
            entries = []
            for line in lines[2:]:   # the country code, then "any"
                name, _, w = line.partition("\t")
                if name:
                    entries.append((name, "", float(w or 1)))
        entries = sorted(entries, key=lambda e: (-e[2], e[0]))[:limit]
        top = entries[0][2] if entries else 1.0
        for name, latin, w in entries:
            rows.append({"culture": culture, "surname": name, "latin": latin, "weight": w / top})
    return rows


# GeoNames gives some capitals and large cities their English names; a company name uses the local one.
EXONYMS = {
    ("AL", "Tirana"): "Tiranë", ("AT", "Vienna"): "Wien", ("CZ", "Prague"): "Praha", ("DE", "Munich"): "München",
    ("DK", "Copenhagen"): "København", ("IT", "Rome"): "Roma", ("IT", "Milan"): "Milano", ("IT", "Naples"): "Napoli",
    ("IT", "Turin"): "Torino", ("IT", "Genoa"): "Genova", ("IT", "Florence"): "Firenze", ("IT", "Padua"): "Padova",
    ("LV", "Riga"): "Rīga", ("MD", "Chisinau"): "Chișinău", ("MX", "Mexico City"): "Ciudad de México",
    ("NL", "The Hague"): "Den Haag", ("PL", "Warsaw"): "Warszawa", ("PT", "Lisbon"): "Lisboa",
    ("RO", "Bucharest"): "București", ("RS", "Belgrade"): "Beograd", ("VN", "Hanoi"): "Hà Nội",
    ("VN", "Ho Chi Minh City"): "Hồ Chí Minh",
}


def place_rows(words, names, info, limit):
    """Places for the {place} part where the name language has none of its own: the country's
    largest places in address-generator, weighted by the square root of their population."""
    by_cc = defaultdict(list)
    for r in read_tsv(SIBLINGS / "address-generator" / "resources" / "full" / "places.tsv"):
        if r["country_code"] in info and r["population"]:
            by_cc[r["country_code"]].append((int(r["population"]), r["name"]))
    rows = []
    for cc in sorted(info):
        langs, _ = names[cc]
        if all(any(k == "place" for k, _ in words[lang]["entries"]) for lang, _ in langs):
            continue
        seen = set()
        top = sorted(by_cc.get(cc, []), key=lambda t: (-t[0], t[1]))
        kept = []
        for pop, name in top:
            name = EXONYMS.get((cc, name), name)
            if name in seen or not is_latin(name):
                continue
            seen.add(name)
            kept.append((pop, name))
            if len(kept) == limit:
                break
        if not kept:
            continue
        m = math.sqrt(kept[0][0])
        for pop, name in kept:
            rows.append({"country_code": cc, "place": name, "weight": math.sqrt(pop) / m})
    return rows


def pattern_rows():
    tilts = {r["language"]: r for r in read_tsv(CURATED / "pattern_tilts.tsv")}
    base = read_tsv(CURATED / "patterns.tsv")
    return base, tilts


# ------------------------------------------------------------------ domains

def parse_tlds(text, cctld):
    out = []
    for part in text.split(";"):
        suffix, _, share = part.partition(":")
        out.append((cctld if suffix == "cc" else suffix, float(share)))
    t = sum(s for _, s in out)
    return [(s, x / t) for s, x in out]


def domain_rows(info):
    cur = {r["country"]: r["tlds"] for r in read_tsv(CURATED / "domains.tsv")}
    rows, ev = [], {}
    for cc in sorted(info):
        cctld = info[cc]["tld"].lstrip(".").split(";")[0] or cc.lower()
        if cc in cur:
            spec, ev[cc] = cur[cc], "curated"
        else:
            spec, ev[cc] = REGION_TLDS[info[cc]["region"]], "filled"
        merged = Counter()
        for suffix, share in parse_tlds(spec, cctld):
            merged[suffix] += share
        for suffix, share in sorted(merged.items(), key=lambda t: (-t[1], t[0])):
            rows.append({"country_code": cc, "tld": suffix, "share": share})
    return rows, ev


# ------------------------------------------------------------------ numbers

LITE_ANCHOR_FIRMS = 10000
ANCHOR_COLUMNS = ("q001", "q10", "q25", "q50", "q75", "q90", "q999")


def first_url(sources):
    for part in sources.split(";"):
        part = part.strip()
        if part.startswith("http"):
            return part.split(" ")[0]
    return sources.split(";")[0].strip()


def scheme_rows():
    """curated/schemes.tsv joined with the number survey (labels, safe method, evidence, sources). A
    scheme is served when the curated row says so, else when the survey does ("optional" is not)."""
    survey = {r["scheme"]: r for r in read_tsv(CURATED / "survey_numbers.tsv")}
    rows, curated = [], set()
    for r in read_tsv(CURATED / "schemes.tsv"):
        v = survey[r["key"]]
        curated.add(r["key"])
        if r["serve"] not in ("", "yes", "no"):
            raise ValueError(f"schemes.tsv: {r['key']}: serve must be empty, yes or no")
        if r["serve"] == "no" or (r["serve"] == "" and v["served"] != "yes"):
            continue
        rows.append({"key": r["key"], "country_code": v["cc"] or "*", "kind": v["kind"], "label": v["label"],
                     "native_label": v["native_label"], "short": r["short"], "builder": r["builder"],
                     "display": r["display"], "pattern": r["pattern"], "check": r["check"], "groups": r["groups"],
                     "safe_method": v["safe_method"], "from": r["from"], "until": r["until"], "params": r["params"],
                     "evidence": v["evidence"], "source": first_url(v["sources"])})
    missing = set(k for k, v in survey.items() if v["served"] != "no") - curated
    if missing:
        raise ValueError(f"schemes.tsv: no row for {sorted(missing)}")
    return sorted(rows, key=lambda r: (r["country_code"], r["key"]))


# Code lists copied from id-generator's places.tsv: (its scheme, our list, the codes kept)
ID_LISTS = (("cn-ric", "cn-county", None), ("ru-inn", "ru-region", None),
            ("ro-cnp", "ro-county", {f"{i:02d}" for i in list(range(1, 41)) + [51, 52]}),
            ("th-pin", "th-province", None))
LITE_COUNTIES = 300       # lite: the most populous Chinese counties
LEI_SHARE = 0.995         # the issuers holding this share of a country's LEIs
LEI_LITE = 4              # lite: at most this many issuers per country


def code_rows(tier):
    """codes.tsv: the curated lists, id-generator's place codes and GLEIF's LEI issuers by country."""
    rows = [{**r, "weight": float(r["weight"])} for r in read_tsv(CURATED / "codes.tsv")]
    places = read_tsv(SIBLINGS / "id-generator/resources/full/places.tsv")
    for scheme, name, keep in ID_LISTS:
        got = {}
        for r in places:
            if r["scheme"] != scheme or r["until"] or (keep and r["code"] not in keep):
                continue
            w = float(r["weight"] or 0)
            if w > 0 and r["code"] not in got:
                got[r["code"]] = {"list": name, "code": r["code"], "label": r["label"], "weight": w, "extra": ""}
        part = sorted(got.values(), key=lambda r: r["code"])
        if name == "cn-county" and tier == "lite":
            top = sorted(part, key=lambda r: (-r["weight"], r["code"]))[:LITE_COUNTIES]
            part = sorted(top, key=lambda r: r["code"])
        rows += part
    lei = load_work("lei.json")
    pooled = Counter()
    for cc, prefixes in lei["prefixes"].items():
        pooled.update(prefixes)
    for cc, prefixes in [("*", dict(pooled)), *sorted(lei["prefixes"].items())]:
        if cc != "*" and len(cc) != 2:
            continue
        total, acc = sum(prefixes.values()), 0
        for i, (prefix, n) in enumerate(sorted(prefixes.items(), key=lambda kv: (-kv[1], kv[0]))):
            if (tier == "lite" and i >= LEI_LITE) or (i > 0 and acc >= LEI_SHARE * total):
                break
            rows.append({"list": f"lei-lou:{cc}", "code": prefix, "label": "", "weight": float(n), "extra": ""})
            acc += n
    if any(p == "ZZZZ" for p in lei["every"]):
        raise ValueError("the safe LEI prefix ZZZZ is held by an LEI in the golden copy")
    return rows


def anchor_rows(tier):
    """lite keeps the yearly rows of series with LITE_ANCHOR_FIRMS firms or more (the small UK
    series keep their highest number only, enough for safe mode)."""
    rows = []
    for scheme, v in sorted(load_work("anchors.json").items()):
        for series, s in sorted(v["series"].items()):
            firms = sum(x[-1] for x in s["years"].values())
            years = s["years"] if tier == "full" or firms >= LITE_ANCHOR_FIRMS else {}
            if tier == "lite":   # every year from 1990, every fifth before; readers interpolate between years
                years = {y: x for y, x in years.items() if int(y) >= 1990 or int(y) % 5 == 0}
            for year, (*qs, n) in sorted(years.items()):
                rows.append({"scheme": scheme, "series": series or "-", "year": int(year),
                             **dict(zip(ANCHOR_COLUMNS, qs)), "count": n})
            rows.append({"scheme": scheme, "series": series or "-", "year": "highest", "q999": s["highest"],
                         "source": v["source"]})
    return rows


# ------------------------------------------------------------------ main

def main():
    sys.stdout.reconfigure(encoding="utf-8")
    info = countries()
    econ = prepare_economy.build(info)
    forms = prepare_forms.build(info, econ)
    words = load_words()
    names = name_languages()
    emp, emp_ev, emp_src, ent, ent_ev, ent_src = econ["industries"]
    size_rows = size_tables(econ)
    _, size_ev = econ["sizes"]
    occ_rows, occ_ev = econ["occupations"]
    age_rows, young = econ["ages"]
    dom_rows, dom_ev = domain_rows(info)

    # ---- countries.tsv
    def countries_rows(tier, n):
        rows = []
        for cc in sorted(info):
            langs, culture = names[cc]
            inc = info[cc]["income_level"]
            nd = NO_DOMAIN.get(inc, NO_DOMAIN["Upper middle income"])
            y = young.get(cc)
            rows.append({
                "country_code": cc, "enterprises": round(sum(ent[cc].values())),
                "persons_employed": round(sum(emp[cc].values())),
                "languages": ";".join(f"{c}:{s:g}" for c, s in langs), "culture": culture,
                "industry_evidence": ent_ev[cc], "employment_evidence": emp_ev[cc],
                "size_evidence": size_ev.get(cc, ("filled", ""))[0],
                "form_evidence": forms["evidence"][cc],
                "age_evidence": "register" if cc in ("FR", "NO", "GB") else ("measured" if y else "filled"),
                "young_share": sig(y[0], n) if y else "",
                "domain_evidence": dom_ev[cc],
                **{f"no_domain_{b}": nd[i] for i, b in enumerate(BANDS)},
                "industry_source": ent_src[cc], "employment_source": emp_src[cc],
                "size_source": size_ev.get(cc, ("", "subregion's pooled sizes"))[1],
                "form_source": forms["sources"][cc], "age_source": y[1] if y else "",
            })
        return rows
    write_both("countries.tsv",
               "per country: enterprises and persons employed (see doc/data.md for the sources by field), the "
               "languages business names are written in (curated), the surname culture (name-generator), the "
               "evidence of each field, the share of firms up to five years old (empty: the US shape as it is) "
               "and the share of firms without a website by size band",
               ["country_code", "enterprises", "persons_employed", "languages", "culture", "industry_evidence",
                "employment_evidence", "size_evidence", "form_evidence", "age_evidence", "young_share",
                "domain_evidence", *[f"no_domain_{b}" for b in BANDS], "industry_source", "employment_source",
                "size_source", "form_source", "age_source"], countries_rows)

    # ---- industries.tsv
    def industries_rows(tier, n):
        rows = []
        for cc in sorted(info):
            te, tp = sum(ent[cc].values()), sum(emp[cc].values())
            for d in sorted(set(ent[cc]) | set(emp[cc])):
                e, p = per_million(ent[cc].get(d, 0.0) / te, n), per_million(emp[cc].get(d, 0.0) / tp, n)
                floor = LITE_DIVISION_FLOOR if tier == "lite" else 1
                if e >= floor or p >= floor:
                    rows.append({"country_code": cc, "division": d, "enterprises": e, "employment": p})
        return rows
    write_both("industries.tsv", "the country's enterprises and persons employed by ISIC Rev. 4 division, per "
               "million of its total (lite: divisions under 50 per million in both left out); evidence and sources "
               "in countries.tsv",
               ["country_code", "division", "enterprises", "employment"], industries_rows)

    # ---- sizes.tsv
    def sizes_rows(tier, n):
        rows = size_rows if tier == "full" else sections_of_sizes(size_rows, ent)
        return [{"country_code": r["country_code"], "isic": r["isic"], "band": r["band"],
                 "enterprise_share": sig(r["enterprise_share"], n),
                 "employment_share": sig(r["employment_share"] or 0.0, n),
                 "mean_persons": sig(r["mean_persons"], n)}
                for r in sorted(rows, key=lambda r: (r["country_code"], r["isic"], BANDS.index(r["band"])))]
    write_both("sizes.tsv", "size bands (micro 1-9, small 10-49, medium 50-249, large 250+ persons employed) by "
               "ISIC division or section: the share of enterprises and of persons employed, and the mean head "
               "count in the band; country * is the pooled default (lite: sections only)",
               ["country_code", "isic", "band", "enterprise_share", "employment_share", "mean_persons"], sizes_rows)

    # ---- ages.tsv
    def ages_rows(tier, n):
        cells = defaultdict(dict)
        for r in age_rows:
            cells[(r["country_code"], r["section"], r["band"])][r["age"]] = r["share"]
        return [{"country_code": cc, "section": s, "band": b, **{f"age_{a}": sig(v.get(a, 0.0), n) for a in AGE_CLASSES}}
                for (cc, s, b), v in sorted(cells.items(), key=lambda t: (t[0][0], t[0][1], band_order(t[0][2])))]
    write_both("ages.tsv", "firm age in years by section and size band: country * is the US's (Census BDS 2023, "
               "employer firms), FR, NO and GB their registers' (band * for every band: the UK register has no sizes); other countries scale the first five years to "
               "their young_share (countries.tsv)",
               ["country_code", "section", "band", *[f"age_{a}" for a in AGE_CLASSES]], ages_rows)

    # ---- occupations.tsv (wide: one column per section)
    def occ_out(tier, n):
        cells = defaultdict(dict)
        for r in occ_rows:
            cells[(r["country_code"], r["isco"])][r["section"]] = r["employment"]
        tot = defaultdict(float)
        for (cc, _), v in cells.items():
            tot[cc] += sum(v.values())
        return [{"country_code": cc, "isco": isco, **{s: per_million(v.get(s, 0.0) / tot[cc], n) for s in SECTS}}
                for (cc, isco), v in sorted(cells.items())]
    write_both("occupations.tsv", "persons employed by ISCO-08 major group and ISIC section, per million of the "
               "country's employment (ILOSTAT EMP_TEMP_ECO_OCU_NB_A); country * is the pooled default",
               ["country_code", "isco", *SECTS], occ_out)

    # ---- legal_forms.tsv, groups.tsv, form_odds.tsv
    def forms_rows(tier, n):
        rows, top = [], set()
        keyed, used = [], set()      # the key: the ISO 20275 code, else the Latin name; the same in both tiers
        for r in forms["legal_forms"]:
            base = r["code"] or form_slug(r["latin_name"] or r["name"])
            key, i = base, 2
            while (r["country_code"], key) in used:
                key, i = f"{base}_{i}", i + 1
            used.add((r["country_code"], key))
            keyed.append({"key": key, **r})
        prepare_forms.apply_name_forms(keyed)
        for r in keyed:   # sorted by share within each group: the first is the top form
            key = (r["country_code"], r["group"])
            first = key not in top
            top.add(key)
            if tier == "lite" and not first and max(r["share_small"], r["share_large"]) < LITE_FORM_SHARE:
                continue
            rows.append(dict(r))
        tot = defaultdict(lambda: [0.0, 0.0])   # lite: the forms kept share the whole group
        for r in rows:
            tot[(r["country_code"], r["group"])][0] += r["share_small"]
            tot[(r["country_code"], r["group"])][1] += r["share_large"]
        for r in rows:
            ts, tl = tot[(r["country_code"], r["group"])]
            r["share_small"] = sig(r["share_small"] / ts, n) if ts else 0.0
            r["share_large"] = sig(r["share_large"] / tl, n) if tl else 0.0
        return rows
    write_both("legal_forms.tsv", "legal forms by country and group: ISO 20275 code (empty for curated forms), "
               "abbreviation and name in the name language and in Latin letters (empty: the same), the dates a "
               "form was open to new firms, the sections or divisions it is kept to (only) or kept out of (not), "
               "and its share of the group among small (micro, small) and large (medium, large) firms (lite: forms "
               "under 1% of their group left out); in_name: how the form is written in a name (scripts/curated/"
               "name_forms.tsv; empty: the abbreviation, by the name language's style)",
               ["country_code", "key", "code", "group", "abbreviation", "latin_abbreviation", "name", "latin_name", "in_name",
                "from", "until", "only", "not", "share_small", "share_large", "evidence"], forms_rows)

    def groups_rows(tier, n):
        cells = defaultdict(dict)
        ev = {}
        for r in forms["groups"]:
            cells[(r["country_code"], r["section"])][r["group"]] = r["base"]
            ev[(r["country_code"], r["section"])] = r["evidence"]
        return [{"country_code": cc, "section": s, **{g: sig(v[g], n) for g in GROUPS}, "evidence": ev[(cc, s)]}
                for (cc, s), v in sorted(cells.items())]
    write_both("groups.tsv", "the base weight of each legal-form group: P(group | country, section, band) is "
               "base x odds (form_odds.tsv), normalised. A country's own row for the section when it has one; "
               "else the pooled row (country *) times the country's multipliers (section *), the multipliers "
               "applying to micro and small firms only (medium and large firms take the pooled row for every "
               "group the country has); see doc/data.md",
               ["country_code", "section", *GROUPS, "evidence"], groups_rows)

    def odds_rows(tier, n):
        cells = defaultdict(dict)
        for r in forms["form_odds"]:
            cells[(r["section"], r["band"])][r["group"]] = r["odds"]
        return [{"section": s, "band": b, **{g: sig(v.get(g, 0.0), n) for g in GROUPS}}
                for (s, b), v in sorted(cells.items(), key=lambda t: (t[0][0], BANDS.index(t[0][1])))]
    write_both("form_odds.tsv", "odds of each legal-form group by section and size band, pooled from France's "
               "SIRENE and Norway's Enhetsregisteret; section * serves cells the registers count too thinly",
               ["section", "band", *GROUPS], odds_rows)

    # ---- words.tsv, patterns.tsv, surnames.tsv, places.tsv, domains.tsv
    def words_out(tier, n):
        keep = None if tier == "full" else (lambda kind: LITE_WORDS.get(kind))
        return [{"language": lang, "kind": kind, "key": key, "word": word, "latin": latin,
                 "weight": sig(w, n)} for lang, kind, key, word, latin, w in words_rows(words, keep)]
    write_both("words.tsv", "business-name words by language (curated, scripts/curated/words): styles, "
               "templates for each pattern, trade words per ISIC division (or section), adjectives, region and "
               "place words, family words, joiners and the syllables of coined names; latin: the word in Latin "
               "letters where it is not",
               ["language", "kind", "key", "word", "latin", "weight"], words_out)

    base, tilts = pattern_rows()

    def patterns_out(tier, n):
        rows = []
        for lang in ["*"] + sorted(x for x in words if x in tilts):   # "*": every language without a tilt
            t = tilts.get(lang, {})
            for r in base:
                x = [float(r[p]) * float(t.get(p) or 1) for p in PATTERNS]
                tot = sum(x)
                rows.append({"language": lang, "group": r["group"], "band": r["band"],
                             **{p: sig(v / tot, n) for p, v in zip(PATTERNS, x)}})
        return rows
    write_both("patterns.tsv", "the share of each name pattern by name language, legal-form group and size band "
               "(curated estimates, design 5.6); language * serves every language without rows of its own", ["language", "group", "band", *PATTERNS], patterns_out)

    write_both("surnames.tsv", "surnames by culture for the countries whose name language has none of its own: "
               "name-generator's lists (decision 7), curated lists for the x_ cultures; weight relative to the "
               "most common", ["culture", "surname", "latin", "weight"],
               lambda tier, n: [{**r, "weight": sig(r["weight"], n)}
                                for r in surname_rows(words, names, SURNAMES[tier])])
    write_both("places.tsv", "places for business names where the name language has none of its own: the "
               "country's largest places in address-generator (GeoNames), weighted by the square root of their "
               "population", ["country_code", "place", "weight"],
               lambda tier, n: [{**r, "weight": sig(r["weight"], n)} for r in place_rows(words, names, info, PLACES[tier])])
    write_both("domains.tsv", "top-level domains of company websites by country (curated estimates, "
               "scripts/curated/domains.tsv; regional defaults where it has no row)", ["country_code", "tld", "share"],
               lambda tier, n: [{**r, "share": sig(r["share"], n)} for r in dom_rows])

    write_both("isic.tsv", "ISIC Rev. 4 sections and divisions with their titles (UN Statistics Division, Statistical "
               "Papers Series M No. 4/Rev.4, 2008), lower-cased as the result's to_string() prints them; a section row "
               "(a letter) is followed by its divisions", ["code", "label"],
               lambda tier, n: [{"code": r["code"], "label": r["label"]} for r in read_tsv(CURATED / "isic.tsv")])
    write_both("schemes.tsv", "registration and tax numbers by country: how each is built (builder, pattern, check; "
               "see scripts/curated/schemes.tsv for the notation), the legal-form groups holding it (S, P, L, O) and "
               "its safe method; from the number survey (designs/company-generator-numbers.tsv)",
               ["key", "country_code", "kind", "label", "native_label", "short", "builder", "display", "pattern",
                "check", "groups", "safe_method", "from", "until", "params", "evidence", "source"],
               lambda tier, n: scheme_rows())
    write_both("codes.tsv", "code lists the number builders draw from: Indian states, Hungarian tax directorates and "
               "German register courts (curated, scripts/curated/codes.tsv), Chinese counties, Russian regions, "
               "Romanian counties and Thai provinces (copied from id-generator), and the issuers (LOU prefixes) of "
               "each country's active LEIs (GLEIF golden copy, CC0; * pools every country); weight relative (lite: "
               f"the {LITE_COUNTIES} most populous Chinese counties, at most {LEI_LITE} issuers a country)",
               ["list", "code", "label", "weight", "extra"],
               lambda tier, n: [{**r, "weight": sig(r["weight"], n)} for r in code_rows(tier)])
    fold = read_tsv(SIBLINGS / "email-generator/resources/full/fold.tsv")
    write_both("fold.tsv", "letters and their ASCII form, for domains (copied from email-generator)", ["from", "to"],
               lambda tier, n: fold)
    write_both("anchors.tsv", "issue anchors of sequential numbers, from the whole registers: for the firms registered "
               "in a year, the 0.1, 10, 25, 50, 75, 90 and 99.9% points of their numbers (without the check digit) "
               "and their count; the row \"highest\" holds the highest number issued by the snapshot in q999 (lite: "
               "the yearly rows of series with 10,000 firms or more, every fifth year before 1990)",
               ["scheme", "series", "year", *ANCHOR_COLUMNS, "count", "source"],
               lambda tier, n: anchor_rows(tier))

    for tier in TIERS:
        total = sum(p.stat().st_size for p in (RESOURCES / tier).glob("*.tsv"))
        print(f"{tier}: {total / 1e6:.2f} MB")


if __name__ == "__main__":
    main()
