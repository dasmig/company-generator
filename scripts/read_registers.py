#!/usr/bin/env python3
"""Reduce the open registers and GLEIF's LEI records to counts in raw_data/work/.

Nothing but counts leaves this script: no name, number or address of any register's entry is
written. Sole traders' names in SIRENE are personal data and are not read at all.

Per register (GB Companies House, FR SIRENE, NO Enhetsregisteret), active units only:
- `counts`: "form|division|band|age class" -> units. The band is "*" where the register holds
  no head count (GB); the division is "*" where it holds no activity code.
- `words`: per division, how many names hold each word, and `all_words` over every name; words
  are lower-cased runs of letters. Only words in 20 or more names are kept.
- `patterns`: per "group|band", how many names fall in each name pattern (design section 5.6),
  by the rules in `classify`.

GLEIF (`registers_gleif.json`): active entities that are not funds, by legal address country:
"ELF code|creation year class" -> entities, and per country the 400 commonest name words.
"""
import csv
import datetime
import gzip
import io
import re
import sys
import zipfile
import zlib
from collections import Counter, defaultdict

from companygen_data import DIVISIONS, RAW, SIBLINGS, age_class, band_of, save_work

WORD = re.compile(r"[^\W\d_]+", re.UNICODE)
KEEP_WORDS = 20
# The legal form as written at the end of names; dropped before words and patterns are read.
FORM_WORDS = {
    "GB": {"ltd", "limited", "llp", "lp", "plc", "cic", "cio", "company", "co", "the", "uk"},
    "FR": {"sarl", "sas", "sasu", "eurl", "sa", "snc", "sci", "scp", "scm", "selarl", "selas", "earl", "gaec",
           "scea", "scop", "sca", "scs", "gie", "et", "de", "la", "le", "les", "du", "des", "d", "l"},
    "NO": {"as", "asa", "ans", "da", "enk", "nuf", "sa", "ba", "og", "i", "av"},
}
JOINERS = {"GB": {"and"}, "FR": {"et"}, "NO": {"og"}}
FAMILY = {"GB": {"sons", "son", "brothers", "bros", "family"}, "FR": {"fils", "freres", "frères", "famille"},
          "NO": {"sønn", "sønner", "brødrene", "familie"}}


def surnames(culture, limit=3000):
    """The commonest surnames of a name-generator culture, lower-cased."""
    path = SIBLINGS / "name-generator" / "resources" / "full" / culture / "sur.names"
    lines = path.read_text(encoding="utf-8").splitlines()[2:]
    rows = [line.split("\t") for line in lines if "\t" in line]
    rows.sort(key=lambda r: -float(r[1]))
    return {r[0].lower() for r in rows[:limit]}


def words_of(name, country):
    drop = FORM_WORDS[country]
    return [w for w in (m.lower() for m in WORD.findall(name)) if w not in drop]


def initials_like(raw_name):
    """A name led by letters that read as initials: 'KMT', 'A.B.C.', 'J & K'."""
    first = raw_name.strip().split(" ")[0] if raw_name.strip() else ""
    if re.fullmatch(r"(?:[A-Z]\.){2,4}", first) or re.fullmatch(r"[A-Z]{2,4}", first):
        return not re.search(r"[AEIOUY]", first[1:]) or "." in first
    return False


def classify(raw_name, words, country, division, trade, family_names):
    """The name pattern of design section 5.6, by simple rules:
    initials, two founders (two surnames around a joiner or '&'), founder (+ trade),
    descriptive (trade words, no surname), coined (one or two words, neither)."""
    if not words:
        return "initials" if initials_like(raw_name) else "other"
    if initials_like(raw_name):
        return "initials"
    has_trade = any(w in trade.get(division, ()) for w in words)
    names = [w for w in words if w in family_names]
    joined = "&" in raw_name or any(w in JOINERS[country] for w in WORD.findall(raw_name.lower()))
    if len(names) >= 2 and joined:
        return "two_founders"
    if names:
        return "founder_trade" if has_trade else "founder"
    if has_trade:
        return "descriptive"
    return "coined" if len(words) <= 2 else "descriptive"


class Register:
    def __init__(self, country, culture):
        self.country = country
        self.counts = Counter()
        self.words = defaultdict(Counter)
        self.all_words = Counter()
        self.patterns = defaultdict(Counter)
        self.family = surnames(culture)
        self.sample = []   # (raw name, words, division, group key) for the second pass, 1 in 8

    def add(self, form, division, band, age, name=None, group_key=None):
        self.counts[f"{form}|{division}|{band}|{age}"] += 1
        if name:
            ws = words_of(name, self.country)
            uniq = set(ws)
            self.all_words.update(uniq)
            if division != "*":
                self.words[division].update(uniq)
            if group_key and len(self.sample) < 2_000_000 and zlib.crc32(name.encode()) % 8 == 0:
                self.sample.append((name, ws, division, group_key))

    def finish(self):
        # trade words of a division: in 30+ of its names and 3+ times as common there as overall
        names_in = {d: max(1, self.counts_names(d)) for d in self.words}
        all_names = max(1, sum(names_in.values()))
        trade = {}
        for d, c in self.words.items():
            trade[d] = {w for w, n in c.items() if n >= 30 and (n / names_in[d]) / (self.all_words[w] / all_names) >= 3}
        for name, ws, d, key in self.sample:
            self.patterns[key][classify(name, ws, self.country, d, trade, self.family)] += 1
        return {"counts": dict(self.counts),
                "words": {d: {w: n for w, n in c.items() if n >= KEEP_WORDS} for d, c in self.words.items()},
                "all_words": {w: n for w, n in self.all_words.items() if n >= KEEP_WORDS},
                "names_by_division": names_in,
                "patterns": {k: dict(v) for k, v in self.patterns.items()}}

    def counts_names(self, d):
        return sum(n for k, n in self.counts.items() if k.split("|")[1] == d)


def years_between(start, end):
    return end.year - start.year - ((end.month, end.day) < (start.month, start.day))


def parse_date(text, fmt):
    try:
        return datetime.datetime.strptime(text, fmt).date()
    except ValueError:
        return None


def division_of(code):
    d = code[:2] if code and code[:2].isdigit() else None
    return d if d in DIVISIONS else "*"


# ------------------------------------------------------------------ GB

GB_GROUP = {
    "Private Limited Company": "limited_liability", "Public Limited Company": "limited_liability",
    "Limited Liability Partnership": "partnership", "Limited Partnership": "partnership",
    "Scottish Partnership": "partnership", "Private Unlimited Company": "other", "Private Unlimited": "other",
    "Community Interest Company": "limited_liability",
}


def read_gb():
    z = zipfile.ZipFile(RAW / "registers" / "gb_basic_company_data.zip")
    name = z.namelist()[0]
    asof = datetime.date.fromisoformat(re.search(r"(\d{4}-\d{2}-\d{2})", name).group(1))
    reg = Register("GB", "british")
    with io.TextIOWrapper(z.open(name), encoding="utf-8", newline="") as f:
        rows = csv.reader(f)
        h = [x.strip() for x in next(rows)]
        i_name, i_cat, i_status = h.index("CompanyName"), h.index("CompanyCategory"), h.index("CompanyStatus")
        i_inc, i_acc, i_sic = h.index("IncorporationDate"), h.index("Accounts.AccountCategory"), h.index("SICCode.SicText_1")
        for row in rows:
            if len(row) < len(h) or not row[i_status].startswith("Active"):
                continue
            if row[i_acc] == "DORMANT" or row[i_sic].startswith("99999"):
                continue
            cat = row[i_cat]
            inc = parse_date(row[i_inc], "%d/%m/%Y")
            age = age_class(years_between(inc, asof)) if inc else "*"
            group = GB_GROUP.get(cat, "other")
            reg.add(cat, division_of(row[i_sic]), "*", age, row[i_name], f"{group}|*")
    out = reg.finish()
    out.update({"asof": asof.isoformat(), "source": f"Companies House basic company data {asof}",
                "note": "active companies, dormant ones left out; no sole traders, no head counts"})
    return out


# ------------------------------------------------------------------ FR

FR_TRANCHE = {"NN": 0, "00": 0, "01": 1, "02": 3, "03": 6, "11": 10, "12": 20, "21": 50, "22": 100, "31": 200,
              "32": 250, "41": 500, "42": 1000, "51": 2000, "52": 5000, "53": 10000}


def fr_group(cat):
    if cat.startswith("1"):
        return "sole_proprietorship"
    if cat.startswith("2") or cat[:2] in ("51", "52", "53") or cat.startswith("65"):
        return "partnership"
    if cat[:2] in ("54", "55", "56", "57", "58"):
        return "limited_liability"
    return "other"


def read_fr():
    z = zipfile.ZipFile(RAW / "registers" / "fr_stock_unite_legale.zip")
    name = z.namelist()[0]
    asof = datetime.date.fromtimestamp((RAW / "registers" / "fr_stock_unite_legale.zip").stat().st_mtime)
    reg = Register("FR", "french")
    with io.TextIOWrapper(z.open(name), encoding="utf-8", newline="") as f:
        rows = csv.reader(f)
        h = next(rows)
        ix = {c: h.index(c) for c in ("etatAdministratifUniteLegale", "dateCreationUniteLegale",
                                      "trancheEffectifsUniteLegale", "categorieJuridiqueUniteLegale",
                                      "activitePrincipaleUniteLegale", "nomenclatureActivitePrincipaleUniteLegale",
                                      "denominationUniteLegale", "denominationUsuelle1UniteLegale")}
        for row in rows:
            if row[ix["etatAdministratifUniteLegale"]] != "A":
                continue
            cat = row[ix["categorieJuridiqueUniteLegale"]]
            if row[ix["nomenclatureActivitePrincipaleUniteLegale"]] == "NAFRev2":
                division = division_of(row[ix["activitePrincipaleUniteLegale"]])
            else:
                division = "*"
            created = parse_date(row[ix["dateCreationUniteLegale"]], "%Y-%m-%d")
            age = age_class(years_between(created, asof)) if created else "*"
            employees = FR_TRANCHE.get(row[ix["trancheEffectifsUniteLegale"]], 0)
            group = fr_group(cat)
            persons = employees + (1 if group == "sole_proprietorship" or employees == 0 else 0)
            band = band_of(max(1, persons))
            if group == "sole_proprietorship":
                # a sole trader's name is a person's name: never read; a trade name only when published
                trade_name = row[ix["denominationUsuelle1UniteLegale"]]
                text = trade_name if trade_name and trade_name != "[ND]" else None
            else:
                text = row[ix["denominationUniteLegale"]]
                text = text if text and text != "[ND]" else None
            reg.add(cat, division, band, age, text, f"{group}|{band}" if text else None)
    out = reg.finish()
    out.update({"asof": asof.isoformat(), "source": f"INSEE SIRENE StockUniteLegale {asof}",
                "note": "active legal units; persons employed are the employee band's lower bound plus the owner "
                        "of a sole proprietorship or of a unit without employees"})
    return out


# ------------------------------------------------------------------ NO

NO_GROUP = {"ENK": "sole_proprietorship", "ANS": "partnership", "DA": "partnership", "KS": "partnership",
            "AS": "limited_liability", "ASA": "limited_liability", "NUF": "other", "SA": "other", "BA": "limited_liability"}


def read_no():
    path = RAW / "registers" / "no_enheter.csv.gz"
    asof = datetime.date.fromtimestamp(path.stat().st_mtime)
    reg = Register("NO", "norwegian")
    with io.TextIOWrapper(gzip.open(path), encoding="utf-8", newline="") as f:
        rows = csv.reader(f)
        h = next(rows)
        ix = {c: h.index(c) for c in ("navn", "organisasjonsform.kode", "naeringskode1.kode", "antallAnsatte",
                                      "stiftelsesdato", "registreringsdatoenhetsregisteret", "konkurs",
                                      "underAvvikling", "underTvangsavviklingEllerTvangsopplosning")}
        for row in rows:
            if len(row) < len(h) or "true" in (row[ix["konkurs"]], row[ix["underAvvikling"]],
                                               row[ix["underTvangsavviklingEllerTvangsopplosning"]]):
                continue
            form = row[ix["organisasjonsform.kode"]]
            created = (parse_date(row[ix["stiftelsesdato"]], "%Y-%m-%d") or
                       parse_date(row[ix["registreringsdatoenhetsregisteret"]], "%Y-%m-%d"))
            age = age_class(years_between(created, asof)) if created else "*"
            try:
                employees = int(row[ix["antallAnsatte"]] or 0)
            except ValueError:
                employees = 0
            group = NO_GROUP.get(form, "other")
            persons = employees + (1 if group == "sole_proprietorship" or employees == 0 else 0)
            band = band_of(max(1, persons))
            text = row[ix["navn"]] if group != "sole_proprietorship" else None
            reg.add(form, division_of(row[ix["naeringskode1.kode"]]), band, age, text,
                    f"{group}|{band}" if text else None)
    out = reg.finish()
    out.update({"asof": asof.isoformat(), "source": f"Brønnøysund Enhetsregisteret {asof}",
                "note": "units not bankrupt or being wound up; persons employed are employees plus the owner of "
                        "a sole proprietorship or of a unit without employees"})
    return out


# ------------------------------------------------------------------ GLEIF

def read_gleif():
    z = zipfile.ZipFile(RAW / "gleif" / "lei2_golden_copy.csv.zip")
    name = z.namelist()[0]
    counts = defaultdict(Counter)
    words = defaultdict(Counter)
    other_forms = defaultdict(Counter)
    with io.TextIOWrapper(z.open(name), encoding="utf-8", newline="") as f:
        rows = csv.reader(f)
        h = next(rows)
        ix = {c: h.index(c) for c in ("Entity.LegalName", "Entity.LegalAddress.Country", "Entity.EntityCategory",
                                      "Entity.LegalForm.EntityLegalFormCode", "Entity.LegalForm.OtherLegalForm",
                                      "Entity.EntityStatus", "Entity.EntityCreationDate",
                                      "Registration.RegistrationStatus")}
        for row in rows:
            if row[ix["Entity.EntityStatus"]] != "ACTIVE" or row[ix["Entity.EntityCategory"]] == "FUND":
                continue
            cc = row[ix["Entity.LegalAddress.Country"]]
            code = row[ix["Entity.LegalForm.EntityLegalFormCode"]]
            if code == "8888":
                other_forms[cc][row[ix["Entity.LegalForm.OtherLegalForm"]].strip().upper()[:60]] += 1
            created = row[ix["Entity.EntityCreationDate"]][:10]
            year = int(created[:4]) if created[:4].isdigit() else None
            if year is None:
                ycls = "*"
            elif year < 1950:
                ycls = "<1950"
            else:
                ycls = f"{year // 10 * 10}s"
            counts[cc][f"{code}|{ycls}"] += 1
            words[cc].update({w.lower() for w in WORD.findall(row[ix["Entity.LegalName"]])})
    return {"source": f"GLEIF LEI golden copy {name[:8]}",
            "counts": {cc: dict(c) for cc, c in counts.items()},
            "words": {cc: dict(c.most_common(400)) for cc, c in words.items()},
            "other_forms": {cc: dict(c.most_common(30)) for cc, c in other_forms.items()}}


def main():
    which = sys.argv[1:] or ["gb", "fr", "no", "gleif"]
    for w in which:
        out = {"gb": read_gb, "fr": read_fr, "no": read_no, "gleif": read_gleif}[w]()
        path = save_work(f"registers_{w}.json", out)
        n = sum(out["counts"].values()) if w != "gleif" else sum(sum(c.values()) for c in out["counts"].values())
        print(f"{w}: {n:,} units -> {path.name}")


if __name__ == "__main__":
    main()
