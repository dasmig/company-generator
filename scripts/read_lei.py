#!/usr/bin/env python3
"""LEI issuers by country, from GLEIF's golden copy (CC0).

An LEI's first four characters name the Local Operating Unit that issued it (ISO 17442). For each
legal-address country: how many issued, active LEIs each prefix holds, so a drawn LEI carries a prefix
of an LOU that serves the country, in proportion. Also every prefix the file holds in any status (lapsed
and retired LEIs included), so the safe prefix can be one no LOU has used. Only these counts are kept.

Writes raw_data/work/lei.json.
"""
import csv
import io
import sys
import zipfile
from collections import Counter, defaultdict

from companygen_data import RAW, save_work


def main():
    sys.stdout.reconfigure(encoding="utf-8")
    z = zipfile.ZipFile(RAW / "gleif" / "lei2_golden_copy.csv.zip")
    name = z.namelist()[0]
    by_country = defaultdict(Counter)
    every = Counter()
    with io.TextIOWrapper(z.open(name), encoding="utf-8", newline="") as f:
        rows = csv.reader(f)
        h = next(rows)
        i_lei, i_cc = h.index("LEI"), h.index("Entity.LegalAddress.Country")
        i_status, i_reg = h.index("Entity.EntityStatus"), h.index("Registration.RegistrationStatus")
        for row in rows:
            prefix = row[i_lei][:4]
            every[prefix] += 1
            if row[i_status] == "ACTIVE" and row[i_reg] == "ISSUED":
                by_country[row[i_cc]][prefix] += 1
    out = {"source": f"GLEIF LEI golden copy {name[:8]}",
           "prefixes": {cc: dict(c.most_common()) for cc, c in sorted(by_country.items())},
           "every": sorted(every)}
    save_work("lei.json", out)
    print(len(every), "prefixes;", sum(sum(c.values()) for c in by_country.values()), "active issued LEIs in",
          len(by_country), "countries")
    for cc in ("US", "DE", "IN", "GB", "BR", "NG"):
        print(cc, by_country[cc].most_common(5))


if __name__ == "__main__":
    main()
