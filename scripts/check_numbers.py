#!/usr/bin/env python3
"""Check the engine's registration numbers against python-stdnum (Milestone 5's exit criterion).

scripts/draw_numbers.cpp draws at least N numbers of every scheme (default 100,000; the LEI pooled
over the countries), with safe mode off and on. This script checks each one:

- safe mode off: python-stdnum accepts the number (for the schemes stdnum lacks, a structure check),
  and it is not marked reserved;
- safe mode on: the number is marked reserved, and by the scheme's safe method stdnum rejects it
  (wrong_check, the EIN's reserved prefixes), its prefix is no LOU's (the LEI), or it lies above the
  highest number issued (above_issued);
- a few encodings: an RFC's date is the founding date, a Japanese corporate number's class is one
  the scheme lists, a Romanian ONRC number's year is the year of entry.

python-stdnum (LGPL) is needed to run this check only; nothing of it is shipped.

Usage: python scripts/check_numbers.py [--n 100000] [--tier lite|full] [--cxx clang++]
Exit status 1 when any check fails.
"""
import argparse
import importlib
import os
import re
import subprocess
import sys
import tempfile
from collections import Counter, defaultdict
from pathlib import Path

from companygen_data import RESOURCES, ROOT, load_work, read_tsv

# The python-stdnum module of each scheme (the survey's column stdnum).
STDNUM = {
    "ar-cuit": "ar.cuit", "at-fn": "at.businessid", "at-uid": "at.uid", "au-abn": "au.abn", "au-acn": "au.acn",
    "be-kbo": "be.vat", "br-cnpj": "br.cnpj", "ca-bn": "ca.bn", "ch-uid": "ch.uid", "cl-rut": "cl.rut",
    "cn-uscc": "cn.uscc", "co-nit": "co.nit", "cz-ico": "cz.dic", "de-hrn": "de.handelsregisternummer",
    "de-ustid": "de.vat", "dk-cvr": "dk.cvr", "eg-tn": "eg.tn", "es-nif-entity": "es.nif", "fi-ytunnus": "fi.ytunnus",
    "fr-siren": "fr.siren", "fr-siret": "fr.siret", "fr-tva": "fr.tva", "gb-vat": "gb.vat", "gr-afm": "gr.vat",
    "hu-adoszam": "hu.anum", "id-npwp": "id.npwp", "ie-vat": "ie.vat", "il-hp": "il.hp", "in-gstin": "in_.gstin",
    "in-pan": "in_.pan", "it-piva": "it.iva", "jp-cn": "jp.cn", "ke-pin": "ke.pin", "kr-brn": "kr.brn",
    "mx-rfc-moral": "mx.rfc", "nl-btw": "nl.btw", "nl-rsin": "nl.bsn", "no-orgnr": "no.orgnr", "nz-nzbn": "ean",
    "pe-ruc": "pe.ruc", "pl-nip": "pl.nip", "pl-regon": "pl.regon", "pt-nipc": "pt.nif", "ro-cui": "ro.cui",
    "ro-onrc": "ro.onrc", "ru-inn": "ru.inn", "ru-ogrn": "ru.ogrn", "se-orgnr": "se.orgnr", "sg-uen": "sg.uen",
    "th-juristic": "th.moa", "tr-vkn": "tr.vkn", "tw-ubn": "tw.ubn", "ua-edrpou": "ua.edrpou", "us-ein": "us.ein",
    "vn-mst": "vn.mst", "lei": "lei",
}
# The structure of the schemes python-stdnum lacks.
STRUCTURE = {
    "gb-crn": r"^(\d{8}|[A-Z]{2}\d{6})$",
    "ie-cro": r"^\d{1,6}$",
    "in-cin": r"^[LU]\d{5}[A-Z]{2}\d{4}[A-Z]{3}\d{6}$",
    "nl-kvk": r"^\d{8}$",
    "ng-rc": r"^(RC|BN|IT) \d{1,7}$",
    "pl-krs": r"^\d{10}$",
    "za-cipc": r"^\d{4}/\d{6}/\d{2}$",
}


def build(cxx, binary):
    cmd = [cxx, "-std=c++23", "-O2", "-DNDEBUG", "-Wno-c2y-extensions", "-I", str(ROOT),
           str(ROOT / "scripts" / "draw_numbers.cpp"), "-o", str(binary)]
    if os.name == "nt" and "g++" in Path(cxx).name:
        cmd.append("-static")
    print(" ".join(cmd))
    subprocess.run(cmd, check=True)


def validator(scheme):
    if scheme in STRUCTURE:
        pattern = re.compile(STRUCTURE[scheme])
        return lambda value, compact: bool(pattern.match(value))
    module = importlib.import_module("stdnum." + STDNUM[scheme])
    if scheme == "mx-rfc-moral":
        return lambda value, compact: _try(lambda: module.validate(compact, validate_check_digits=True))
    if scheme == "hu-adoszam":
        return lambda value, compact: _try(lambda: module.validate(compact[:8]))
    if scheme == "nz-nzbn":
        return lambda value, compact: compact.startswith("94") and len(compact) == 13 and module.is_valid(compact)
    if scheme == "kr-brn":
        return lambda value, compact: _try(lambda: module.validate(value)) and kr_brn_valid(compact)
    if scheme == "de-hrn":
        return lambda value, compact: _try(lambda: module.validate(compact))
    return lambda value, compact: _try(lambda: module.validate(value))


def kr_brn_valid(compact):
    """python-stdnum checks a Korean business registration number's structure only; its check digit
    (weights 1 3 7 1 3 7 1 3 5, plus the ninth digit x 5 // 10) is checked here."""
    digits = [int(c) for c in compact]
    s = sum(w * d for w, d in zip((1, 3, 7, 1, 3, 7, 1, 3, 5), digits)) + digits[8] * 5 // 10
    return (10 - s % 10) % 10 == digits[9]


def _try(f):
    try:
        f()
        return True
    except Exception:  # noqa: BLE001 - any stdnum ValidationError (or a crash on a malformed value)
        return False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--n", type=int, default=100000)
    ap.add_argument("--tier", choices=("lite", "full"), default="full")
    ap.add_argument("--cxx", default=os.environ.get("CXX", "clang++"))
    ap.add_argument("--seed", type=int, default=20261111)
    args = ap.parse_args()
    sys.stdout.reconfigure(encoding="utf-8")

    schemes = {r["key"]: r for r in read_tsv(RESOURCES / args.tier / "schemes.tsv")}
    highest = {}
    for r in read_tsv(RESOURCES / args.tier / "anchors.tsv"):
        if r["year"] == "highest":
            highest[(r["scheme"], r["series"])] = int(float(r["q999"]))
    lous = set(load_work("lei.json")["every"]) if (Path(ROOT / "raw_data" / "work" / "lei.json")).exists() else set()
    jp_classes = set(re.findall(r":(\d\d)", dict(p.split("=", 1) for p in schemes["jp-cn"]["params"].split(";"))["class"]))

    work = Path(tempfile.mkdtemp(prefix="company-numbers-"))
    binary = work / ("draw_numbers.exe" if os.name == "nt" else "draw_numbers")
    build(args.cxx, binary)
    proc = subprocess.Popen([str(binary), str(RESOURCES / args.tier), str(args.n), str(args.seed)],
                            stdout=subprocess.PIPE, text=True, encoding="utf-8")
    checks = {}
    count = Counter()
    bad = defaultdict(list)

    def fail(scheme, safe, why, line):
        bad[(scheme, safe)].append(f"{why}: {line}")

    for line in proc.stdout:
        line = line.rstrip("\n")
        scheme, value, compact, safe, reserved, cc, form, founded, name = line.split("\t")
        safe = safe == "1"
        count[(scheme, safe)] += 1
        if scheme not in checks:
            checks[scheme] = validator(scheme)
        valid = checks[scheme](value, compact)
        method = schemes[scheme]["safe_method"]
        if not safe:
            if not valid:
                fail(scheme, safe, "rejected", line)
            if reserved == "1":
                fail(scheme, safe, "reserved outside safe mode", line)
        else:
            if reserved != "1":
                fail(scheme, safe, "not reserved in safe mode", line)
            if method in ("wrong_check",) and valid:
                fail(scheme, safe, "safe number accepted", line)
            if scheme == "us-ein" and valid:
                fail(scheme, safe, "safe EIN accepted", line)
            if scheme == "lei" and (not value.startswith("ZZZZ") or "ZZZZ" in lous):
                fail(scheme, safe, "safe LEI prefix", line)
            if method == "above_issued":
                if scheme == "gb-crn":
                    series = value[:2] if value[:2].isalpha() else "-"
                    top = highest.get(("gb-crn", series), 0)
                    if int(value[2:] if series != "-" else value) <= top:
                        fail(scheme, safe, f"not above the highest {top}", line)
                else:
                    lo = int(dict(p.split("=", 1) for p in schemes[scheme]["params"].split(";"))["safe"].split("-")[0])
                    number = int(re.sub(r"\D", "", value.split("/")[1] if scheme == "za-cipc" else value))
                    if number < lo:
                        fail(scheme, safe, f"below the safe range {lo}", line)
                if not valid:
                    fail(scheme, safe, "safe number malformed", line)
        if scheme == "mx-rfc-moral" and compact[3:9] != founded[2:4] + founded[5:7] + founded[8:10]:
            fail(scheme, safe, "RFC date is not the founding date", line)
        if scheme == "jp-cn" and compact[5:7] not in jp_classes:
            fail(scheme, safe, "class not listed", line)
    if proc.wait() != 0:
        print("draw_numbers failed")
        return 1

    failures = 0
    for scheme in sorted(set(s for s, _ in count) | set(schemes)):
        for safe in (False, True):
            n = count[(scheme, safe)]
            problems = bad[(scheme, safe)]
            method = schemes.get(scheme, {}).get("safe_method", "")
            expected = not (safe and method == "none")
            short = expected and n < args.n
            status = "FAIL" if problems or short else "ok"
            if problems or short:
                failures += 1
            print(f"{status:4} {scheme:14} {'safe' if safe else 'real':4} {n:7} drawn, {len(problems)} failed"
                  + (f" (fewer than {args.n})" if short else ""))
            for p in problems[:3]:
                print("       ", p)
    print(f"{failures} failures")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
