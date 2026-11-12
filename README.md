# Company Generator for C++

> **Requires C++23** (e.g., `-std=c++23` for GCC/Clang, `/std:c++latest` for MSVC).

[![GitHub license](https://img.shields.io/badge/license-MIT-blue.svg)](https://raw.githubusercontent.com/dasmig/company-generator/main/LICENSE.MIT)
[![CI](https://github.com/dasmig/company-generator/actions/workflows/ci.yml/badge.svg)](https://github.com/dasmig/company-generator/actions/workflows/ci.yml)
[![GitHub Releases](https://img.shields.io/github/release/dasmig/company-generator.svg)](https://github.com/dasmig/company-generator/releases)
[![C++23](https://img.shields.io/badge/standard-C%2B%2B23-blue.svg)](https://en.cppreference.com/w/cpp/23)
[![Header-only](https://img.shields.io/badge/type-header--only-green.svg)](https://github.com/dasmig/company-generator#integration)
[![Documentation](https://img.shields.io/badge/docs-GitHub%20Pages-blue.svg)](https://dasmig.github.io/company-generator/)

**[API Reference](https://dasmig.github.io/company-generator/)** · **[Usage Guide](doc/usage.md)** · **[Data and Method](doc/data.md)** · **[Releases](https://github.com/dasmig/company-generator/releases)**

Companies by country: industry, size, legal form, founding date, name, registration numbers and domain.

## Features

- **Country-true Companies**. The industry, size, legal form and age of a company follow the statistics of its country (199 served): Eurostat, the OECD, ILOSTAT, the US Census Bureau, and the whole business registers of the UK, France and Norway. Every company says how well its fields are known (`measured`, `modelled`, `filled`).

- **Names in the Country's Languages**. Names in each country's business-name languages and scripts, with the legal form written the local way: "Fotografie Schmidt e. K.", "ООО «СтройМаркет»", "株式会社渡辺商店", "PT Nusantara Mitra Apotek". A founder surname or place you pass in is used where it fits.

- **Valid Registration Numbers**. 64 register, tax, VAT and LEI schemes, each passing its check digits and encoding what the real one encodes (the RFC's founding date, the SIREN inside the SIRET), checked against python-stdnum on 100,000 numbers per scheme. `is_valid()` checks numbers of your own.

- **Safe Mode**. Numbers no company holds (wrong check digits, reserved prefixes, above the highest issued) and `.example` domains, for test data that leaves the test environment.

- **Forced Fields**. Ask for a country, industry, size, head count or legal form; the rest is drawn to fit, and a request no company can meet throws.

- **Deterministic Seeding**. Per-call `get_company(request, seed)` for reproducible results, and `company::seed()` for replaying a previous generation with the same request and data.

- **Batch Generation**. Generate thousands of results from one seed, on several threads with the same output for any thread count, optionally unique by a key, and stream them as JSON Lines or CSV (see the usage guide).

- **Identical Everywhere**. A seed gives the same result on every compiler, standard library and CPU (GCC, Clang, MSVC, Emscripten; x86-64 and ARM64), checked by known-answer tests.

- **Extensible Data**. Load your own TSV layers on top of the shipped data to add, change or remove entries (`extend()`), checked before they are applied.

- **JSON and Replay**. `to_json()` on every result, and a `signature()` recording the seed and the data it came from.

- **Compiled-in Data**. Define `DASMIG_COMPANY_EMBED` to build the `lite` tier into the program, for WebAssembly and other builds without files.

- **Multi-Instance Support**. Construct independent `cpg` instances with their own data and random engine.

## Integration

[`companygen.hpp`](https://github.com/dasmig/company-generator/blob/main/dasmig/companygen.hpp) is the library [released here](https://github.com/dasmig/company-generator/releases). Keep [`core.hpp`](https://github.com/dasmig/company-generator/blob/main/dasmig/core.hpp) and the [`core/`](https://github.com/dasmig/company-generator/tree/main/dasmig/core) directory ([dasmig-core](https://github.com/dasmig/dasmig-core)) in the same directory. Add

```cpp
#include <dasmig/companygen.hpp>
```

to the files you want to use it in, and supply the [`resources`](https://github.com/dasmig/company-generator/tree/main/resources) folder (or define `DASMIG_COMPANY_EMBED`).

## Usage

```cpp
#include <dasmig/companygen.hpp>
#include <iostream>

auto& gen = dasmig::cpg::instance(); // loads the lite tier

auto v = gen.get_company(dasmig::company_request{});
std::cout << v << '\n';           // DE, Fotografie Schmidt e. K., other professional, scientific and technical activities (74), 2 persons, founded 2021-09-06
std::cout << v.to_json() << '\n'; // {"country_code":"DE","name":"Fotografie Schmidt e. K.",...,"numbers":[...],"domain":"fotografieschmidt.com",...}

// Force what you need; the rest is drawn to fit.
dasmig::company_request r;
r.country = "FR";
r.isic = "F";                     // construction
r.size = dasmig::size_band::small;
r.incorporated_only = true;
auto builder = gen.get_company(r);
builder.numbers;                  // SIREN, SIRET, TVA: each passes its check

r.safe = true;                    // numbers nobody holds, .example domains
auto test_only = gen.get_company(r);

// The same seed gives the same result everywhere.
auto again = gen.get_company(dasmig::company_request{}, v.seed());
```

See the [usage guide](doc/usage.md) for everything else.

## Building

```bash
make            # example
make test       # tests
make bench      # benchmarks, results in build/bench.tsv
make test-embed # tests with the lite tier compiled in
make coverage   # code coverage
make lint       # clang-tidy
make docs       # API docs
make embed      # regenerate dasmig/companygen_data.hpp from resources/lite
```

## Compiler Support

Tested with:
- Clang 18+ (`-std=c++23`)
- GCC 14+ (`-std=c++23`)
- MSVC 19.38+ (`/std:c++latest`)
- Emscripten 3.1.74

## Dependencies

| Dependency | Version | Bundled | Purpose |
|-----------|---------|---------|---------|
| [dasmig-core](https://github.com/dasmig/dasmig-core) | 1.3 | Yes (`core.hpp`, `core/`) | Platform-independent random engine and distributions, data layers, JSON |
| [Catch2](https://github.com/catchorg/Catch2) | 3.x | Yes (amalgamated) | Unit testing |

## License

This library is released under the [MIT License](https://opensource.org/licenses/MIT).

```
MIT License

Copyright (c) 2026 Diego Dasso Migotto
```
