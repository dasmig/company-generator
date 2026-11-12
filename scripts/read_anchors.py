#!/usr/bin/env python3
"""Issue anchors of sequential registration numbers, from the whole registers.

For each scheme and series (the UK's prefixes), and each year: the 0.1, 10, 25, 50, 75, 90 and 99.9%
points of the numbers of the firms registered that year, and their count; and the highest number
issued by the snapshot's date. The draw samples a firm's number from its founding year's points
(design 5.7), and safe numbers start above the highest. Quantiles rather than a line, because the
sequences are not all monotonic: France's SIREN wrapped from 99,999,999 to 10,000,000 in 2025-2026
and old firms were numbered when SIRENE began; Norway issues an 8 and a 9 series side by side. Only these figures are kept, never a register's rows.

Writes raw_data/work/anchors.json. Reads the UK's Basic Company Data, France's SIRENE stock (every
unit, ceased ones too, since their numbers stay issued) and Norway's Enhetsregisteret.
"""
import csv
import datetime
import gzip
import io
import re
import sys
import zipfile
from collections import defaultdict

from companygen_data import RAW, save_work


QUANTILES = (0.001, 0.1, 0.25, 0.5, 0.75, 0.9, 0.999)


def points(values):
    values.sort()
    n = len(values)
    return [values[min(n - 1, int(q * n))] for q in QUANTILES] + [n]


def summarise(by_series, asof):
    last = int(asof[:4])
    out = {}
    for series, years in sorted(by_series.items()):
        out[series] = {"years": {str(y): points(v) for y, v in sorted(years.items()) if len(v) >= 20 and y <= last},
                       "highest": max(x for v in years.values() for x in v)}
    return out


def read_gb():
    z = zipfile.ZipFile(RAW / "registers" / "gb_basic_company_data.zip")
    name = z.namelist()[0]
    asof = re.search(r"(\d{4}-\d{2}-\d{2})", name).group(1)
    by = defaultdict(lambda: defaultdict(list))
    with io.TextIOWrapper(z.open(name), encoding="utf-8", newline="") as f:
        rows = csv.reader(f)
        h = [x.strip() for x in next(rows)]
        i_num, i_inc = h.index("CompanyNumber"), h.index("IncorporationDate")
        for row in rows:
            if len(row) < len(h):
                continue
            m = re.fullmatch(r"([A-Z]{2})?(\d{6,8})", row[i_num].strip())
            d = row[i_inc].strip()
            if not m or len(d) != 10:
                continue
            by[m.group(1) or ""][int(d[6:10])].append(int(m.group(2)))
    return {"asof": asof, "source": f"Companies House basic company data {asof} (live companies)",
            "series": summarise(by, asof)}


def read_fr():
    path = RAW / "registers" / "fr_stock_unite_legale.zip"
    z = zipfile.ZipFile(path)
    name = z.namelist()[0]
    asof = datetime.date.fromtimestamp(path.stat().st_mtime).isoformat()
    by = defaultdict(lambda: defaultdict(list))
    with io.TextIOWrapper(z.open(name), encoding="utf-8", newline="") as f:
        rows = csv.reader(f)
        h = next(rows)
        i_siren, i_date = h.index("siren"), h.index("dateCreationUniteLegale")
        for row in rows:
            s, d = row[i_siren], row[i_date]
            if len(s) != 9 or len(d) < 4 or not s.isdigit():
                continue
            by[""][int(d[:4])].append(int(s[:8]))   # sole traders and legal persons: one sequence
    return {"asof": asof, "source": f"INSEE SIRENE stock unités légales {asof} (every unit)",
            "series": summarise(by, asof)}


def read_no():
    path = RAW / "registers" / "no_enheter.csv.gz"
    asof = datetime.date.fromtimestamp(path.stat().st_mtime).isoformat()
    by = defaultdict(lambda: defaultdict(list))
    with io.TextIOWrapper(gzip.open(path), encoding="utf-8", newline="") as f:
        rows = csv.reader(f)
        h = next(rows)
        i_num, i_reg = h.index("organisasjonsnummer"), h.index("registreringsdatoenhetsregisteret")
        for row in rows:
            if len(row) < len(h):
                continue
            n, d = row[i_num], row[i_reg]
            if len(n) != 9 or not n.isdigit() or len(d) < 4:
                continue
            by[""][int(d[:4])].append(int(n[:8]))
    return {"asof": asof, "source": f"Brønnøysund Enhetsregisteret {asof} (registration date in the register)",
            "series": summarise(by, asof)}


def main():
    sys.stdout.reconfigure(encoding="utf-8")
    out = {"gb-crn": read_gb(), "no-orgnr": read_no(), "fr-siren": read_fr()}
    save_work("anchors.json", out)
    for scheme, v in out.items():
        for series, s in v["series"].items():
            ys = s["years"]
            last = list(ys.items())[-3:]
            print(scheme, series or "-", "highest", s["highest"], "years", len(ys), last)


if __name__ == "__main__":
    main()
