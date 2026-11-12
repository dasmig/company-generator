"""Industry, size, occupation and age tables (design sections 5.1 to 5.3 and 5.5).

Reads raw_data/work/stats.json (read_sources.py), the registers' counts (read_registers.py) and
curated/census.tsv. Returns the rows of industries.tsv, sizes.tsv, occupations.tsv and ages.tsv
and, per country, the totals and the evidence that countries.tsv carries.

Model:
- Employment by division: ILOSTAT's division table; else its section table split by the
  subregion's division mix (else the world's); else the subregion's mix outright.
- Enterprises by division: Eurostat SBS or OECD SDBS where they publish the division; a census
  row spread over its divisions by employment x the division's enterprises per worker; else
  employment x the division's pooled enterprises per worker x the country's level (its census
  total, else the median of its income group). Agriculture, which few registers hold, is scaled
  by the income group's share of farms that register (curated, below).
- Sizes: the division's band mix where measured, the section's where only sections are, else
  the pooled mix of the measured countries ("*" rows).
- Ages: the US BDS shape by section and band ("*"), the registers' own distributions (GB, FR,
  NO), and per country the share of firms up to five years old (Eurostat business demography,
  else the shape's), which scales the shape's young classes.
"""
import statistics
from collections import defaultdict

from companygen_data import (AGE_CLASSES, BANDS, CURATED, SECTIONS, load_work, read_tsv, section_of)
from read_sources import DRAWN

YOUNG = AGE_CLASSES[:6]
# Share of farms that hold a business registration, by income group (curated: registers hold
# most farms in Europe and the US; elsewhere farming is mostly unregistered household work).
FARM_REGISTERED = {"High income": 1.0, "Upper middle income": 0.35, "Lower middle income": 0.12,
                   "Low income": 0.05, "": 0.12}


def expand_divisions(text):
    out = []
    for part in text.split():
        if "-" in part:
            lo, hi = (int(x) for x in part.split("-"))
            out += [d for d in DRAWN if lo <= int(d) <= hi]
        elif part in DRAWN:
            out.append(part)
    return out


def build(info):
    """info: {cc: country-generator row}. Returns a dict of tables and per-country facts."""
    st = load_work("stats.json")
    regs = {cc: load_work(f"registers_{cc.lower()}.json") for cc in ("GB", "FR", "NO")}
    census = defaultdict(list)
    for r in read_tsv(CURATED / "census.tsv"):
        census[r["country"]].append(r)
    countries = sorted(info)
    sub = {cc: info[cc]["subregion"] for cc in countries}
    income = {cc: info[cc]["income_level"] for cc in countries}

    # ---------------------------------------------------------------- employment by division
    ilo_div, ilo_sec = st["ilo_div"], st["ilo_sec"]
    div_mix = {}   # cc -> {division: share within its section}, from ILOSTAT divisions
    for cc, v in ilo_div.items():
        vals = {d: max(0.0, x) for d, x in v["values"].items() if d in DRAWN}
        sec_tot = defaultdict(float)
        for d, x in vals.items():
            sec_tot[section_of(d)] += x
        div_mix[cc] = {d: (x / sec_tot[section_of(d)] if sec_tot[section_of(d)] > 0 else 0.0) for d, x in vals.items()}

    def pooled_mix(peers):
        acc = defaultdict(float)
        n = 0
        for p in peers:
            if p in ilo_div:
                for d, x in ilo_div[p]["values"].items():
                    if d in DRAWN:
                        acc[d] += max(0.0, x) / max(1.0, sum(ilo_div[p]["values"].values()))
                n += 1
        sec_tot = defaultdict(float)
        for d, x in acc.items():
            sec_tot[section_of(d)] += x
        return {d: x / sec_tot[section_of(d)] for d, x in acc.items() if sec_tot[section_of(d)] > 0}, n

    world_mix, _ = pooled_mix(countries)
    world_sec = defaultdict(float)
    for cc in countries:
        if cc in ilo_sec:
            tot = sum(v for s, v in ilo_sec[cc]["values"].items() if s in SECTIONS and s not in "TU")
            for s, v in ilo_sec[cc]["values"].items():
                if s in SECTIONS and s not in "TU" and tot > 0:
                    world_sec[s] += v / tot
    rates = defaultdict(list)
    for cc in countries:
        if cc in ilo_sec and float(info[cc]["population"] or 0) > 0:
            rates[income[cc]].append(sum(ilo_sec[cc]["values"].values()) / float(info[cc]["population"]))

    def emp_rate(cc):
        r = rates.get(income[cc]) or [x for v in rates.values() for x in v]
        return statistics.median(r)

    emp, emp_ev, emp_src = {}, {}, {}
    for cc in countries:
        if cc in ilo_div and sum(ilo_div[cc]["values"].get(d, 0) for d in DRAWN) > 0:
            emp[cc] = {d: float(max(0, ilo_div[cc]["values"].get(d, 0))) for d in DRAWN}
            emp_ev[cc], emp_src[cc] = "measured", ilo_div[cc]["source"]
            continue
        peers = [p for p in countries if sub[p] == sub[cc] and p in ilo_div and p != cc]
        mix, n = pooled_mix(peers)
        if n < 2:
            mix = world_mix
        if cc in ilo_sec:
            secs = {s: v for s, v in ilo_sec[cc]["values"].items() if s in SECTIONS}
            emp_ev[cc], emp_src[cc] = "measured_section", ilo_sec[cc]["source"] + "; divisions split as the subregion's"
        else:
            peers_sec = [p for p in countries if sub[p] == sub[cc] and p in ilo_sec]
            secs = defaultdict(float)
            for p in peers_sec or countries:
                if p in ilo_sec:
                    tot = sum(ilo_sec[p]["values"].values())
                    for s, v in ilo_sec[p]["values"].items():
                        if s in SECTIONS:
                            secs[s] += v / tot
            emp_ev[cc], emp_src[cc] = "filled", "the subregion's employment mix (ILOSTAT)"
        e = {d: secs.get(section_of(d), 0.0) * mix.get(d, 0.0) for d in DRAWN}
        if cc not in ilo_sec:   # shares only: scaled to population x the income group's employment rate
            k = float(info[cc]["population"]) * emp_rate(cc) / max(1e-9, sum(e.values()))
            e = {d: x * k for d, x in e.items()}
        emp[cc] = e

    # ---------------------------------------------------------------- enterprises by division
    cells = st["cells"]
    measured_n = {cc: {d: sum(b[0] for b in v.values()) for d, v in cells[cc]["divisions"].items()}
                  for cc in cells if cc in info}
    # pooled enterprises per worker by division, over countries with both counts
    epw_num, epw_den = defaultdict(float), defaultdict(float)
    for cc, n in measured_n.items():
        if emp_ev.get(cc) != "measured":
            continue
        for d, x in n.items():
            if emp[cc].get(d, 0) > 0:
                epw_num[d] += x
                epw_den[d] += emp[cc][d]
    # divisions the statistics leave out (01-03, 84, 94): the registers' ratio to their own level
    reg_ratio = {}
    for d in DRAWN:
        r = []
        for cc in ("FR", "NO"):
            counts = regs[cc]["counts"]
            nd = sum(v for k, v in counts.items() if k.split("|")[1] == d)
            if nd and emp[cc].get(d, 0) > 0:
                cov = [x for x in DRAWN if x in measured_n.get(cc, {})]
                lvl_n = sum(v for k, v in counts.items() if k.split("|")[1] in cov)
                lvl_e = sum(emp[cc][x] for x in cov)
                r.append((nd / emp[cc][d]) / (lvl_n / lvl_e))
        if r:
            reg_ratio[d] = statistics.mean(r)
    total_n = sum(epw_num.values())
    total_e = sum(epw_den[d] for d in epw_num)
    base_level = total_n / total_e
    epw_rel = {}
    for d in DRAWN:
        if epw_den[d] > 0 and epw_num[d] > 0:
            epw_rel[d] = (epw_num[d] / epw_den[d]) / base_level
        elif d in reg_ratio:
            epw_rel[d] = reg_ratio[d]
        else:
            epw_rel[d] = 1.0
    # the country's level: enterprises per worker relative to the pool, where measured
    level = {}
    for cc, n in measured_n.items():
        cov = [d for d in n if emp[cc].get(d, 0) > 0]
        e_rel = sum(emp[cc][d] * epw_rel[d] for d in cov)
        if e_rel > 0:
            level[cc] = sum(n[d] for d in cov) / e_rel
    for cc, rows in census.items():
        tot = next((r for r in rows if r["isic"] == "*" and r["enterprises"]), None)
        if tot and cc not in level and cc in emp:
            e_rel = sum(emp[cc][d] * epw_rel[d] for d in DRAWN)
            if cc == "CN" or cc == "ID":   # the census leaves out farming: compare outside A
                e_rel = sum(emp[cc][d] * epw_rel[d] for d in DRAWN if section_of(d) != "A")
            level[cc] = float(tot["enterprises"]) / e_rel if e_rel > 0 else None
    by_income = defaultdict(list)
    for cc, lv in level.items():
        if lv:
            by_income[income[cc]].append(lv)
    income_level = {k: statistics.median(v) for k, v in by_income.items()}
    world_level = statistics.median([lv for lv in level.values() if lv])

    ent, ent_ev, ent_src = {}, {}, {}
    topup = {}   # cc -> {division: units added to the micro band}
    for cc in countries:
        lv = level.get(cc) or income_level.get(income[cc]) or world_level
        farm = FARM_REGISTERED.get(income[cc], 0.12)
        est = {d: emp[cc][d] * epw_rel[d] * lv * (farm if section_of(d) == "A" else 1.0) for d in DRAWN}
        if cc in measured_n:
            out = dict(est)
            out.update({d: x for d, x in measured_n[cc].items() if d in DRAWN})
            ent[cc] = out
            ent_ev[cc], ent_src[cc] = "measured", cells[cc]["source"]
            tot = next((r for r in census.get(cc, []) if r["isic"] == "*" and r["enterprises"]), None)
            if tot and float(tot["enterprises"]) > 1.2 * sum(out.values()):
                # the statistics leave out the smallest units the register holds (Brazil's MEI):
                # the difference is added to the micro band, spread as the measured micro firms
                extra = float(tot["enterprises"]) - sum(out.values())
                micro = {d: c["micro"][0] for d, c in cells[cc]["divisions"].items() if d in DRAWN}
                ms = sum(micro.values())
                topup[cc] = {d: extra * x / ms for d, x in micro.items()}
                for d, x in topup[cc].items():
                    out[d] += x
                ent_src[cc] += f"; {tot['source']} for the units it leaves out"
        elif cc in census and any(r["isic"] != "*" and r["enterprises"] for r in census[cc]):
            out = dict(est)
            for r in census[cc]:
                if r["isic"] == "*" or not r["enterprises"]:
                    continue
                ds = expand_divisions(r["isic"])
                w = {d: emp[cc][d] * epw_rel[d] for d in ds}
                ws = sum(w.values()) or len(ds)
                for d in ds:
                    out[d] = float(r["enterprises"]) * (w[d] / ws if sum(w.values()) else 1 / len(ds))
            ent[cc] = out
            ent_ev[cc], ent_src[cc] = "census", census[cc][0]["source"]
        else:
            ent[cc] = est
            if cc in level:
                ent_ev[cc], ent_src[cc] = "census", next(r["source"] for r in census[cc] if r["isic"] == "*")
            else:
                ent_ev[cc] = "employment" if emp_ev[cc] != "filled" else "filled"
                ent_src[cc] = (f"{emp_src[cc]} x enterprises per worker (pooled measured countries; "
                               f"level of the {income[cc] or 'world'} group)")
    # census totals the divisions must add up to (countries with a total and no division rows
    # cover everything; China and Indonesia leave farming out, so their rows stand as they are)
    for cc, rows in census.items():
        tot = next((r for r in rows if r["isic"] == "*" and r["enterprises"]), None)
        if tot and cc in ent and cc not in measured_n and not any(r["isic"] != "*" for r in rows):
            s = sum(ent[cc].values())
            k = float(tot["enterprises"]) / s if s > 0 else 1.0
            ent[cc] = {d: x * k for d, x in ent[cc].items()}

    # ---------------------------------------------------------------- sizes
    pooled = defaultdict(lambda: {b: [0.0, 0.0] for b in BANDS})
    for cc, v in cells.items():
        if cc not in info:
            continue
        tot = sum(sum(b[0] for b in cell.values()) for cell in v["divisions"].values())
        for d, cell in v["divisions"].items():
            for b in BANDS:
                # each country counts by its share, so the US does not outweigh the rest
                pooled[d][b][0] += cell[b][0] / tot
                pooled[d][b][1] += (cell[b][1] or 0.0) / tot
    size_rows = []
    size_ev = {}

    def size_row(cc, key, cell, ev):
        e = sum(cell[b][0] for b in BANDS)
        p = sum((cell[b][1] or 0.0) for b in BANDS)
        if e <= 0:
            return
        for b in BANDS:
            n, w = cell[b][0], (cell[b][1] or 0.0)
            mean = w / n if n > 0 and w > 0 else None
            size_rows.append({"country_code": cc, "isic": key, "band": b, "enterprise_share": n / e,
                              "employment_share": (w / p) if p > 0 else None, "mean_persons": mean, "evidence": ev})

    for d in DRAWN:
        cell = pooled.get(d)
        if cell is None:   # 01-03, 84, 94: the section's pool, else the whole economy's
            ds = [x for x in pooled if section_of(x) == section_of(d)] or list(pooled)
            cell = {b: [sum(pooled[x][b][0] for x in ds), sum(pooled[x][b][1] for x in ds)] for b in BANDS}
        size_row("*", d, cell, "filled")
    for cc, v in sorted(cells.items()):
        if cc not in info:
            continue
        for d in DRAWN:
            if d in v["divisions"]:
                cell = {b: list(x) for b, x in v["divisions"][d].items()}
                add = topup.get(cc, {}).get(d, 0.0)
                cell["micro"][0] += add
                cell["micro"][1] = (cell["micro"][1] or 0.0) + add
                size_row(cc, d, cell, "measured")
        size_ev[cc] = ("measured", v["source"])
    for cc, v in sorted(st["section_cells"].items()):
        if cc not in info:
            continue
        for s, cell in v["sections"].items():
            if s not in SECTIONS or s in "TU":
                continue
            ents, emps = cell["enterprises"], cell["employment"]
            if any(ents.get(b) is None for b in BANDS):
                continue
            c = {b: [ents[b], emps.get(b)] for b in BANDS}
            size_row(cc, s, c, "measured_section")
        size_ev[cc] = ("measured_section", v["source"])

    # ---------------------------------------------------------------- occupations
    occ_rows = []
    occ_ev = {}
    pool_occ = defaultdict(float)
    for cc, v in st["ilo_occ"].items():
        if cc not in info:
            continue
        tot = sum(x for k, x in v["values"].items() if k.split("|")[1] not in "TU")
        for k, x in sorted(v["values"].items()):
            isco, s = k.split("|")
            if s in "TU" or x <= 0:
                continue
            occ_rows.append({"country_code": cc, "isco": isco, "section": s, "employment": x})
            pool_occ[(isco, s)] += x / tot
        occ_ev[cc] = ("measured", v["source"])
    for (isco, s), x in sorted(pool_occ.items()):
        occ_rows.append({"country_code": "*", "isco": isco, "section": s, "employment": x * 1e6})
    occ_rows.sort(key=lambda r: (r["country_code"], r["isco"], r["section"]))

    # ---------------------------------------------------------------- ages
    age_rows = []
    us_age = st["us_age"]
    shape = {}
    for s in SECTIONS:
        if s in "TU":
            continue
        for b in BANDS:
            row = us_age.get(f"{s}|{b}")
            if not row or sum(row.values()) < 50:   # O and thin cells: the band over every sector
                row = defaultdict(float)
                for key, r in us_age.items():
                    if key.endswith("|" + b):
                        for a, x in r.items():
                            row[a] += x
            tot = sum(row.values())
            shape[(s, b)] = {a: row.get(a, 0.0) / tot for a in AGE_CLASSES}
            for a in AGE_CLASSES:
                age_rows.append({"country_code": "*", "section": s, "band": b, "age": a, "share": shape[(s, b)][a]})
    for cc, reg in regs.items():
        acc = defaultdict(float)
        for k, n in reg["counts"].items():
            form, d, b, a = k.split("|")
            if a == "*" or d == "*" or d not in DRAWN:
                continue
            acc[(section_of(d), b, a)] += n
        keys = {(s, b) for s, b, _ in acc}
        for s, b in sorted(keys):
            tot = sum(acc[(s, b, a)] for a in AGE_CLASSES)
            if tot < 200:
                continue
            for a in AGE_CLASSES:
                age_rows.append({"country_code": cc, "section": s, "band": b, "age": a, "share": acc[(s, b, a)] / tot})
    # the share of firms up to five years old, by country
    us_young = sum(us_age[k][a] for k in us_age for a in YOUNG) / sum(sum(v.values()) for v in us_age.values())
    young = {}
    for cc, v in st["young"].items():
        if cc in info:
            young[cc] = (sum(v["classes"]["TOTAL"]), v["source"])
    young["US"] = (us_young, "Census BDS 2023 (employer firms)")

    return {"industries": (emp, emp_ev, emp_src, ent, ent_ev, ent_src),
            "sizes": (size_rows, size_ev), "occupations": (occ_rows, occ_ev), "ages": (age_rows, young),
            "topup": topup, "levels": {"pool": base_level, "level": level, "income": income_level, "world": world_level,
                       "epw_rel": epw_rel}}
