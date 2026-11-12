"""Shared helpers for company-generator's data build (stdlib only)."""
import csv
import json
from pathlib import Path

csv.field_size_limit(10**9)

ROOT = Path(__file__).resolve().parent.parent
CURATED = ROOT / "scripts" / "curated"
RAW = ROOT / "raw_data"
WORK = RAW / "work"          # intermediate counts written by the read_* scripts
RESOURCES = ROOT / "resources"
SIBLINGS = ROOT.parent
TIERS = ("lite", "full")

BANDS = ("micro", "small", "medium", "large")          # under 10, 10-49, 50-249, 250+
BAND_LIMITS = {"micro": (1, 9), "small": (10, 49), "medium": (50, 249), "large": (250, None)}
GROUPS = ("sole_proprietorship", "partnership", "limited_liability", "other")
# Age classes in years (the US BDS classes): 0, 1, ..., 5, 6-10, 11-15, 16-20, 21-25, 26+.
AGE_CLASSES = ("0", "1", "2", "3", "4", "5", "6-10", "11-15", "16-20", "21-25", "26+")
EVIDENCE_ORDER = ("measured", "register", "census", "employment", "gleif", "curated", "filled", "none")
# Eurostat's geo codes that are not ISO alpha-2.
EUROSTAT_GEO = {"EL": "GR", "UK": "GB", "XK": "XK"}

# ISIC Rev. 4 sections and their divisions (NACE Rev. 2 has the same 21 sections and 88 divisions).
SECTIONS = {
    "A": (1, 3), "B": (5, 9), "C": (10, 33), "D": (35, 35), "E": (36, 39), "F": (41, 43),
    "G": (45, 47), "H": (49, 53), "I": (55, 56), "J": (58, 63), "K": (64, 66), "L": (68, 68),
    "M": (69, 75), "N": (77, 82), "O": (84, 84), "P": (85, 85), "Q": (86, 88), "R": (90, 93),
    "S": (94, 96), "T": (97, 98), "U": (99, 99),
}
# Division numbers that do not exist inside a section's range.
MISSING_DIVISIONS = {4, 34, 40, 44, 48, 54, 57, 67, 76, 83, 89}


def divisions():
    """The 88 ISIC Rev. 4 divisions as two-digit strings, in order."""
    out = []
    for lo, hi in SECTIONS.values():
        out += [f"{d:02d}" for d in range(lo, hi + 1) if d not in MISSING_DIVISIONS]
    return out


DIVISIONS = divisions()
assert len(DIVISIONS) == 88, len(DIVISIONS)


def section_of(division):
    d = int(division)
    for s, (lo, hi) in SECTIONS.items():
        if lo <= d <= hi:
            return s
    raise ValueError(f"no section for division {division}")


def band_of(persons):
    """The size band of a head count (persons employed, the owner counted)."""
    if persons < 10:
        return "micro"
    if persons < 50:
        return "small"
    if persons < 250:
        return "medium"
    return "large"


def age_class(years):
    if years < 0:
        return None
    if years <= 5:
        return str(years)
    if years >= 26:
        return "26+"
    lo = 6 + 5 * ((years - 6) // 5)
    return f"{lo}-{lo + 4}"


def read_tsv(path):
    """Rows of a TSV as dicts; `#` lines are comments, short rows are padded."""
    with open(path, encoding="utf-8", newline="") as f:
        lines = [line for line in f.read().split("\n") if line and not line.startswith("#")]
    header = lines[0].rstrip("\r").split("\t")
    rows = []
    for line in lines[1:]:
        cells = line.rstrip("\r").split("\t")
        cells += [""] * (len(header) - len(cells))
        rows.append(dict(zip(header, cells)))
    return rows


def countries():
    """The 199 countries the other Tier 5 libraries serve, with country-generator's record."""
    fin = read_tsv(SIBLINGS / "finance-generator/resources/full/countries.tsv")
    served = {r["country_code"] for r in fin if not r["territory"]}
    cg = {r["cca2"]: r for r in read_tsv(SIBLINGS / "country-generator/resources/full/countries.tsv")}
    return {cc: cg[cc] for cc in sorted(served)}


def fmt(x, digits=4):
    """A share or ratio as short text: no trailing zeros, no exponent."""
    if x is None or x == "":
        return ""
    if isinstance(x, str):
        return x
    if isinstance(x, bool):
        return "1" if x else "0"
    if isinstance(x, int):
        return str(x)
    text = f"{x:.{digits}f}".rstrip("0").rstrip(".")
    return "0" if text in ("-0", "") else text


def write_table(tier, name, comment, columns, rows, digits=4):
    """Write resources/<tier>/<name> with the source in its first line."""
    path = RESOURCES / tier / name
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        for line in comment.split("\n"):
            f.write(f"# {line}\n")
        f.write("\t".join(columns) + "\n")
        for row in rows:
            cells = [fmt(row.get(c, ""), digits) for c in columns]
            for cell in cells:
                if "\t" in cell or "\n" in cell or "\r" in cell:
                    raise ValueError(f"{name}: a control character in {cell!r}")
            f.write("\t".join(cells) + "\n")
    return path


def save_work(name, obj):
    WORK.mkdir(parents=True, exist_ok=True)
    path = WORK / name
    path.write_text(json.dumps(obj, ensure_ascii=False, sort_keys=True, indent=0), encoding="utf-8")
    return path


def load_work(name):
    return json.loads((WORK / name).read_text(encoding="utf-8"))


def eurostat(name):
    """A saved Eurostat JSON-stat file as (dimension ids, {(dim values...): value}, labels).

    Geo codes are turned into ISO alpha-2 and aggregates (EU27_2020, EA20, ...) dropped."""
    d = json.loads((RAW / "eurostat" / f"{name}.json").read_text(encoding="utf-8"))
    ids, size = d["id"], d["size"]
    index = [{v: k for k, v in d["dimension"][dim]["category"]["index"].items()} for dim in ids]
    stride = [1] * len(size)
    for i in range(len(size) - 2, -1, -1):
        stride[i] = stride[i + 1] * size[i + 1]
    out = {}
    gi = ids.index("geo")
    for k, v in d["value"].items():
        k = int(k)
        key = [index[i][(k // stride[i]) % size[i]] for i in range(len(size))]
        geo = EUROSTAT_GEO.get(key[gi], key[gi])
        if len(geo) != 2:
            continue
        key[gi] = geo
        out[tuple(key)] = v
    labels = {dim: d["dimension"][dim]["category"]["label"] for dim in ids}
    return ids, out, labels


def normalise(shares):
    total = sum(shares)
    if total <= 0:
        raise ValueError(f"shares sum to {total}")
    return [s / total for s in shares]
