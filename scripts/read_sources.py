#!/usr/bin/env python3
"""Read the statistical sources in raw_data/ into raw_data/work/stats.json.

What it keeps, per country (ISO alpha-2):
- `cells`: enterprises and persons employed by ISIC division and size band, from Eurostat SBS
  (Europe), OECD SDBS (Brazil, Costa Rica, Israel, Turkey, New Zealand, the US) and, for the US,
  the Nonemployer Statistics added to the micro band. Confidential cells are filled from the
  country's section or the source's division mix (`complete`).
- `section_cells`: the same by section only, for the SDBS areas that publish nothing finer.
- `ilo_div`, `ilo_sec`, `ilo_occ`: ILOSTAT employment by division, by section and by
  section x ISCO-08 major group; the latest year a labour force survey covers well.
- `forms`: Eurostat's legal-form groups (sole proprietorship; partnership, co-operative,
  association; limited liability) by section, enterprises and persons employed.
- `young`: the share of active enterprises aged 0 to 5 years by employee class (Eurostat
  business demography: births and survivors over active enterprises).
And for the US alone, `us_age`: firms by section, size band and age class from the BDS 2023,
with persons employed, which gives the age shape everywhere past five years.
"""
import csv
import io
import re
import zipfile
from collections import defaultdict

from companygen_data import (AGE_CLASSES, BANDS, DIVISIONS, RAW, SECTIONS, countries, eurostat, save_work,
                             section_of)

SERVED = countries()
A3 = {r["cca3"]: cc for cc, r in SERVED.items()}
# Divisions the library draws: T (households as employers) and U (extraterritorial bodies) are no
# businesses; O (public administration) stays, a worker's employer can be the state.
DRAWN = [d for d in DIVISIONS if section_of(d) not in ("T", "U")]
SBS_BANDS = {"micro": ["0-9"], "small": ["10-19", "20-49"], "medium": ["50-249"], "large": ["GE250"]}
SDBS_BANDS = {"micro": ["S1T9"], "small": ["S10T49"], "medium": ["S50T249"], "large": ["S_GE250"]}
# BDS firm size is employees; the bands are persons employed. 20-99 and 100-499 straddle the
# limits; the age shape is taken from the nearest classes (micro 1-9, small 10-99, medium
# 100-499, large 500+), which is close enough for a shape.
BDS_BANDS = {"a) 1 to 4": "micro", "b) 5 to 9": "micro", "c) 10 to 19": "small", "d) 20 to 99": "small",
             "e) 100 to 499": "medium", "f) 500 to 999": "large", "g) 1000 to 2499": "large",
             "h) 2500 to 4999": "large", "i) 5000 to 9999": "large", "j) 10000+": "large"}
BDS_AGES = {"a) 0": "0", "b) 1": "1", "c) 2": "2", "d) 3": "3", "e) 4": "4", "f) 5": "5", "g) 6 to 10": "6-10",
            "h) 11 to 15": "11-15", "i) 16 to 20": "16-20", "j) 21 to 25": "21-25", "k) 26+": "26+",
            "l) Left Censored": "26+"}
# NAICS sectors (BDS) to ISIC sections; where a section has none (O, public administration),
# the whole economy's shape is used.
NAICS_SECTION = {"11": "A", "21": "B", "22": "D", "23": "F", "31-33": "C", "42": "G", "44-45": "G", "48-49": "H",
                 "51": "J", "52": "K", "53": "L", "54": "M", "55": "M", "56": "N", "61": "P", "62": "Q", "71": "R",
                 "72": "I", "81": "S"}
LEGAL_GROUPS = {"ENT_SOLE": "sole_proprietorship", "ENT_PA": "partnership", "ENT_LL": "limited_liability"}


def num(text):
    try:
        return float(text)
    except (TypeError, ValueError):
        return None


def complete(total, parts, ref):
    """Fill the missing parts of a total: the residual spread over the missing parts by `ref`
    (shares by part). Known parts are kept; a negative residual is taken as zero."""
    known = {k: v for k, v in parts.items() if v is not None}
    missing = [k for k, v in parts.items() if v is None]
    if not missing:
        return dict(known)
    residual = max(0.0, (total or 0.0) - sum(known.values())) if total is not None else None
    weights = {k: max(ref.get(k, 0.0), 0.0) for k in missing}
    wsum = sum(weights.values())
    out = dict(known)
    for k in missing:
        if residual is None:
            out[k] = None
        else:
            out[k] = residual * (weights[k] / wsum if wsum > 0 else 1 / len(missing))
    return out


def shares(d):
    s = sum(v for v in d.values() if v)
    return {k: (v or 0.0) / s for k, v in d.items()} if s > 0 else {}


# ------------------------------------------------------------------ Eurostat SBS

def read_sbs():
    ids, v, _ = eurostat("sbs_sc_ovw")
    pos = {name: i for i, name in enumerate(ids)}
    data = defaultdict(dict)   # (geo, time) -> {(indic, nace, size): value}
    for key, value in v.items():
        data[(key[pos["geo"]], key[pos["time"]])][(key[pos["indic_sbs"]], key[pos["nace_r2"]], key[pos["size_emp"]])] = value
    # The year per country: the most division cells, the latest on ties.
    best = {}
    for (geo, t), cells in data.items():
        n = sum(1 for (i, nace, s) in cells if i == "ENT_NR" and len(nace) == 3 and nace[1:].isdigit())
        if geo not in best or (n, t) > best[geo][0]:
            best[geo] = ((n, t), t)
    # The source's division mix by size, over every country, for cells a country withholds.
    pooled = defaultdict(lambda: defaultdict(float))
    for geo, (_, t) in best.items():
        cells = data[(geo, t)]
        for (indic, nace, size), value in cells.items():
            if len(nace) == 3 and nace[1:].isdigit():
                pooled[(indic, nace[1:])][size] += value
    out = {}
    for geo, (_, t) in sorted(best.items()):
        if geo not in SERVED:
            continue
        cells = data[(geo, t)]
        divs = {}
        for d in DRAWN:
            nace = section_of(d) + d
            band_values = {}
            for indic in ("ENT_NR", "EMP_NR"):
                total = cells.get((indic, nace, "TOTAL"))
                if total is None:
                    break
                sizes = {s: cells.get((indic, nace, s)) for b in BANDS for s in SBS_BANDS[b]}
                ref = {s: pooled[(indic, d)].get(s, 0.0) for s in sizes}
                filled = complete(total, sizes, ref)
                band_values[indic] = {b: sum(filled[s] or 0.0 for s in SBS_BANDS[b]) for b in BANDS}
            if "ENT_NR" not in band_values:
                continue
            if "EMP_NR" not in band_values:   # persons employed withheld: the pooled mean per band
                band_values["EMP_NR"] = {}
                for b in BANDS:
                    e = sum(pooled[("ENT_NR", d)].get(s, 0.0) for s in SBS_BANDS[b])
                    p = sum(pooled[("EMP_NR", d)].get(s, 0.0) for s in SBS_BANDS[b])
                    band_values["EMP_NR"][b] = band_values["ENT_NR"][b] * (p / e if e > 0 else 0.0)
            divs[d] = {b: [round(band_values["ENT_NR"][b], 1), round(band_values["EMP_NR"][b], 1)] for b in BANDS}
        if divs:
            out[geo] = {"year": t, "source": f"Eurostat SBS sbs_sc_ovw {t}", "divisions": divs}
    return out


# ------------------------------------------------------------------ OECD SDBS

def read_sdbs():
    """SDBS by area: divisions where it has them, else sections."""
    data = defaultdict(dict)   # (area, time) -> {(measure, activity, size): value}
    with open(RAW / "sdbs" / "sdbs_isic4.csv", encoding="utf-8", newline="") as f:
        for r in csv.DictReader(f):
            area = A3.get(r["REF_AREA"])
            if not area or not r["OBS_VALUE"]:
                continue
            a = r["ACTIVITY"]
            if not (len(a) == 1 or (len(a) == 3 and a[1:].isdigit())):
                continue
            mult = 10 ** int(r["UNIT_MULT"] or 0)
            data[(area, r["TIME_PERIOD"])][(r["MEASURE"], a, r["SIZE_CLASS"])] = float(r["OBS_VALUE"]) * mult
    best_div, best_sec = {}, {}
    for (area, t), cells in data.items():
        nd = sum(1 for (m, a, s) in cells if m == "ENTR" and len(a) == 3 and s in ("S1T9", "S10T49", "S50T249", "S_GE250"))
        ns = sum(1 for (m, a, s) in cells if m == "ENTR" and len(a) == 1 and s in ("S1T9", "S10T49", "S50T249", "S_GE250"))
        if nd and (area not in best_div or nd > best_div[area][0] * 1.25 or
                   (nd >= best_div[area][0] * 0.8 and t > best_div[area][1])):
            best_div[area] = (nd, t)
        if ns and (area not in best_sec or (ns, t) > best_sec[area]):
            best_sec[area] = (ns, t)
    divisions, sections = {}, {}
    for area, (nd, t) in best_div.items():
        if nd < 200:   # fewer than about 50 divisions with every band: not a division table
            continue
        cells = data[(area, t)]
        divs = {}
        for d in DRAWN:
            a = section_of(d) + d
            vals = {}
            for m in ("ENTR", "EMPE"):
                sizes = {s: cells.get((m, a, s)) for b in BANDS for s in SDBS_BANDS[b]}
                total = cells.get((m, a, "_T"))
                if total is None and all(x is not None for x in sizes.values()):
                    total = sum(sizes.values())
                if total is None:
                    continue
                sec = {s: cells.get((m, section_of(d), s)) or 0.0 for s in sizes}
                vals[m] = complete(total, sizes, sec)
            if "ENTR" not in vals:
                continue
            ent = {b: vals["ENTR"][SDBS_BANDS[b][0]] or 0.0 for b in BANDS}
            if "EMPE" in vals:
                emp = {b: vals["EMPE"][SDBS_BANDS[b][0]] or 0.0 for b in BANDS}
            else:
                emp = {b: None for b in BANDS}
            divs[d] = {b: [round(ent[b], 1), None if emp[b] is None else round(emp[b], 1)] for b in BANDS}
        if len(divs) >= 50:
            divisions[area] = {"year": t, "source": f"OECD SDBS DF_SDBS_ISIC4 {t}", "divisions": divs}
    for area, (ns, t) in best_sec.items():
        if area in divisions:
            continue
        cells = data[(area, t)]
        secs = {}
        for s in SECTIONS:
            ent = {b: cells.get(("ENTR", s, SDBS_BANDS[b][0])) for b in BANDS}
            emp = {b: cells.get(("EMPE", s, SDBS_BANDS[b][0])) for b in BANDS}
            total = cells.get(("ENTR", s, "_T"))
            if all(x is None for x in ent.values()) and total is None:
                continue
            secs[s] = {"enterprises": ent, "employment": emp, "total": total,
                       "employment_total": cells.get(("EMPE", s, "_T"))}
        if secs:
            sections[area] = {"year": t, "source": f"OECD SDBS DF_SDBS_ISIC4 {t} (sections)", "sections": secs}
    return divisions, sections


# ------------------------------------------------------------------ US

def naics_to_divisions():
    """NAICS 2017 code (2 to 6 digits) -> {ISIC division: weight}, from the Census concordance.

    A 6-digit code split across several ISIC classes counts equally in each; a shorter code is
    the sum of its 6-digit codes."""
    import openpyxl
    import warnings
    with warnings.catch_warnings():
        warnings.simplefilter("ignore")
        ws = openpyxl.load_workbook(str(RAW / "us" / "naics2017_isic4.xlsx"), data_only=True).worksheets[0]
    six = defaultdict(set)
    for row in ws.iter_rows(min_row=2, values_only=True):
        naics, isic = row[1], row[4]
        if not naics or not isic:
            continue
        naics, isic = str(naics).strip(), f"{int(isic):04d}"
        if len(naics) == 6 and isic[:2] in DRAWN:
            six[naics].add(isic[:2])
    out = defaultdict(lambda: defaultdict(float))
    for code, divs in six.items():
        for n in range(2, 7):
            for d in divs:
                out[code[:n]][d] += 1.0 / len(divs)
    return out


def read_nes(crosswalk):
    """Nonemployer firms (one person employed: the owner) by ISIC division and legal form."""
    with zipfile.ZipFile(RAW / "us" / "nonemp22us.zip") as z:
        name = next(n for n in z.namelist() if n.endswith(".txt"))
        text = z.read(name).decode("latin-1")
    rows = list(csv.DictReader(io.StringIO(text)))
    lfo_group = {"S": "sole_proprietorship", "P": "partnership", "C": "limited_liability", "Z": "limited_liability"}
    by_code = {}
    for r in rows:
        if r["ST"] != "00" or r["RCPTOT_SIZE"] != "001" or not r["ESTAB"]:
            continue
        by_code[(r["NAICS"], r["LFO"])] = float(r["ESTAB"])
    # The finest codes: those with no published child.
    codes = {c for c, _ in by_code if c != "00"}
    leaves = [c for c in codes if not any(o != c and o.startswith(c) for o in codes)]
    divisions = defaultdict(float)
    groups = defaultdict(lambda: defaultdict(float))
    unmapped = 0.0
    for c in leaves:
        total = by_code.get((c, "-"), 0.0)
        cw = None
        for n in range(len(c), 1, -1):   # 2022 codes the 2017 concordance lacks: the parent's mix
            if c[:n] in crosswalk:
                cw = crosswalk[c[:n]]
                break
        if not cw:
            unmapped += total
            continue
        wsum = sum(cw.values())
        for d, w in cw.items():
            divisions[d] += total * w / wsum
            for lfo, g in lfo_group.items():
                groups[section_of(d)][g] += by_code.get((c, lfo), 0.0) * w / wsum
    return {"divisions": {d: round(v, 1) for d, v in divisions.items()},
            "groups": {s: {g: round(v, 1) for g, v in gs.items()} for s, gs in groups.items()},
            "total": by_code[("00", "-")], "unmapped": unmapped}


def read_bds():
    """US firms by section, band and age class, with persons employed (2023)."""
    firms = defaultdict(float)
    emp = defaultdict(float)
    with open(RAW / "us" / "bds2023_sec_fa_fz.csv", encoding="utf-8", newline="") as f:
        for r in csv.DictReader(f):
            if r["year"] != "2023":
                continue
            s = NAICS_SECTION[r["sector"]]
            b = BDS_BANDS[r["fsize"]]
            a = BDS_AGES[r["fage"]]
            n, e = num(r["firms"]), num(r["emp"])
            if n:
                firms[(s, b, a)] += n
            if e:
                emp[(s, b, a)] += e
    out = {}
    for s in SECTIONS:
        for b in BANDS:
            row = {a: firms.get((s, b, a), 0.0) for a in AGE_CLASSES}
            if sum(row.values()) > 0:
                out[f"{s}|{b}"] = {a: round(v, 1) for a, v in row.items()}
    totals = {f"{s}|{b}": [round(sum(firms.get((s, b, a), 0.0) for a in AGE_CLASSES), 1),
                           round(sum(emp.get((s, b, a), 0.0) for a in AGE_CLASSES), 1)]
              for s in SECTIONS for b in BANDS}
    return out, totals


def compose_us(sdbs_us, nes, bds_totals):
    """The US by division and band: SDBS 2015's employer firms scaled to the BDS 2023 totals of
    their section and band, plus the nonemployers in the micro band (one person each)."""
    divs = {d: {b: list(v) for b, v in cells.items()} for d, cells in sdbs_us["divisions"].items()}
    for s in SECTIONS:
        for b in BANDS:
            have = [d for d in divs if section_of(d) == s]
            ent = sum(divs[d][b][0] for d in have)
            emp = sum(divs[d][b][1] or 0.0 for d in have)
            tgt_ent, tgt_emp = bds_totals.get(f"{s}|{b}", [0.0, 0.0])
            if ent > 0 and tgt_ent > 0:
                for d in have:
                    divs[d][b][0] *= tgt_ent / ent
            if emp > 0 and tgt_emp > 0:
                for d in have:
                    # persons employed: employees plus the owner of each firm
                    divs[d][b][1] = (divs[d][b][1] or 0.0) * tgt_emp / emp + divs[d][b][0]
    for d, n in nes["divisions"].items():
        cell = divs.setdefault(d, {b: [0.0, 0.0] for b in BANDS})
        cell["micro"][0] += n
        cell["micro"][1] = (cell["micro"][1] or 0.0) + n
    divs = {d: {b: [round(v[0], 1), round(v[1] or 0.0, 1)] for b, v in cells.items()} for d, cells in divs.items()}
    return {"year": "2023", "source": "OECD SDBS 2015 (divisions), scaled to Census BDS 2023 (employer firms) "
                                      "+ Nonemployer Statistics 2022", "divisions": divs}


# ------------------------------------------------------------------ ILOSTAT

def ilo_rows(name):
    with open(RAW / "ilo" / f"{name}.csv", encoding="utf-8-sig", newline="") as f:
        yield from csv.DictReader(f)


def pick(groups, minimum):
    """The (time, source) with the most cells, among the latest years reaching `minimum`;
    labour force surveys (BA) first."""
    best = None
    for (t, src), cells in groups.items():
        if len(cells) < minimum:
            continue
        key = (t, src.startswith("BA"), len(cells))
        if best is None or key > best[0]:
            best = (key, (t, src))
    return best[1] if best else None


def read_ilo():
    div = defaultdict(lambda: defaultdict(dict))
    for r in ilo_rows("EMP_TEMP_SEX_EC2_NB_A"):
        cc = A3.get(r["ref_area"])
        c = r["classif1"]
        if cc and c.startswith("EC2_ISIC4_") and c[-2:].isdigit() and r["obs_value"]:
            div[cc][(r["time"], r["source"])][c[-2:]] = float(r["obs_value"]) * 1000
    sec = defaultdict(lambda: defaultdict(dict))
    for r in ilo_rows("EMP_TEMP_SEX_ECO_NB_A"):
        cc = A3.get(r["ref_area"])
        c = r["classif1"]
        if cc and c.startswith("ECO_ISIC4_") and len(c) == 11 and r["obs_value"]:
            sec[cc][(r["time"], r["source"])][c[-1]] = float(r["obs_value"]) * 1000
    occ = defaultdict(lambda: defaultdict(dict))
    for r in ilo_rows("EMP_TEMP_ECO_OCU_NB_A"):
        cc = A3.get(r["ref_area"])
        c1, c2 = r["classif1"], r["classif2"]
        if (cc and c1.startswith("ECO_ISIC4_") and len(c1) == 11 and c2.startswith("OCU_ISCO08_")
                and c2[-1].isdigit() and r["obs_value"]):
            occ[cc][(r["time"], r["source"])][f"{c2[-1]}|{c1[-1]}"] = float(r["obs_value"]) * 1000
    out_div, out_sec, out_occ = {}, {}, {}
    for cc, groups in div.items():
        k = pick(groups, 40)
        if k:
            out_div[cc] = {"year": k[0], "source": f"ILOSTAT EMP_TEMP_SEX_EC2_NB_A {k[0]} ({k[1]})",
                           "values": {d: round(v) for d, v in groups[k].items()}}
    for cc, groups in sec.items():
        k = pick(groups, 15)
        if k:
            out_sec[cc] = {"year": k[0], "source": f"ILOSTAT EMP_TEMP_SEX_ECO_NB_A {k[0]} ({k[1]})",
                           "values": {s: round(v) for s, v in groups[k].items()}}
    for cc, groups in occ.items():
        k = pick(groups, 60)
        if k:
            out_occ[cc] = {"year": k[0], "source": f"ILOSTAT EMP_TEMP_ECO_OCU_NB_A {k[0]} ({k[1]})",
                           "values": {key: round(v) for key, v in groups[k].items()}}
    return out_div, out_sec, out_occ


def read_lfs():
    """Eurostat LFS employment by ISCO major group x NACE section, where ILOSTAT has none."""
    ids, v, _ = eurostat("lfsa_eisn2")
    pos = {name: i for i, name in enumerate(ids)}
    latest = {}
    for key in v:
        geo, t = key[pos["geo"]], key[pos["time"]]
        latest[geo] = max(latest.get(geo, t), t)
    out = {}
    for key, value in v.items():
        geo, t = key[pos["geo"]], key[pos["time"]]
        isco, nace = key[pos["isco08"]], key[pos["nace_r2"]]
        if geo not in SERVED or t != latest[geo] or not re.fullmatch(r"OC\d", isco) or len(nace) != 1:
            continue
        o = out.setdefault(geo, {"year": t, "source": f"Eurostat LFS lfsa_eisn2 {t}", "values": {}})
        o["values"][f"{isco[2]}|{nace}"] = round(value * 1000)
    return out


# ------------------------------------------------------------------ Eurostat business demography

def read_forms():
    """Enterprises and persons employed by legal-form group and section, latest year."""
    ids, v, _ = eurostat("bd_l_form")
    pos = {name: i for i, name in enumerate(ids)}
    data = defaultdict(dict)
    for key, value in v.items():
        nace = key[pos["nace_r2"]]
        if not (len(nace) == 1 or nace == "B-S_X_O_S94"):
            continue
        data[(key[pos["geo"]], key[pos["time"]])][(key[pos["indic_sbs"]], key[pos["leg_form"]], nace)] = value
    best = {}
    for (geo, t), cells in data.items():
        n = len(cells)
        if geo not in best or (n, t) > best[geo]:
            best[geo] = (n, t)
    out = {}
    for geo, (_, t) in best.items():
        if geo not in SERVED:
            continue
        cells = data[(geo, t)]
        secs = {}
        for nace in list(SECTIONS) + ["B-S_X_O_S94"]:
            ent = {g: cells.get(("ENT_NR", lf, nace)) for lf, g in LEGAL_GROUPS.items()}
            emp = {g: cells.get(("EMP_NR", lf, nace)) for lf, g in LEGAL_GROUPS.items()}
            total = cells.get(("ENT_NR", "TOTAL", nace))
            if total is None or all(x is None for x in ent.values()):
                continue
            known = sum(x for x in ent.values() if x is not None)
            # the rest of the total (other forms, and withheld groups) is "other"
            secs["*" if nace == "B-S_X_O_S94" else nace] = {
                "enterprises": {**{g: x for g, x in ent.items() if x is not None}, "other": max(0.0, total - known)},
                "employment": {g: x for g, x in emp.items() if x is not None},
                "withheld": [g for g, x in ent.items() if x is None]}
        if secs:
            out[geo] = {"year": t, "source": f"Eurostat bd_l_form {t}", "sections": secs}
    return out


def read_young():
    """Share of active enterprises aged 0 (births) to 5 (survivors) by employee class."""
    ent_ids, ent, _ = eurostat("bd_size_ent")
    br_ids, br, _ = eurostat("bd_size_brth")
    sv_ids, sv, _ = eurostat("bd_size_srvl")

    def index(ids, values):
        pos = {name: i for i, name in enumerate(ids)}
        out = {}
        for key, value in values.items():
            if key[pos["nace_r2"]] != "B-S_X_O_S94":
                continue
            out[(key[pos["geo"]], key[pos["time"]], key[pos["sizeclas"]], key[pos["age"]])] = value
        return out
    e, b, s = index(ent_ids, ent), index(br_ids, br), index(sv_ids, sv)
    out = {}
    years = sorted({k[1] for k in e}, reverse=True)
    for geo in sorted({k[0] for k in e}):
        if geo not in SERVED:
            continue
        for t in years:
            row = {}
            for cls in ("0", "1-4", "5-9", "GE10", "TOTAL"):
                active = e.get((geo, t, cls, "TOTAL"))
                births = b.get((geo, t, cls, "TOTAL"))
                surv = [s.get((geo, t, cls, f"Y{a}")) for a in range(1, 6)]
                if not active or births is None or any(x is None for x in surv):
                    continue
                row[cls] = [round(x / active, 5) for x in [births] + surv]
            if "TOTAL" in row and len(row) >= 3:
                out[geo] = {"year": t, "source": f"Eurostat bd_size {t} (births and survivors over active enterprises)",
                            "classes": row}
                break
    return out


def main():
    sbs = read_sbs()
    sdbs_div, sdbs_sec = read_sdbs()
    crosswalk = naics_to_divisions()
    nes = read_nes(crosswalk)
    us_age, bds_totals = read_bds()
    cells = dict(sbs)
    for cc, v in sdbs_div.items():
        if cc == "US":
            cells["US"] = compose_us(v, nes, bds_totals)
        elif cc not in cells:
            cells[cc] = v
    ilo_div, ilo_sec, ilo_occ = read_ilo()
    lfs = read_lfs()
    for cc, v in lfs.items():
        ilo_occ.setdefault(cc, v)
    forms = read_forms()
    forms["US"] = {"year": "2022", "source": "Census Nonemployer Statistics 2022 (legal form of organisation)",
                   "sections": {s: {"enterprises": g, "employment": {}, "withheld": []} for s, g in nes["groups"].items()},
                   "nonemployers_only": True}
    young = read_young()
    out = {"cells": cells, "section_cells": {k: v for k, v in sdbs_sec.items() if k not in cells},
           "ilo_div": ilo_div, "ilo_sec": ilo_sec, "ilo_occ": ilo_occ, "forms": forms, "young": young,
           "us_age": us_age, "us_nonemployers": {"total": nes["total"], "unmapped": nes["unmapped"]}}
    save_work("stats.json", out)
    print(f"cells: {len(cells)} countries ({', '.join(sorted(cells))})")
    print(f"section cells: {sorted(out['section_cells'])}")
    print(f"ILOSTAT: divisions {len(ilo_div)}, sections {len(ilo_sec)}, occupation x section {len(ilo_occ)}")
    print(f"legal-form groups: {len(forms)}; young firms: {len(young)}; US age cells: {len(us_age)}")
    print(f"US nonemployers: {nes['total']:,.0f}, unmapped {nes['unmapped']:,.0f}")


if __name__ == "__main__":
    main()
