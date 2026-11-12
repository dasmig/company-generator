#!/usr/bin/env python3
"""Download the raw sources into raw_data/ (git-ignored). Existing files are kept,
so delete a file to refresh it. Exits non-zero if anything could not be fetched.

Usage: fetch_raw.py [--small]   (--small skips the three registers and the LEI golden copy, 3 GB)

Sources (design section 4):
- Eurostat: SBS by size class (sbs_sc_ovw), business demography by size and by legal form
  (bd_size, bd_l_form), LFS employment by occupation and activity (lfsa_eisn2). JSON-stat.
- OECD SDBS: enterprises and persons employed by ISIC Rev. 4 and size class (DF_SDBS_ISIC4).
- US Census Bureau: Business Dynamics Statistics 2023, firms by sector, firm age and firm size;
  Nonemployer Statistics 2022 by NAICS and legal form; the NAICS 2017 to ISIC Rev. 4 concordance.
- ILOSTAT: employment by ISIC division, by section, and by section x ISCO-08 major group.
- GLEIF: the ISO 20275 entity legal forms code list and the LEI level-1 golden copy (CSV).
- Registers: Companies House basic company data (UK), SIRENE StockUniteLegale (France),
  Enhetsregisteret (Norway).
"""
import json
import re
import sys
import time
import urllib.parse
import urllib.request

from companygen_data import RAW

HEADERS = {"User-Agent": "Mozilla/5.0 (dasmig-data-build/1.0)"}
EUROSTAT = "https://ec.europa.eu/eurostat/api/dissemination/statistics/1.0/data/{}?format=JSON&lang=EN{}"
EUROSTAT_TABLES = {  # file name: (table, filter); bd_size is split, whole it is too large
    # enterprises and persons employed by NACE (to divisions) and size class
    "sbs_sc_ovw": ("sbs_sc_ovw", "&indic_sbs=ENT_NR&indic_sbs=EMP_NR&sinceTimePeriod=2021"),
    # active enterprises, births, and survivors by age (1 to 5 years), by employee band
    "bd_size_ent": ("bd_size", "&indic_sbs=ENT_NR&age=TOTAL&sinceTimePeriod=2021"),
    "bd_size_brth": ("bd_size", "&indic_sbs=ENT_BRTH_NR&age=TOTAL&sinceTimePeriod=2021"),
    "bd_size_srvl": ("bd_size", "&indic_sbs=ENT_SRVL_NR&sinceTimePeriod=2021"),
    # active enterprises and persons employed by legal-form group
    "bd_l_form": ("bd_l_form", "&indic_sbs=ENT_NR&indic_sbs=EMP_NR&sinceTimePeriod=2019"),
    # employment by ISCO-08 major group x NACE section
    "lfsa_eisn2": ("lfsa_eisn2", "&sex=T&age=Y_GE15&sinceTimePeriod=2022"),
}
SDBS = ("https://sdmx.oecd.org/public/rest/data/OECD.SDD.TPS,DSD_SDBSBSC_ISIC4@DF_SDBS_ISIC4,1.0/"
        "A..ENTR+EMPE...?startPeriod=2012&format=csvfile")
ILO = "https://rplumber.ilo.org/data/indicator/?id={}&timefrom=2012&type=code&format=.csv{}"
ILO_TABLES = {
    "EMP_TEMP_SEX_EC2_NB_A": "&sex=SEX_T",   # employment by ISIC division
    "EMP_TEMP_SEX_ECO_NB_A": "&sex=SEX_T",   # employment by ISIC section
    "EMP_TEMP_ECO_OCU_NB_A": "",             # employment by section x ISCO-08 major group
}
FILES = {
    "us/bds2023_sec_fa_fz.csv":
        "https://www2.census.gov/programs-surveys/bds/tables/time-series/2023/bds2023_sec_fa_fz.csv",
    "us/nonemp22us.zip":
        "https://www2.census.gov/programs-surveys/nonemployer-statistics/datasets/2022/historical-datasets/nonemp22us.zip",
    "us/naics2017_isic4.xlsx": "https://www.census.gov/naics/concordances/2017_NAICS_to_ISIC_4.xlsx",
    "sdbs/sdbs_isic4.csv": SDBS,
    # ISO 20275 code list v1.6 (2026-02-19); the API's pages leave out part of the list
    "gleif/elf-code-list-v1.6.csv": "https://www.gleif.org/lei-data/code-lists/"
                                    "iso-20275-entity-legal-forms-code-list/2026-02-19-elf-code-list-v1.6.csv",
}
for _t, _q in ILO_TABLES.items():
    FILES[f"ilo/{_t}.csv"] = ILO.format(_t, _q)
for _f, (_t, _q) in EUROSTAT_TABLES.items():
    FILES[f"eurostat/{_f}.json"] = EUROSTAT.format(_t, _q)

LARGE = {}  # resolved at run time: the registers publish dated file names


def resolve_large():
    page = get("https://download.companieshouse.gov.uk/en_output.html").decode("utf-8", "replace")
    name = sorted(set(re.findall(r"BasicCompanyDataAsOneFile-[\d-]+\.zip", page)))[-1]
    LARGE["registers/gb_basic_company_data.zip"] = f"https://download.companieshouse.gov.uk/{name}"
    ds = json.loads(get("https://www.data.gouv.fr/api/1/datasets/"
                        "base-sirene-des-entreprises-et-de-leurs-etablissements-siren-siret/"))
    url = next(r["url"] for r in ds["resources"]
               if r["title"].startswith("Sirene : Fichier StockUniteLegale -") and r["url"].endswith("-csv.zip"))
    LARGE["registers/fr_stock_unite_legale.zip"] = url
    LARGE["registers/no_enheter.csv.gz"] = "https://data.brreg.no/enhetsregisteret/api/enheter/lastned/csv"
    LARGE["gleif/lei2_golden_copy.csv.zip"] = \
        "https://goldencopy.gleif.org/api/v2/golden-copies/publishes/lei2/latest.csv"


def get(url, data=None, attempts=4):
    for attempt in range(attempts):
        try:
            req = urllib.request.Request(url, data=data, headers=HEADERS)
            with urllib.request.urlopen(req, timeout=600) as r:
                return r.read()
        except Exception as e:  # noqa: BLE001 - retried, then reported
            if attempt == attempts - 1:
                raise
            print(f"  retry {url[:90]}: {e}", file=sys.stderr)
            time.sleep(5 * (attempt + 1))


def stream(url, path, attempts=4):
    """Download a large file to path through a .part file."""
    part = path.with_suffix(path.suffix + ".part")
    for attempt in range(attempts):
        try:
            req = urllib.request.Request(url, headers=HEADERS)
            with urllib.request.urlopen(req, timeout=600) as r, open(part, "wb") as f:
                total = 0
                while True:
                    chunk = r.read(1 << 20)
                    if not chunk:
                        break
                    f.write(chunk)
                    total += len(chunk)
            part.replace(path)
            return total
        except Exception as e:  # noqa: BLE001
            if attempt == attempts - 1:
                raise
            print(f"  retry {url[:90]}: {e}", file=sys.stderr)
            time.sleep(10 * (attempt + 1))


def main():
    small = "--small" in sys.argv[1:]
    failed = []
    jobs = dict(FILES)
    if not small:
        resolve_large()
        jobs.update(LARGE)
    for rel, url in jobs.items():
        path = RAW / rel
        if path.exists():
            continue
        path.parent.mkdir(parents=True, exist_ok=True)
        try:
            if rel in LARGE:
                n = stream(url, path)
            else:
                data = get(url)
                path.write_bytes(data)
                n = len(data)
            print(f"{rel}: {n:,} bytes")
        except Exception as e:  # noqa: BLE001
            failed.append(rel)
            print(f"FAILED {rel}: {e}", file=sys.stderr)
    if failed:
        sys.exit(f"{len(failed)} downloads failed: {', '.join(failed)}")


if __name__ == "__main__":
    main()
