# Usage Guide

This guide covers every feature of the company-generator library. For a quick overview, see the [README](../README.md); for the sources and the model behind the data, see [Data and Method](data.md). For the full API reference, run `doxygen Doxyfile` from the repository root and open `doc/api/html/index.html`.

[TOC]

<!-- GitHub-rendered TOC (Doxygen uses [TOC] above instead) -->
<ul>
<li><a href="#quick-start">Quick Start</a></li>
<li><a href="#installation">Installation</a></li>
<li><a href="#loading-resources">Loading Resources</a></li>
<li><a href="#generating">Generating</a></li>
<li><a href="#forcing-fields">Forcing Fields</a></li>
<li><a href="#weighting">Weighting</a></li>
<li><a href="#names-numbers-and-domains">Names, Numbers and Domains</a></li>
<li><a href="#safe-mode">Safe Mode</a></li>
<li><a href="#evidence">Evidence</a></li>
<li><a href="#seeding-and-deterministic-generation">Seeding and Deterministic Generation</a></li>
<li><a href="#extending-data">Extending Data</a></li>
<li><a href="#replay-and-json">Replay and JSON</a></li>
<li><a href="#compiled-in-data">Compiled-in Data</a></li>
<li><a href="#thread-safety">Thread Safety</a></li>
<li><a href="#batch-generation">Batch Generation</a></li>
<li><a href="#error-reference">Error Reference</a></li>
</ul>

## Quick Start

```cpp
#include <dasmig/companygen.hpp>
#include <iostream>

int main()
{
    auto& gen = dasmig::cpg::instance();
    std::cout << gen.get_company(dasmig::company_request{}) << '\n';
    // DE, Fotografie Schmidt e. K., other professional, scientific and technical activities (74), 2 persons, founded 2021-09-06
}
```

## Installation

Copy `dasmig/companygen.hpp`, `dasmig/core.hpp` and the `dasmig/core/` directory into your include path, keeping them together. Supply `resources/` next to the program, or define `DASMIG_COMPANY_EMBED` to compile the `lite` tier in.

## Loading Resources

`instance()` loads the `lite` tier on first use (or `full` if there is no `lite`), from the compiled-in copy or from the first of `resources/`, `../resources`, `company-generator/resources` and `../company-generator/resources` that has it.

```cpp
dasmig::cpg gen;                           // an empty, independent instance
bool ok = gen.load(dasmig::dataset::lite); // false when the tier is not found
gen.load(std::filesystem::path{"path/to/resources/lite"}); // a directory
gen.has_data();
gen.countries();  // the 199 countries served
gen.divisions();  // the 85 ISIC Rev. 4 divisions
```

Each `load` replaces the data and every layer loaded before. The data is 18 tables; see [Data and Method](data.md).

## Generating

```cpp
auto c = gen.get_company(dasmig::company_request{});      // draws a seed from the instance engine
auto d = gen.get_company(dasmig::company_request{}, 1234); // deterministic; does not touch the engine

c.country_code;      // "DE"
c.name;              // "Fotografie Schmidt e. K."
c.short_name;        // "Fotografie Schmidt", the name without the legal form
c.isic;              // "43", the ISIC Rev. 4 division
c.isic_label;        // "specialized construction activities"
c.isic_section;      // 'F'
c.size;              // size_band::small
c.employees;         // 18: persons employed, the owner counted
c.legal_form;        // "GmbH" (the local name when the form has no abbreviation)
c.legal_form_code;   // "2HBR", ISO 20275; empty for forms the standard lacks
c.legal_form_group;  // "limited_liability"
c.founded;           // std::chrono::year_month_day
c.numbers;           // registration, tax and VAT numbers, then the LEI
c.domain;            // "fotografieschmidt.com"
c.evidence;          // "measured", "modelled" or "filled"
```

Fields are drawn in turn, each conditioned on the ones before: country, industry, size band and head count, legal form, founding date, then the name, numbers and domain. The industry, size and legal form follow the country's own statistics where it has them (Europe, the US and the OECD), else the subregion's. A company is a registered business: most of the world's informal trade is not in the counts (see [Known Limitations](data.md#known-limitations)).

Queries:

```cpp
gen.legal_forms("DE");   // code, abbreviation, name, group, from and until of each legal form
gen.evidence("DE");      // the evidence behind each field of the country's data
```

## Forcing Fields

Every field of `company_request` is optional. A forced field skips its draw; the fields drawn after it are conditioned on it, so a forced industry changes the sizes and forms likely for it, and a forced legal form changes the industries and sizes likely for it.

```cpp
dasmig::company_request r;
r.country = "FR";                         // ISO alpha-2, any case
r.isic = "F";                             // a section letter, or a two-digit division ("43")
r.size = dasmig::size_band::small;        // micro (1-9), small (10-49), medium (50-249), large (250+)
r.employees = 18;                         // a head count of 1 or more; sets the band
r.legal_form = "SAS";                     // an ISO 20275 code ("2HBR") or the abbreviation, any case
r.incorporated_only = true;               // no sole traders or partnerships
r.reference_date = std::chrono::year{2010} / 1 / 1; // bounds the founding date; default 2025-07-01
```

- `legal_form` is checked against the country's forms (`legal_forms()`) and against the reference date: the German Unternehmergesellschaft ("UG (haftungsbeschränkt)") is not yet in force before 2008-11-01.
- A form has the dates it was open to new firms (`from`, `until`) and the industries it is barred from or reserved to (a sole trader cannot be a bank). The founding date and the industry respect them; a request that no company can meet (a forced "e. K." in division 64) throws rather than drawing something else.
- The founding date is bounded by the reference date and by the form's dates; ages follow the country's own distribution, or the US's shape scaled to the country's share of firms up to five years old.

## Weighting

`weight` says what a draw counts:

- `company_weight::enterprises` (the default): each business counts once. This is a register's mix, mostly micro firms: the right default for test data that wants "a company".
- `company_weight::employment`: each business counts for its staff, so the draw is a worker's employer. The country, industry and size band all follow persons employed.

`occupation_isco` (an ISCO-08 code, or just its first digit) draws the industry of a worker in that occupation from the country's employment by occupation and activity, and implies `employment`. A farm worker (`"6"`) works in agriculture; a clerk (`"4"`) is spread over services.

`weighted = false` draws countries uniformly; the rest still follows the data.

## Names, Numbers and Domains

The name is written in one of the country's business-name languages, in its script, from a pattern that fits the legal form and size: a founder ("Weber e. K."), a founder and a trade ("Fotografie Schmidt"), two founders, a descriptive name ("Logística Culiacán S. A. de C. V."), a coined name ("Korion Bau GmbH") or initials. The legal form is written the way the country writes it in names (`ООО «СтройМаркет»`, `株式会社渡辺商店`, `PT Nusantara Mitra Apotek`).

```cpp
dasmig::company_request r;
r.country = "DE";
r.founder_surname = "Hoffmann";  // e.g. from name-generator
r.place = "Bremen";              // e.g. from city-generator
for (std::uint64_t s = 0; s < 12; ++s) gen.get_company(r, s).name;
// "Hoffmann Energie GmbH", "Finanzberatung Bremen GmbH", "Hausverwaltung Hoffmann GbR", "Hoffmann & Sohn e. K.", ...
```

`founder_surname` and `place` fill the parts of the name that take a surname or a place; a name without such a part ignores them, and so does a name whose script does not fit (a Latin surname is not put in a Japanese name). They change only those parts: the other fields and draws stay the same.

`numbers` lists the numbers a company of its form holds, the register's first, then statistical, tax and VAT numbers, then the LEI:

```cpp
for (const auto& n : c.numbers)
{
    n.scheme;   // "de-hrn", "de-ustid", "lei"
    n.label;    // "HR", "USt-IdNr.", "LEI"
    n.value;    // "Amtsgericht Coburg HRA 2106", "DE296418722", as printed
    n.compact;  // "Coburg HRA 2106", "DE296418722": digits and letters
    n.reserved; // true in safe mode
}
gen.is_valid("de-ustid", "DE296418722"); // checks a number's structure and check digits
```

- A number passes its scheme's check and encodes what the scheme encodes: a Mexican RFC holds the founding date, a French SIRET and TVA hold the SIREN, an Indian GSTIN holds the PAN, an Indian CIN holds the division, state and year.
- Sequential numbers (UK company numbers, Polish KRS, ...) follow the founding year: an old firm has a low number.
- Schemes apply to the legal-form groups that hold them (a sole trader has no UK company number) and from the dates they began.
- An LEI is given to firms with 250 or more persons or in finance (section K), never to sole traders.
- `is_valid(scheme, number)` accepts the printed or the compact form; it throws `std::invalid_argument` for an unknown scheme. The 63 schemes are in `resources/*/schemes.tsv`.
- A country without schemes has no numbers (`evidence(country).numbers` is `"none"`).

`domain` is the name's Latin form folded to ASCII (`ü` as `ue` in German), with the country's usual TLD. Every company has one.

## Safe Mode

```cpp
dasmig::company_request r;
r.country = "DE";
r.safe = true;
auto c = gen.get_company(r, 14); // "Korion Bau GmbH": the same name, form and dates as without safe
c.domain;                        // "korionbau.example"
c.numbers[0].reserved;           // true
```

`safe` gives numbers and domains no company holds, for data that may leave a test environment:

- Domains end in `.example` (RFC 2606).
- A number has a wrong check digit, a prefix no issuer uses (an EIN prefix the IRS gives no campus, the LEI prefix `ZZZZ`), or is above the highest number issued. It is marked `reserved`, and `is_valid` rejects the ones with a wrong check.
- A scheme with none of these safe methods is left out (the German, Austrian and Dutch register numbers, India's CIN and PAN, Egypt's and Kenya's tax numbers).
- The name, industry, size, form and dates do not change. A name can still belong to a real firm; nothing rules that out (see [Known Limitations](data.md#known-limitations)).

## Evidence

Each field of a country's data has a level, and every company carries the weakest of the fields it drew:

| Level | Meaning |
| --- | --- |
| `measured` | a statistics office's table, a register or a national census |
| `modelled` | derived: employment by activity with the subregion's enterprises per worker, curated shares, GLEIF's legal-form counts |
| `filled` | the subregion's or the pooled default |

`evidence(country)` lists the level of the industry, size, legal form, age, name, numbers and domain data (`numbers` is `"none"` for a country with no schemes). Treat `filled` as plausible, not measured. Coverage by field is in [Data and Method](data.md#coverage).

## Seeding and Deterministic Generation

- `get_company(request, seed)` gives the same result for the same request, seed and data, on every platform, without advancing the instance engine.
- `get_company(request)` draws a fresh seed from the instance engine, initially seeded from system entropy. There are no generator-level `seed()` or `unseed()` controls; for deterministic sequences, keep a `dasmig::core::rng` in the caller and pass its next value as the seed.
- `c.seed()` is the seed a result was generated from: `get_company(request, c.seed())` replays it with the same request and data.
- The call seed splits into the streams `country`, `industry`, `size`, `employees`, `form`, `founded`, `name` (one split per part of the name), `numbers` (one split per scheme), `domain` and `lei`. Forcing the country or the division to the value that would have been drawn changes nothing else; forcing a size or a legal form can move the draws after it, because they are conditioned on it. Safe mode changes only the numbers and the domain.

## Extending Data

The shipped data is a base layer. Files with the same columns as a table can be loaded on top of it to add rows, change them or remove them, without editing the shipped file. Rows are keyed by the table's key columns (`country_code` and `key` for `legal_forms.tsv`, `country_code` and `division` for `industries.tsv`, and so on; see [Data and Method](data.md#tables)).

```cpp
// A directory holding any of the table files; all are applied together or none.
gen.extend("mods/");

// One file, by its name (legal_forms.tsv, industries.tsv, ...).
gen.extend("mods/legal_forms.tsv");

// From text, for example supplied by a WebAssembly host.
gen.extend_from_string(dasmig::company_table::legal_forms,
    "country_code\tkey\tcode\tgroup\tabbreviation\tlatin_abbreviation\tname\tlatin_name\tfrom\tuntil\tonly\tnot\tshare_small\tshare_large\tevidence\n"
    "DE\tmy_form\t\tlimited_liability\tXY\tXY\tMy form\tMy form\t\t\t\t\t0.01\t0.01\tcurated\n",
    "scenario");

// Remove a row: a layer needs only the key and a disabled column.
gen.extend_from_string(dasmig::company_table::legal_forms, "country_code\tkey\tdisabled\nDE\tmy_form\t1\n", "cleanup");
```

- Every value in a layer is checked before anything changes. A bad layer throws `dasmig::core::tsv_error` with the file, line and column; rows that are valid alone but do not fit the other tables (a legal form for an unknown country) throw `std::invalid_argument`. Either way the data stays as it was.
- `layers(table)` lists what is loaded for a table: source, mode, rows added, replaced and removed, and warnings such as unknown columns.
- `load(...)` replaces the base and drops every layer.
- A form's share is relative to the others in its group, so a layer that adds a form with a share of 1000000 makes it the only one drawn in its group.

## Replay and JSON

Every result records the seed and the data it came from:

```cpp
auto c = gen.get_company(dasmig::company_request{});
c.signature().seed; // the call seed
c.signature().data; // gen.data_hash() when it was generated
```

`to_json()` gives the result as JSON, the form the WebAssembly layer and the game consume. Seeds are strings because JavaScript numbers cannot hold every 64-bit value. `to_json(writer)` writes into an existing `dasmig::core::json_writer`, for composing larger documents.

## Compiled-in Data

Define `DASMIG_COMPANY_EMBED` before including the header to compile the `lite` tier into the program. `load(dataset::lite)` and `instance()` then use it and need no files. The compiled-in data has the same `data_hash()` as the file, so seeds replay the same either way.

`dasmig/companygen_data.hpp` is generated from `resources/lite` by `make embed`; CI checks it is up to date.

## Thread Safety

Each `cpg` instance is independent. Concurrent calls to `get_company(request)` on the **same** instance require external synchronisation (e.g. a `std::mutex`); the seeded overloads are `const` and may run concurrently.

## Batch Generation

Every seeded getter has batch forms: `get_company_batch` returns a `std::vector`, and `for_each_company` streams the items to a sink in index order without holding the batch.

```cpp
auto& gen = dasmig::cpg::instance();
const auto request = dasmig::company_request{};

// 1,000 results from one batch seed, on four threads.
auto items = gen.get_company_batch(request, {.count = 1000, .threads = 4}, 42);

// Item 1 replays on its own: the batch seed and the index give its seed.
auto again = gen.get_company(request, dasmig::core::item_seed(42, 1));

// A million results to a file as JSON Lines, never all in memory.
std::ofstream out{"items.jsonl"};
dasmig::core::jsonl_writer lines{out};
gen.for_each_company(request, {.count = 1'000'000, .threads = 0}, 42, lines);
```

- **Item seeds.** Item `i` is exactly `get_company(request, core::item_seed(batch_seed, first + i))`. A range can be generated directly (`first = 1000` starts at item 1000), two batches that follow each other equal one, and the output is the same for any `threads` (0 means the hardware count). Without a `batch_seed` one is drawn like the single getter draws its own.
- **Options.** `core::batch_options` has `count`, `first`, `threads` and `unique`. The request describes one item; the options describe the batch.
- **Uniqueness.** `.unique = core::unique_rule::by<company>(key)` regenerates an item whose key already appeared in the batch, with a deterministic retry seed, and throws `std::runtime_error` naming the item when the attempts run out.
- **Sinks.** `core::jsonl_writer` writes one `to_json()` per line, `core::csv_writer` flattens each result's JSON into dotted columns. Any callable `(std::uint64_t index, company&)` works.
- Batch calls use the same const getters from several threads, which the thread-safety rules above allow; the CI runs the batch tests under ThreadSanitizer.

## Error Reference

| Exception | When |
|-----------|------|
| `std::runtime_error` | Any generation or query call before data is loaded |
| `std::invalid_argument` | An unknown country, ISIC code or number scheme (`is_valid`); a legal form the country does not have, not yet in force at the reference date, or not incorporated with `incorporated_only`; a head count of 0 or outside the forced band; `occupation_isco` that is not ISCO-08; a reference date that is invalid or outside 1900 to 2100; a request no company can meet (a forced form barred from the forced industry); tables that do not fit together after a layer |
| `dasmig::core::tsv_error` | A malformed table or layer (message gives file, line and column); the data is unchanged |
