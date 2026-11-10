"""Legal forms: the group shares, the group odds by size, and the forms within each group.

The model (design section 5.4): P(group | country, section, band) is proportional to
base(country, section, group) x odds(group, section, band). The odds come from the two registers
that know each unit's size (France's SIRENE and Norway's Enhetsregisteret). The base is fitted so
that, over the country's own mix of size bands, the groups add up to the shares measured or
curated for the country (an iterative proportional fit).

Where the group shares are known by section (the registers, Eurostat's bd_l_form, the US
nonemployer statistics) the country has a base row per section. Elsewhere only an overall split is
known (a census, or the income group's curated default); the country then has one row of
multipliers (section "*") applied to the pooled base of each section ("*" rows), fitted so its
overall split comes out.

Within a group, a form's share comes from a register where one is open, else from GLEIF's LEI
counts, damped towards small firms for forms meant for large ones (decision 3), else from
curated sets (form_templates.tsv for the countries ISO 20275 does not list, pseudo_forms.tsv
for sole traders and partnerships it leaves out).
"""
import re
import sys
from collections import Counter, defaultdict

from companygen_data import (BANDS, CURATED, GROUPS, SECTIONS, load_work, read_tsv, section_of)
from elf_groups import by_country, read_elf

SECTS = [s for s in SECTIONS if s not in "TU"]
SMALL, LARGE = ("micro", "small"), ("medium", "large")
# overall splits by income group where nothing is measured (curated; World Bank Enterprise and
# informality literature put sole traders at 80-90% of registered firms in low-income countries)
INCOME_SPLIT = {
    "Low income": {"sole_proprietorship": 0.85, "partnership": 0.03, "limited_liability": 0.09, "other": 0.03},
    "Lower middle income": {"sole_proprietorship": 0.75, "partnership": 0.04, "limited_liability": 0.17, "other": 0.04},
    "Upper middle income": {"sole_proprietorship": 0.60, "partnership": 0.04, "limited_liability": 0.31, "other": 0.05},
    "High income": {"sole_proprietorship": 0.50, "partnership": 0.06, "limited_liability": 0.38, "other": 0.06},
    None: {"sole_proprietorship": 0.65, "partnership": 0.04, "limited_liability": 0.26, "other": 0.05},
}
# forms meant for large or listed firms: their LEI share overstates them among small firms
PUBLIC = re.compile(r"public|publiczn|anonym|anónima|anônima|anonima|aktiengesellschaft|aktieselskab|"
                    r"allmennaksje|akciová|akcyjna|joint[- ]stock|per azioni|naamloze|részvény|julkinen|"
                    r"публичн|открыт|\bplc\b|\bag\b|\bsa\b|\bnv\b|\bs\.a\.|\basa\b|\boyj\b|\bnyrt\b|\bpjsc\b|"
                    r"aktiebolag \(publ\)|societas europaea|europeisk selskap|europäische", re.I)
# names that mark a form for small or closely held firms even though they match PUBLIC
PRIVATE = re.compile(r"cerrad|simplifi|privat|closed|close corporation|непублич|nepublich|zártkörű|\bzrt\b|"
                     r"unipersonal|unipessoal", re.I)
DAMP = 0.15
# sections and divisions a form is kept to ("only") or kept out of ("not"), by its names
RESERVED = [
    (re.compile(r"\bbank|banca|banco|sparkasse|sparebank|sparbank|caisse d'épargne|caisse de crédit|"
                r"credit union|kreditinstitut|crédit|savings", re.I), "K", ""),
    (re.compile(r"insurance|assurance|versicherung|forsikring|försäkring|seguros|assicurazion|"
                r"mutuelle|mutual|gjensidig", re.I), "K", ""),
    (re.compile(r"agricol|agrar|agrícola|agraria|landwirtschaft|\bfarm|фермер|fermersk|exploitation agricole|gaec|"
                r"groupement foncier|groupement forestier|forestier|\bcuma\b|ziraat|rural|"
                r"producción rural|agricultural", re.I), "A", ""),
    (re.compile(r"immobili|real estate|borettslag|housing|bostadsrätt|construction-vente|"
                r"attribution|copropriété", re.I), "L,F", ""),
    (re.compile(r"exercice libéral|\bsel[a-z]*\b|professionnelle|professional|partnerschaftsgesellschaft|"
                r"advoka|avocat|lawyer|notari|médic|steuerberat|wirtschaftsprüf|sociedad profesional|"
                r"profesional|participations financières de profession", re.I), "M,Q", ""),
    (re.compile(r"commune|municipal|kommun|county|fylke|département|région\b|ministr|government|"
                r"state body|public body|public institution|établissement public|public-law|"
                r"public administrative|administration|statutory|budgetary|institución autónoma|"
                r"organismo|local government|province|state administration", re.I), "O,P,Q,R,E,H", ""),
    (re.compile(r"associa|verein|förening|forening|stowarzysz|vereniging|udruga|društvo, zveza|"
                r"foundation|stiftung|stiftelse|fondation|fundaci|fundação|stichting|zaklada|"
                r"charit|non-?profit|not for profit|guarantee|\bngo\b|church|religio|paroisse|"
                r"congrégation|trade union|syndicat|sindicat|union\b|ordre professionnel|"
                r"mittetulundus|incorporated society", re.I), "P,Q,R,S,M,N,I", ""),
]
GROUP_NOT = {"sole_proprietorship": "64,65", "partnership": "65"}


# ------------------------------------------------------------------ the forms of each country

def norm(text):
    return re.sub(r"\bnon profit\b", "nonprofit", re.sub(r"[^\w]+", " ", text.lower()).strip())


def lei_counts():
    out = defaultdict(Counter)
    for cc, c in load_work("registers_gleif.json")["counts"].items():
        for k, n in c.items():
            out[cc][k.split("|")[0]] += n
    return out


def pick_name(e, language):
    """The form's name and abbreviation in the language names are written in, else the first."""
    i = e["languages"].index(language) if language in e["languages"] else 0
    own, own_latin = e["abbr_by_language"][i], e["latin_abbr_by_language"][i]
    abbr = own[0] if own else ""
    latin = own_latin[0] if own_latin else abbr
    return e["names"][i], e["latin_names"][i], abbr, latin


def country_forms(elf, leis, info, name_lang):
    """{cc: [form]} from ISO 20275, forms listed per state or province merged by name."""
    out = {}
    for cc, forms in by_country(elf).items():
        if cc not in info:
            continue
        merged = {}
        for e in forms:
            group = e["group"] or "other"
            if group in ("drop", "fund"):
                continue
            name, latin_name, abbr, latin_abbr = pick_name(e, name_lang.get(cc, ""))
            key = e["code"] if not e["subdivision"] else ("sub", norm(latin_name or name))
            n = leis[cc][e["code"]]
            f = merged.get(key)
            if f is None:
                merged[key] = f = {"code": e["code"], "group": group, "name": name, "latin_name": latin_name,
                                   "abbreviation": abbr, "latin_abbreviation": latin_abbr, "lei": 0, "best": -1,
                                   "names": set(), "evidence": "gleif"}
            f["lei"] += n
            f["names"].update(x for x in e["names"] + e["latin_names"] + e["abbreviations"] if x)
            if n > f["best"]:   # the state's form with the most LEIs stands for all
                f["best"] = n
                f.update(code=e["code"], name=name, latin_name=latin_name, abbreviation=abbr,
                         latin_abbreviation=latin_abbr, group=group)
        out[cc] = list(merged.values())
    return out


def template_forms(info):
    tmpl = defaultdict(list)
    for r in read_tsv(CURATED / "form_templates.tsv"):
        tmpl[r["template"]].append(r)
    out = {}
    for r in read_tsv(CURATED / "form_template_countries.tsv"):
        if r["country"] not in info:
            continue
        out[r["country"]] = [{
            "code": "", "group": t["group"], "name": t["local_name"], "latin_name": t["english_name"],
            "abbreviation": t["abbreviation"], "latin_abbreviation": t["latin_abbreviation"],
            "names": {t["local_name"], t["english_name"]}, "lei": 0, "evidence": "curated",
            "share_small": float(t["share_small"]), "share_large": float(t["share_large"]),
            "public": t["public"] == "1"} for t in tmpl[r["template"]]]
    return out


def add_pseudo(forms):
    for r in read_tsv(CURATED / "pseudo_forms.tsv"):
        forms.setdefault(r["country"], []).append({
            "code": "", "group": r["group"], "name": r["local_name"], "latin_name": r["english_name"],
            "abbreviation": r["abbreviation"], "latin_abbreviation": r["latin_abbreviation"],
            "names": {r["local_name"], r["english_name"]}, "lei": 0, "evidence": "curated",
            "pseudo": True, "share": float(r["share"]) if r["share"] else None})


# ------------------------------------------------------------------ registers

def register_map(country):
    rows = [r for r in read_tsv(CURATED / "register_forms.tsv") if r["country"] == country]

    def look(form):
        best = None
        for r in rows:
            f = r["form"]
            if f == form or (f.endswith("*") and form.startswith(f[:-1])):
                if best is None or (best["form"] != form and (f == form or len(f) > len(best["form"]))):
                    best = r
        if best is None:
            raise ValueError(f"register_forms.tsv: no row for {country} form {form!r}")
        return best["code"], best["group"]
    return look


def read_register(cc):
    """Counts by (group, section, band) and by (code, group, size class), drops removed."""
    look = register_map(cc)
    reg = load_work(f"registers_{cc.lower()}.json")
    by_cell, by_form = Counter(), Counter()
    for k, n in reg["counts"].items():
        form, d, b, a = k.split("|")
        code, group = look(form)
        if group == "drop":
            continue
        s = section_of(d) if d != "*" else "*"
        if s in ("T", "U"):
            continue
        by_cell[(group, s, b)] += n
        size = "*" if b == "*" else ("small" if b in SMALL else "large")
        by_form[(code, group, size)] += n
    return by_cell, by_form


# ------------------------------------------------------------------ fitting

def mix(base, odds, bands, s):
    """P(group) over the band mix bands {b: share} given base {g: x} and odds."""
    out = Counter()
    for b, pb in bands.items():
        w = {g: base.get(g, 0.0) * odds_of(odds, g, s, b) for g in GROUPS}
        t = sum(w.values())
        if t > 0:
            for g in GROUPS:
                out[g] += pb * w[g] / t
    return out


def odds_of(odds, g, s, b):
    return odds.get((g, s, b), odds.get((g, "*", b), 0.0))   # no register counts the group in the band


def fit_section(target, odds, bands, s, rounds=200):
    """The base whose band mixture gives target {g: share}."""
    base = {g: target.get(g, 0.0) for g in GROUPS}
    for _ in range(rounds):
        got = mix(base, odds, bands, s)
        for g in GROUPS:
            if base[g] > 0 and got[g] > 0:
                base[g] *= target[g] / got[g]
    t = sum(base.values())
    return {g: base[g] / t for g in GROUPS} if t > 0 else base


def scaled_base(pooled, lam, band):
    """A country's base from the pooled one: its multipliers apply to micro and small firms; medium
    and large firms take the pooled base in every group the country has. A country's overall split
    is nearly all micro firms, and large firms' legal forms differ far less between countries (an
    overall split with as many partnerships as companies would otherwise make half of India's
    large manufacturers partnerships)."""
    if band in SMALL:
        return {g: pooled[g] * lam[g] for g in GROUPS}
    return {g: pooled[g] if lam[g] > 0 else 0.0 for g in GROUPS}


def fit_multipliers(target, pooled_base, odds, band_mix, weights, rounds=400):
    """Per-group multipliers on the pooled base so the country's overall split is target."""
    lam = {g: (1.0 if target.get(g, 0) > 0 else 0.0) for g in GROUPS}
    for _ in range(rounds):
        got = Counter()
        for s, w in weights.items():
            for b, pb in band_mix[s].items():
                for g, x in mix(scaled_base(pooled_base[s], lam, b), odds, {b: pb}, s).items():
                    got[g] += w * x
        tot = sum(got.values())
        for g in GROUPS:
            if lam[g] > 0 and got[g] > 0:
                lam[g] *= target[g] / (got[g] / tot)
        m = max(lam.values())
        lam = {g: x / m for g, x in lam.items()}
    return lam


# ------------------------------------------------------------------ build

def band_mixes(econ, cc, ent):
    """{section: {band: share}} for the country: its own size rows, else the pooled ones."""
    rows = econ["sizes"][0]
    own = defaultdict(dict)
    pooled = defaultdict(dict)
    for r in rows:
        if r["country_code"] == cc:
            own[r["isic"]][r["band"]] = r["enterprise_share"]
        elif r["country_code"] == "*":
            pooled[r["isic"]][r["band"]] = r["enterprise_share"]
    out = {}
    for s in SECTS:
        acc, wt = Counter(), 0.0
        if s in own:
            out[s] = dict(own[s])
            continue
        for d, n in ent.items():
            if section_of(d) != s:
                continue
            cell = own.get(d) or pooled.get(d)
            if not cell:
                continue
            for b, x in cell.items():
                acc[b] += n * x
            wt += n
        if wt <= 0:
            ds = [d for d in pooled if section_of(d) == s]
            for d in ds:
                for b, x in pooled[d].items():
                    acc[b] += x
            wt = len(ds) or 1
        out[s] = {b: acc[b] / wt for b in BANDS}
    return out


def build(info, econ):
    st = load_work("stats.json")
    names = {r["country"]: r for r in read_tsv(CURATED / "name_languages.tsv")}
    name_lang = {cc: r["languages"].split(";")[0].split(":")[0] for cc, r in names.items()}
    elf = read_elf()
    leis = lei_counts()
    forms = country_forms(elf, leis, info, name_lang)
    forms.update(template_forms(info))
    add_pseudo(forms)
    apply_overrides(forms)
    ent_all = econ["industries"][3]

    # ---- registers: measured groups by section and band, forms by size class
    regs = {cc: read_register(cc) for cc in ("FR", "NO", "GB")}

    # ---- odds by group, section and band, pooled from the registers that know the band
    odds = {}
    pool = Counter()
    for cc in ("FR", "NO"):
        cells = regs[cc][0]
        sb = Counter()
        for (g, s, b), n in cells.items():
            sb[(s, b)] += n
        for (g, s, b), n in cells.items():
            if sb[(s, b)] > 0:
                pool[(g, s, b)] += n / sb[(s, b)] / 2
                pool[(g, "*", b)] += n / sb[(s, b)] / 2 / len(SECTS)
    for s in SECTS + ["*"]:
        p_gs = {g: sum(pool[(g, s, b)] for b in BANDS) / len(BANDS) for g in GROUPS}
        for g in GROUPS:
            for b in BANDS:
                if p_gs[g] > 0 and pool[(g, s, b)] > 0:
                    odds[(g, s, b)] = pool[(g, s, b)] / p_gs[g]
    # thin cells (a band that is almost empty in a section) take the all-section odds
    for (g, s, b) in list(odds):
        if s != "*" and sum(regs[cc][0][(g, s, b)] for cc in ("FR", "NO")) < 30:
            del odds[(g, s, b)]

    # ---- section targets where measured
    targets, evidence, sources = defaultdict(dict), {}, {}
    for cc in ("FR", "NO"):
        cells = regs[cc][0]
        for s in SECTS:
            t = {g: sum(cells[(g, s, b)] for b in BANDS) for g in GROUPS}
            tot = sum(t.values())
            if tot >= 100:
                targets[cc][s] = {g: x / tot for g, x in t.items()}
        evidence[cc] = "register"
    sources["FR"] = "INSEE SIRENE stock (whole register)"
    sources["NO"] = "Brønnøysund Enhetsregisteret (whole register)"
    # Eurostat: "partnership" there holds partnerships, co-operatives and associations; it is
    # split in the two registers' ratio for the section
    reg_ratio = {}
    for s in SECTS:
        p = sum(targets[cc].get(s, {}).get("partnership", 0) for cc in ("FR", "NO"))
        o = sum(targets[cc].get(s, {}).get("other", 0) for cc in ("FR", "NO"))
        reg_ratio[s] = p / (p + o) if p + o > 0 else 0.5
    for cc, v in st["forms"].items():
        if cc not in info or cc in ("FR", "NO"):
            continue
        for s, cell in v["sections"].items():
            if s not in SECTS:
                continue
            e = cell["enterprises"]
            tot = sum(e.values())
            if tot < 50:
                continue
            bundle = e.get("partnership", 0.0)
            if cc == "US":   # nonemployers: no co-operatives or associations in the count
                t = {"sole_proprietorship": e.get("sole_proprietorship", 0), "partnership": bundle,
                     "limited_liability": e.get("limited_liability", 0), "other": 0.0}
            else:
                t = {"sole_proprietorship": e.get("sole_proprietorship", 0),
                     "partnership": bundle * reg_ratio[s], "other": bundle * (1 - reg_ratio[s]),
                     "limited_liability": e.get("limited_liability", 0)}
            tot = sum(t.values())
            targets[cc][s] = {g: x / tot for g, x in t.items()}
        evidence[cc] = "measured"
        sources[cc] = v["source"]

    # ---- pooled section pattern: every measured country counts once
    pooled_t = {}
    for s in SECTS:
        acc, n = Counter(), 0
        for cc, ts in targets.items():
            if s in ts and cc != "US":
                for g, x in ts[s].items():
                    acc[g] += x
                n += 1
        if n == 0:   # A and O: the registers only
            for cc in ("FR", "NO"):
                for g, x in targets[cc].get(s, {}).items():
                    acc[g] += x
                n += 1
        pooled_t[s] = {g: acc[g] / n for g in GROUPS}
    pooled_ent = Counter()
    for cc in targets:
        for d, x in ent_all.get(cc, {}).items():
            pooled_ent[d] += x / max(sum(ent_all[cc].values()), 1)
    pooled_bands = band_mixes(econ, "*", pooled_ent)
    pooled_base = {s: fit_section(pooled_t[s], odds, pooled_bands[s], s) for s in SECTS}

    # ---- overall splits for everyone else
    splits = {r["country"]: r for r in read_tsv(CURATED / "group_splits.tsv")}
    group_rows, mult_rows = [], []
    for s in SECTS:
        for g in GROUPS:
            group_rows.append({"country_code": "*", "section": s, "group": g, "base": pooled_base[s][g],
                               "evidence": "pooled"})
    for cc in sorted(info):
        have = {f["group"] for f in forms.get(cc, [])}
        ent = ent_all.get(cc, {})
        bands = band_mixes(econ, cc, ent)
        weights = Counter()
        for d, x in ent.items():
            weights[section_of(d)] += x
        tw = sum(weights.values()) or 1.0
        weights = {s: weights[s] / tw for s in SECTS if weights[s] > 0}
        if cc in targets and cc not in splits:   # a curated overall split wins (GB, US)
            for s, t in sorted(targets[cc].items()):
                t = {g: (x if g in have else 0.0) for g, x in t.items()}
                if sum(t.values()) <= 0:
                    continue
                tt = sum(t.values())
                base = fit_section({g: x / tt for g, x in t.items()}, odds, bands[s], s)
                for g in GROUPS:
                    group_rows.append({"country_code": cc, "section": s, "group": g, "base": base[g],
                                       "evidence": evidence[cc]})
            # the sections the source leaves out (A, O, S in Eurostat) take multipliers fitted
            # to the covered sections' overall split
            covered = {s for s in targets[cc]}
            cov_w = {s: w for s, w in weights.items() if s in covered}
            ct = sum(cov_w.values()) or 1.0
            overall = Counter()
            for s, w in cov_w.items():
                for g, x in targets[cc][s].items():
                    overall[g] += w / ct * x
            target = {g: (overall[g] if g in have else 0.0) for g in GROUPS}
            lam = fit_multipliers(target, pooled_base, odds, bands, {s: w / ct for s, w in cov_w.items()})
            ev, src = evidence[cc], sources[cc]
        else:
            if cc in splits:
                r = splits[cc]
                target = {g: float(r[g]) for g in GROUPS}
                ev, src = {"GB": "register", "US": "measured"}.get(cc, "census"), r["source"]
            else:
                target = dict(INCOME_SPLIT.get(info[cc]["income_level"], INCOME_SPLIT[None]))
                ev, src = "curated", f"income-group default ({info[cc]['income_level'] or 'world'})"
                if any(f["evidence"] == "curated" and not f.get("pseudo") for f in forms.get(cc, [])):
                    src += "; forms by legal tradition"
            target = {g: (x if g in have else 0.0) for g, x in target.items()}
            tt = sum(target.values())
            target = {g: x / tt for g, x in target.items()}
            lam = fit_multipliers(target, pooled_base, odds, bands, weights)
        for g in GROUPS:
            mult_rows.append({"country_code": cc, "section": "*", "group": g, "base": lam[g], "evidence": ev})
        evidence[cc], sources[cc] = ev, src
    group_rows += mult_rows

    # ---- forms within each group
    form_rows = []
    reg_forms = {cc: regs[cc][1] for cc in regs}
    dates = read_tsv(CURATED / "form_dates.tsv")
    for cc in sorted(info):
        fl = forms.get(cc, [])
        by_group = defaultdict(list)
        for f in fl:
            by_group[f["group"]].append(f)
        for g, fs in by_group.items():
            shares = within_group(cc, g, fs, reg_forms.get(cc))
            for f, (small, large, ev) in zip(fs, shares):
                if small < 0.002 and large < 0.002:
                    continue
                only, not_ = constraints(f)
                fr, un = form_dates(cc, f, dates)
                form_rows.append({"country_code": cc, "code": f["code"], "group": g,
                                  "abbreviation": f["abbreviation"], "latin_abbreviation": f["latin_abbreviation"],
                                  "name": f["name"], "latin_name": f["latin_name"] if f["latin_name"] != f["name"] else "",
                                  "from": fr, "until": un, "only": only, "not": not_,
                                  "share_small": small, "share_large": large, "evidence": ev})
    # renormalise after the cut
    tot = defaultdict(lambda: [0.0, 0.0])
    for r in form_rows:
        tot[(r["country_code"], r["group"])][0] += r["share_small"]
        tot[(r["country_code"], r["group"])][1] += r["share_large"]
    for r in form_rows:
        t = tot[(r["country_code"], r["group"])]
        r["share_small"] = r["share_small"] / t[0] if t[0] > 0 else 0.0
        r["share_large"] = r["share_large"] / t[1] if t[1] > 0 else 0.0
    form_rows.sort(key=lambda r: (r["country_code"], GROUPS.index(r["group"]), -r["share_small"], r["code"], r["name"]))

    odds_rows = [{"group": g, "section": s, "band": b, "odds": x} for (g, s, b), x in sorted(odds.items())]
    return {"legal_forms": form_rows, "groups": group_rows, "form_odds": odds_rows,
            "evidence": evidence, "sources": sources}


def within_group(cc, g, fs, reg):
    """[(share_small, share_large, evidence)] for the forms of one group.

    Forms with a fixed share (a pseudo form's, or one set in form_overrides.tsv) take it; forms
    matched by one override row split its share by their LEIs. The other forms share what is
    left: the ISO forms together by the register where it counts them, else by LEIs (forms for
    large firms damped among small ones), else by the curated template shares, else equally;
    each pseudo form without a share takes as much as the ISO forms together.
    """
    real = [f for f in fs if not f.get("pseudo") and f.get("share") is None]
    free = [f for f in fs if f.get("pseudo") and f.get("share") is None]
    pools = defaultdict(list)
    for f in fs:
        if f.get("share") is not None:
            pools[f.get("pool", id(f))].append(f)
    shares = {}
    if real:
        counted = reg is not None and any(reg[(f["code"], g, z)] for f in real for z in ("small", "large", "*"))
        if counted:
            sm = [reg[(f["code"], g, "small")] + reg[(f["code"], g, "*")] for f in real]
            lg = [reg[(f["code"], g, "large")] for f in real]
            if sum(lg) == 0:   # the UK register has no sizes: LEIs stand for the large firms
                lg = [f["lei"] for f in real]
            if sum(lg) == 0:
                lg = sm
            ev = ["register" if x > 0 else "gleif" for x in sm]
        elif all("share_small" in f for f in real):
            sm = [f["share_small"] for f in real]
            lg = [f["share_large"] for f in real]
            ev = ["curated"] * len(real)
        elif sum(f["lei"] for f in real) > 0:
            lg = [f["lei"] for f in real]
            big = [is_public(f) for f in real]
            if all(big[i] for i in range(len(real)) if lg[i] > 0):   # nothing to damp against
                big = [False] * len(real)
            sm = [x * (DAMP if b else 1.0) for x, b in zip(lg, big)]
            ev = ["gleif"] * len(real)
        else:
            sm = lg = [1.0] * len(real)
            ev = ["gleif" if f["code"] else "curated" for f in real]
        ts, tl = sum(sm) or 1.0, sum(lg) or 1.0
        for f, a, b, e in zip(real, sm, lg, ev):
            shares[id(f)] = (a / ts, b / tl, e)
    left = max(0.0, 1.0 - sum(p[0]["share"] for p in pools.values()))
    parts = len(free) + (1 if real else 0)
    each = left / parts if parts else 0.0
    for f in free:
        shares[id(f)] = (each, each, "curated")
    for f in real:
        a, b, e = shares[id(f)]
        shares[id(f)] = (a * each, b * each, e)
    for p in pools.values():
        w = [f["lei"] for f in p]
        if sum(w) == 0:
            w = [1] * len(p)
        for f, x in zip(p, w):
            k = p[0]["share"] * x / sum(w)
            shares[id(f)] = (k, k, "curated")
    return [shares[id(f)] for f in fs]


def is_public(f):
    """A form for large firms, by its own name (not by every name the list gives it: Germany's KG
    lists "AG & Co. KG")."""
    own = [f["name"], f["latin_name"], f["abbreviation"]]
    return any(PUBLIC.search(n) for n in own if n) and not any(PRIVATE.search(n) for n in own if n)


def apply_overrides(forms):
    """form_overrides.tsv: names, abbreviations and fixed shares of ISO forms."""
    for k, r in enumerate(read_tsv(CURATED / "form_overrides.tsv")):
        if not any(r[c] for c in ("abbreviation", "latin_abbreviation", "name", "latin_name", "share")):
            raise ValueError(f"form_overrides.tsv: the {r['country']} row for {r['code'] or r['match']!r} sets nothing")
        hit = False
        for f in forms.get(r["country"], []):
            if f.get("pseudo"):
                continue
            if r["code"]:
                if f["code"] != r["code"]:
                    continue
            elif not any(re.search(r["match"], n, re.I) for n in f["names"]):
                continue
            hit = True
            for col in ("abbreviation", "latin_abbreviation", "name", "latin_name"):
                if r[col]:
                    f[col] = "" if r[col] == "-" else r[col]
            if r["share"]:
                f["share"], f["pool"] = float(r["share"]), k
        if not hit:
            raise ValueError(f"form_overrides.tsv: no {r['country']} form for {r['code'] or r['match']!r}")


def constraints(f):
    text = " | ".join(sorted(f["names"]))
    only = next((o for rx, o, _ in RESERVED if rx.search(text)), "")
    return only, GROUP_NOT.get(f["group"], "")


def form_dates(cc, f, dates):
    for r in dates:
        if r["country"] != cc:
            continue
        if r["code"] and r["code"] == f["code"]:
            return r["from"], r["until"]
        if not r["code"] and r["match"] and any(re.search(r["match"], n, re.I) for n in f["names"]):
            return r["from"], r["until"]
    return "", ""


def main():
    sys.stdout.reconfigure(encoding="utf-8")
    from companygen_data import countries
    import prepare_economy
    info = countries()
    econ = prepare_economy.build(info)
    out = build(info, econ)
    rows = out["legal_forms"]
    print(len(rows), "forms;", len(out["groups"]), "group rows;", len(out["form_odds"]), "odds")
    ev = Counter(out["evidence"].values())
    print("group evidence:", dict(ev))
    for cc in ("FR", "DE", "GB", "US", "IN", "CN", "BR", "NG", "JP", "VN"):
        fs = [r for r in rows if r["country_code"] == cc][:8]
        print(cc, out["evidence"][cc], " | ".join(f"{r['group'][:4]}:{r['abbreviation'] or r['name'][:22]}:"
                                                 f"{r['share_small']:.2f}/{r['share_large']:.2f}" for r in fs))


if __name__ == "__main__":
    main()
