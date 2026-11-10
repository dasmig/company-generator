"""Read the curated business-name word lists (curated/words/<language>.tsv) and check them.

Each list holds rows "kind  key  value": value is ";"-separated entries "word[=latin][:weight]".
A trade key is an ISIC division ("43") or a section letter ("F") that serves the section's
divisions without a row of their own. Templates are entries too; their words are patterns with
{placeholders}. Words in a non-Latin script must carry a Latin form (used for domains), and so must
templates with non-Latin literal text.

Run directly to print a summary; prepare_tables.py imports load_words() and words_rows().
"""
import re
import sys
import unicodedata

from companygen_data import CURATED, DIVISIONS, read_tsv, section_of

WORDS_DIR = CURATED / "words"
KINDS = ("style", "template", "trade", "adjective", "region", "place", "surname", "family", "joiner",
         "coined_head", "coined_tail")
PATTERNS = ("founder", "founder_trade", "two_founders", "descriptive", "coined", "initials")
PLACEHOLDERS = {"surname", "surname2", "trade", "place", "region", "adjective", "adjective2",
                "adjective3", "coined", "coined_head", "coined_tail", "initials", "family", "joiner"}
STYLES = {"script", "form_position", "space", "coined_join", "quotes"}
PLACEHOLDER = re.compile(r"\{([a-z_0-9]+)\}")


class WordError(ValueError):
    pass


def is_latin(text):
    """True when every letter in text is Latin (accents allowed)."""
    for ch in text:
        if ch.isalpha() and "LATIN" not in unicodedata.name(ch, ""):
            return False
    return True


def parse_entry(entry, where):
    """'word[=latin][:weight]' -> (word, latin, weight)."""
    weight = 1.0
    head, sep, tail = entry.rpartition(":")
    if sep and re.fullmatch(r"\d+(\.\d+)?", tail):
        entry, weight = head, float(tail)
    word, _, latin = entry.partition("=")
    word, latin = word.strip(), latin.strip()
    if not word:
        raise WordError(f"{where}: empty entry")
    if weight <= 0:
        raise WordError(f"{where}: weight {weight} for {word!r}")
    return word, latin, weight


def load_language(path):
    lang = path.stem
    data = {"language": lang, "style": {}, "entries": {}}
    for row in read_tsv(path):
        kind, key, value = row["kind"], row["key"], row["value"]
        where = f"{path.name} {kind} {key}"
        if kind not in KINDS:
            raise WordError(f"{where}: unknown kind")
        if kind == "style":
            if key not in STYLES:
                raise WordError(f"{where}: unknown style")
            data["style"][key] = value
            continue
        entries = [parse_entry(e, where) for e in value.split(";") if e.strip()]
        if not entries:
            raise WordError(f"{where}: no entries")
        if (kind, key) in data["entries"]:
            raise WordError(f"{where}: repeated row")
        data["entries"][(kind, key)] = entries
    return data


def load_words():
    return {p.stem: load_language(p) for p in sorted(WORDS_DIR.glob("*.tsv"))}


def trade_entries(data, division):
    """The trade words serving a division: its own row, else its section's."""
    e = data["entries"]
    return e.get(("trade", division)) or e.get(("trade", section_of(division)))


def check_language(data, drawn):
    lang, e, problems = data["language"], data["entries"], []
    script = data["style"].get("script")
    if not script:
        problems.append(f"{lang}: no script style")
    latin_script = script == "Latn"
    templates = {k: v for (kind, k), v in e.items() if kind == "template"}
    for name in templates:
        if name not in PATTERNS:
            problems.append(f"{lang}: unknown pattern {name}")
    for p in ("founder", "descriptive", "coined"):
        if p not in templates:
            problems.append(f"{lang}: no {p} template")
    have = {kind for kind, _ in e}
    for name, entries in templates.items():
        for word, latin, _ in entries:
            names = set(PLACEHOLDER.findall(word))
            if names - PLACEHOLDERS:
                problems.append(f"{lang}: {name} uses {sorted(names - PLACEHOLDERS)}")
            literal = PLACEHOLDER.sub("", word)
            if not is_latin(literal) and not latin:
                problems.append(f"{lang}: template {word!r} needs a Latin form")
            if latin and set(PLACEHOLDER.findall(latin)) != names:
                problems.append(f"{lang}: template {word!r} Latin form has other placeholders")
            for ph in names:
                base = re.sub(r"\d$", "", ph)
                if base in ("coined", "initials", "surname", "trade"):
                    continue
                if base not in have and not (base == "place"):
                    problems.append(f"{lang}: {word!r} uses {{{ph}}} but the list has no {base} words")
            if "{coined}" in word and ("coined_head" not in have or "coined_tail" not in have):
                problems.append(f"{lang}: {word!r} needs coined_head and coined_tail")
    for (kind, key), entries in e.items():
        if kind == "template":
            continue
        for word, latin, _ in entries:
            if not latin_script and not latin and not is_latin(word):
                problems.append(f"{lang}: {kind} {key} {word!r} has no Latin form")
            if latin and not is_latin(latin):
                problems.append(f"{lang}: {kind} {key} {word!r} Latin form {latin!r} is not Latin")
        if kind == "trade" and not (re.fullmatch(r"\d\d", key) or re.fullmatch(r"[A-S]", key)):
            problems.append(f"{lang}: trade key {key!r}")
        if kind == "trade" and re.fullmatch(r"\d\d", key) and key not in DIVISIONS:
            problems.append(f"{lang}: trade key {key} is not an ISIC division")
    missing = [d for d in drawn if not trade_entries(data, d)]
    if missing:
        problems.append(f"{lang}: no trade words for divisions {missing}")
    return problems


def check_all(words, drawn):
    problems = []
    for data in words.values():
        problems += check_language(data, drawn)
    native = {r["culture"] for r in read_tsv(CURATED / "surnames.tsv") if not is_latin(r["surname"])}
    for row in read_tsv(CURATED / "name_languages.tsv"):
        for part in row["languages"].split(";"):
            code, _, share = part.partition(":")
            if code not in words:
                problems.append(f"name_languages {row['country']}: no word list for {code}")
                continue
            data = words[code]
            if (data["style"].get("script") != "Latn" and ("surname", "*") not in data["entries"]
                    and row["culture"] not in native):
                problems.append(f"name_languages {row['country']}: {code} names need native surnames,"
                                f" and {row['culture']} has none")
    return problems


def words_rows(words, keep=None):
    """Flatten to rows (language, kind, key, word, latin, weight). keep(kind) limits entries per row."""
    rows = []
    for lang, data in sorted(words.items()):
        for key, value in sorted(data["style"].items()):
            rows.append((lang, "style", key, value, "", 1.0))
        for (kind, key), entries in sorted(data["entries"].items()):
            limit = keep(kind) if keep else None
            for word, latin, weight in entries[:limit]:
                rows.append((lang, kind, key, word, latin, weight))
    return rows


def main():
    sys.stdout.reconfigure(encoding="utf-8")
    from read_sources import DRAWN
    words = load_words()
    problems = check_all(words, DRAWN)
    for p in problems:
        print(p)
    n = sum(len(v) for d in words.values() for v in d["entries"].values())
    print(f"{len(words)} languages, {n} entries, {len(problems)} problems")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
