# Data and Method

The data in `resources/` and how company-generator uses it (rule 6 of the dasmig standard). The design is in `designs/company-generator.md` of the meta repository; this page describes what was built.

**Status:** the tables are built (Milestone 3); the engine draws the country, industry, size band and head count, legal form and founding date (Milestone 4), and the name, registration numbers and domain (Milestone 5).

## Tiers

| Tier | Size | What it holds |
| --- | --- | --- |
| `full` | 4.36 MB | Every table at ISIC division level, source texts, 1,000 surnames per culture, 40 places per country, the whole word lists, every Chinese county, the LEI issuers that hold 99.5% of each country's LEIs |
| `lite` | 1.10 MB (compiled in with `DASMIG_COMPANY_EMBED`) | Sizes by ISIC section, three significant digits, no source texts, legal forms under 1% of their group left out, 30 surnames per culture, 8 places per country, shorter word lists, the 300 most populous Chinese counties, at most 4 LEI issuers per country |

Readers normalise every row, so the rounding and the left-out entries do not bias a draw.

## Tables

All countries are the 199 the other Tier 5 libraries serve, keyed by `country_code` (ISO alpha-2). A `*` row is the pooled default for countries without one of their own.

| Table | Key | Values |
| --- | --- | --- |
| `isic.tsv` | section or division | the ISIC Rev. 4 titles, lower-cased as `to_string()` prints them (sections A to S; divisions 01 to 96) |
| `countries.tsv` | country | enterprises, persons employed, the languages business names are written in, the surname culture, the evidence (and in `full` the source) of each field, the share of firms up to five years old, the share of firms without a website by size band |
| `industries.tsv` | country, ISIC Rev. 4 division | enterprises and persons employed per million of the country's total |
| `sizes.tsv` | country, division (`lite`: section), band | share of enterprises and of persons employed, mean head count in the band |
| `ages.tsv` | country, section, band | share of firms by age: 0 to 5 years, then 6-10, 11-15, 16-20, 21-25, 26+ |
| `occupations.tsv` | country, ISCO-08 major group | persons employed per million of the country's total, by ISIC section |
| `legal_forms.tsv` | country, key (the ISO 20275 code, else the Latin name) | group, ISO 20275 code, abbreviation and name (with Latin forms), the dates the form was open to new firms, the sections it is kept to or out of, its share of the group among small and among large firms, how it is written in a name (`in_name`) |
| `groups.tsv` | country, section | the base weight of each legal-form group |
| `form_odds.tsv` | section, band | the odds of each legal-form group by size |
| `words.tsv` | language | name styles, templates, trade words per division, adjectives, region words, places, family words, joiners, coined-name syllables |
| `patterns.tsv` | language, group, band | the share of each name pattern |
| `surnames.tsv` | culture | surnames for countries whose name language has none of its own |
| `places.tsv` | country | places for names, where the name language has none of its own |
| `domains.tsv` | country | top-level domains of company websites |
| `schemes.tsv` | scheme | how each registration and tax number is built (builder, display mask, pattern, check, parameters), who holds it, the dates it applies, its safe method |
| `anchors.tsv` | scheme, series, year | the distribution of numbers issued each year, and the highest issued |
| `codes.tsv` | list, code | the codes number builders draw from: Indian states, Hungarian tax directorates, German register courts, Chinese counties, Russian regions, Romanian counties, Thai provinces, each country's LEI issuers |
| `fold.tsv` | letter | the ASCII form of Latin letters, for domains |

## Sources

| Source | Licence | Used for |
| --- | --- | --- |
| Eurostat: `sbs_sc_ovw` (SBS), `bd_l_form`, `bd_size_ent`, `bd_size_brth`, `bd_size_srvl`, `lfsa_eisn2` | Eurostat reuse policy (free reuse with attribution) | Europe's enterprises and employment by division and size, legal-form groups by section, the share of young firms |
| OECD Structural and Demographic Business Statistics (SDBS, ISIC Rev. 4) | CC BY 4.0 | Enterprises and employment by division and size outside the EU |
| ILOSTAT: `EMP_TEMP_SEX_EC2_NB_A`, `EMP_TEMP_SEX_ECO_NB_A`, `EMP_TEMP_ECO_OCU_NB_A` | CC BY 4.0 | Employment by division (153 areas) or section, and by occupation × section |
| US Census Bureau: Business Dynamics Statistics 2023, Nonemployer Statistics 2022, the NAICS 2017 to ISIC 4 concordance | Public domain | Firm age by sector and size; nonemployers by sector |
| IRS Statistics of Income (returns by entity type, 2021) and exempt organisations | Public domain | The US's legal-form split (`scripts/curated/group_splits.tsv`) |
| GLEIF: the Entity Legal Forms code list (ISO 20275, v1.6) and the LEI golden copy | CC0 | Legal forms, and their shares where no register is open; the issuer (LOU) prefixes of each country's LEIs |
| Companies House, Basic Company Data (2026-11-01) | Open Government Licence v3.0 | UK legal forms by industry and age, issue anchors of company numbers, English trade words |
| INSEE, SIRENE stock of legal units | Licence Ouverte / Etalab 2.0 | French legal forms by industry, size and age, SIREN anchors, French trade words |
| Brønnøysund Register Centre, Enhetsregisteret | NLOD 2.0 | Norwegian legal forms by industry, size and age, organisation-number anchors, Norwegian trade words |
| National economic censuses and business counts (CN, IN, ID, PK, BD, RU, BR, MX, JP, VN, EG, GB, AU, CA, KR), read by hand | Facts reported by the statistics offices; see `scripts/curated/census.tsv` | Enterprise totals, sector shares and legal-form splits where no machine-readable source exists |
| name-generator surnames (dasmig) | MIT | Surnames by culture (decision 7) |
| address-generator places (GeoNames) | CC BY 4.0 | Places for names (22 capitals and large cities given their local names) |
| id-generator code lists (Chinese counties and Thai provinces from Wikidata, CC0; Russian regions and Romanian counties curated, MIT) | CC0, MIT | Region codes inside Chinese, Russian, Romanian and Thai numbers |
| email-generator's fold table (Unicode decomposition) | MIT | The ASCII form of names in domains |
| country-generator (dasmig) | MIT | Countries, regions, income groups, ccTLDs |
| The number survey (`designs/company-generator-numbers.tsv`, copied to `scripts/curated/survey_numbers.tsv`) | MIT | Every registration and tax number: structure, check, worked examples, sources per row |

Only counts and word statistics are kept from the registers and GLEIF, never a name or a number of a real firm (from the LEI golden copy, only the four-character issuer prefixes and their counts).

python-stdnum (LGPL 2.1) checks the numbers in `scripts/check_numbers.py`; nothing of it is shipped or needed to build or use the library.

## Evidence

Each field of `countries.tsv` says where its value comes from:

- `measured`: a statistics office's table for the country (Eurostat, SDBS, ILOSTAT, the US Census Bureau, IRS).
- `register`: a whole business register (UK, France, Norway).
- `census`: a national census or business count read by hand.
- `employment`: the country's employment by division with the subregion's enterprises per worker.
- `curated`: set by hand from what is known of the country (legal forms by legal tradition, income-group splits, TLD shares).
- `filled`: the subregion's or the pooled default.

### Coverage

Share of the 199 countries' population by evidence (`scripts/check_company.py`):

| Field | |
| --- | --- |
| industry | census 53.9%, employment 27.8%, measured 14.6%, filled 3.7% |
| employment | measured 72.3%, filled 21.8%, measured by section 5.9% |
| size | filled 82.4%, measured 14.6%, measured by section 3.0% |
| legal form | census 50.1%, curated 37.9%, measured 10.2%, register 1.8% |
| age | filled 88.1%, measured 10.1%, register 1.8% |
| domain | curated 77.4%, filled 22.6% |

## Generation Model

Each field is drawn in turn, conditioned on the ones before (design section 5).

### Industry and Size

The division from `industries.tsv`, by enterprises or by employment. The size band from `sizes.tsv` (the country's row for the division, else its section, else the pooled division, else the pooled section), by the cell's share of enterprises or of persons employed. A cell with shares of one kind only (the UK register has no persons employed by size) takes the other from the mean head count of each band: the share of persons is the share of enterprises times the mean, and the reverse.

The head count within a band is a discrete power law on the band (1-9, 10-49, 50-249) whose exponent is fitted to the band's mean: the engine tabulates the law at exponents 0, 0.1, ..., 10 and draws from the mixture of the two neighbouring laws whose mean is the band's, so the mean is matched exactly. A mean the laws cannot reach is clamped (above the band's midpoint, the uniform law's; below 1.0008, the steepest). A mean outside its band in the source (confidential SBS cells, or persons employed counted without the owner) is replaced by a default for the band (2, 19, 100, 900).

The open top band (250 and more) is a truncated Pareto on 250 to a cap, its exponent fitted to the band's mean. The cap is the scale of the country's largest employers, which hold about 2% of its persons employed, within 1,000 and 100,000.

### Legal Form

The group first: P(group | country, section, band) is proportional to `base × odds`.

- `odds` (`form_odds.tsv`) are pooled from the two registers that know each unit's size, France's and Norway's: how much likelier each group is in a band than across bands. A group a register never counts in a band (a sole trader with 250 staff) has odds 0 there.
- `base` (`groups.tsv`) is fitted so that, over the country's own mix of size bands, the groups add up to the shares measured for it:
  - France and Norway: their registers, by section.
  - 33 European countries: Eurostat's `bd_l_form` by section. Eurostat's "partnership" also holds co-operatives and associations; it is split in the two registers' ratio for the section.
  - The UK, the US, China, India, Brazil, Japan, Korea, Russia, Vietnam, Indonesia, Mexico, Australia and Egypt: an overall split from a census, a register or tax returns (`scripts/curated/group_splits.tsv`).
  - Elsewhere: the income group's curated split (low income: 85% sole traders).
- A country with section rows uses them. Otherwise its base is the pooled section row times the country's multipliers (`groups.tsv` section `*`). The multipliers apply to micro and small firms only; medium and large firms take the pooled base for every group the country has. An overall split is nearly all micro firms, and large firms' forms differ much less between countries: without this rule half of India's large manufacturers came out as partnerships.
- A group the country has no form for is left out.

Then the form within the group (`legal_forms.tsv`), by `share_small` (micro and small firms) or `share_large` (medium and large):

- The register's counts where one is open (France, Norway, the UK, whose large firms take GLEIF's shares since its register has no sizes).
- Else GLEIF's LEI counts. LEIs are held by firms that trade in financial markets, so a form meant for large or listed firms (an AG, an S.A., a PLC) is damped to 15% among small firms when the group has another form.
- Fixed curated shares where LEIs mislead (`scripts/curated/form_overrides.tsv`): foundations, trusts and public bodies hold most LEIs of the "other" group, associations and co-operatives most of its firms.
- Curated forms where ISO 20275 has none: the sole traders and general partnerships it leaves out (`pseudo_forms.tsv`), and whole sets for the 86 countries it does not cover, by legal tradition (`form_templates.tsv`: common law, OHADA, the Maghreb, lusophone and hispanic law, the CIS, ...).

A request that forces the legal form or `incorporated_only`, or the size band, conditions the industry and the size on it (the weight of a division is multiplied by the probability that a company in it meets the request), so a forced "e. K." comes mostly from retail and crafts, and a request no company can meet throws.

Constraints: `from` and `until` (Germany's UG from 2008, France's SAS from 1994, the US LLC from 1977, Japan's 有限会社 until 2006, Russia's ОАО until 2014; `scripts/curated/form_dates.tsv`), `only` (banks and insurers to section K, agricultural forms to A, professional forms to M and Q, public bodies to O, P, Q, R, E and H, associations and foundations to P, Q, R, S, M, N and I) and `not` (no sole trader in divisions 64 and 65, no partnership in 65).

### Age

The US's age shape by section and band (BDS), or the register's own (France, Norway, the UK). A class of the shape (age 0, 1, ..., 5, 6-10, ..., 26+) is spread evenly over its years; the 26+ class decays over ages 26 to 125 with a 25-year scale (curated). Countries with Eurostat's measure scale the first six classes (ages 0 to 5) by one factor per country, fitted so that the country's own mix of sections and bands gives its `young_share`. The US takes the shape as it is.

A year is picked by those weights, then a day inside it, never after the reference date and never outside the legal form's dates: a form's `from` and `until` cut the weight of the years they overlap in proportion to the days left. A firm older than its form's `from` date (one that converted) is not modelled; its founding date is in the form's years.

### Name

The name language by the country's shares in `countries.tsv`. A pattern (founder, founder and trade, two founders, descriptive, coined, initials) from `patterns.tsv` by the language, the group and the band, among the patterns whose parts the language has; then the language's template for the pattern, filled with surnames, trade words of the division (else its section), adjectives, regions, places and coined syllables. Surnames are the language's own, else the country's culture's from `surnames.tsv`; a Latin-script name takes a non-Latin culture's surnames in their Latin form. Places are the language's own, else the country's from `places.tsv` (for Latin-script languages). Each part is drawn from its own stream, so a given `founder_surname` or `place` changes only that part; either is used only when its script fits the language.

The legal form is written as `in_name` says where it is set (`ООО «…»`, `株式会社…` or `…株式会社`), else as its abbreviation, or its Latin abbreviation when the abbreviation's script does not fit the name, before or after the name as the language's style says. `short_name` is the name without the form.

The trade words were read by hand: for English, French and Norwegian from the words most over-represented in each division's names in the register (lift of 3 or more, in 40 or more names), for the other 35 languages against GLEIF's most frequent words. Brands and place names were left out.

### Numbers

Each country's schemes in `schemes.tsv` (64 schemes), held by the legal-form groups named in `groups` (a sole trader has no UK company number) and in force at the founding date or the reference date (`from`, `until`). They are listed register first, then statistical, tax and VAT numbers, then the LEI.

- **Generic schemes** draw the pattern's digits and compute the check (Luhn, GTIN, ISO 7064 mod 11,10 and mod 97-10, weighted sums with the scheme's rule, or a named algorithm such as Belgium's or Taiwan's).
- **Built schemes** encode the company: Mexico's RFC its initials and founding date, India's CIN its listing, division, state, year and ownership, Russia's OGRN its year and region, Spain's NIF its form, Japan's corporate number its registry office and class, South Africa's its year and type, Korea's business number its tax office and type. Numbers that share a part share it: the SIREN in the SIRET and the TVA, the ACN in the ABN, the PAN in the GSTIN, the RSIN in the Dutch VAT number.
- **Sequential numbers** (UK company numbers, Irish CRO, Polish KRS, Nigerian RC and BN, South African and German register numbers) are drawn from their founding year's distribution in `anchors.tsv`, interpolated between quantiles, or from a curve between the first year and the highest issued.
- **The LEI** goes to firms with 250 or more persons or in section K (decision 9), never to sole traders; its prefix is an issuer of the country's LEIs, by their counts.

Safe numbers: a wrong check digit, a reserved range (the 17 EIN prefixes the IRS gives no campus; the LEI prefix `ZZZZ`, which no issuer has), or a number above the highest issued. A scheme with none of these is left out in safe mode (decision 10): the German, Austrian and Dutch register numbers, India's CIN and PAN, and Egypt's and Kenya's tax numbers. Safe numbers are marked `reserved`.

### Domain

Every company has a domain (decision 8): the name's Latin form without the legal form, folded to ASCII (German ä, ö and ü as ae, oe and ue; the rest by `fold.tsv`), as one word or with hyphens (a quarter of firms), cut to whole words within 24 characters and not ending on a word of one or two letters, with a TLD from `domains.tsv`. Safe mode uses `.example` (RFC 2606). The `no_domain_<band>` columns of `countries.tsv` are kept but not used.

## Curated Estimates

These are set by hand and not measured; each file says so:

- The name patterns by group and band, and their language tilts (`patterns.tsv`, `pattern_tilts.tsv`).
- The TLD shares (`domains.tsv`): registries publish their own counts, not company websites by TLD.
- The share of firms without a website: guided by Eurostat's `isoc_ciweb` (about 78%, 91% and 95% of EU firms with 10-49, 50-249 and 250+ persons had a website in 2023; recalled, not downloaded) for high-income countries; micro firms and other income groups are estimated.
- The income-group legal-form splits, the template forms and their shares.

## Validation

`scripts/validate_company.py` builds `scripts/draw_companies.cpp`, draws 20,000 companies per country on both tiers and both weightings (16 million in all), and compares them with the tables by recomputing, independently of the engine, what the counts should be: the shares of the 19 sections, the four size bands, the mean head count of the finite bands, the four legal-form groups (from `groups.tsv`, `form_odds.tsv` and the forms open on 2025-07-01) and the share of firms up to five years old where Eurostat measures it. Each comparison is a z test at 5 standard errors. All 199 countries pass on both tiers (2026-11-11). This checks that the engine draws what the tables say; it does not check the tables against the world, which `check_company.py` and the sources do.

`scripts/check_numbers.py` builds `scripts/draw_numbers.cpp`, draws at least 100,000 numbers of every scheme with safe mode off and on (the LEI pooled over the countries) and checks each with python-stdnum: real numbers must pass and not be marked reserved; safe numbers must be marked reserved and fail stdnum (a wrong check), carry a reserved prefix, or lie above the highest issued. It also checks that an RFC's date is the founding date and a Japanese corporate number's class is one the scheme lists. Seven schemes stdnum lacks are checked by their structure, and Korea's business number also by its check digit, which stdnum does not test. All 126 scheme and mode pairs pass (2026-11-11). The tests check the number survey's worked examples with `is_valid`.

## Rebuilding the Data

```
python scripts/fetch_raw.py          # downloads to raw_data/ (about 2.5 GB, not in the repository)
python scripts/read_sources.py       # Eurostat, SDBS, ILOSTAT, US: raw_data/work/stats.json
python scripts/read_registers.py     # UK, FR, NO registers and GLEIF: counts and word statistics
python scripts/read_anchors.py       # issue anchors of sequential numbers
python scripts/read_lei.py           # GLEIF golden copy: the issuer prefixes of each country's LEIs
python scripts/prepare_words.py      # checks the word lists
python scripts/prepare_tables.py     # writes resources/full and resources/lite
python scripts/check_company.py      # checks both tiers, prints the coverage
python scripts/validate_company.py   # draws from both tiers and compares with the tables (needs a C++23 compiler)
python scripts/check_numbers.py      # checks 100,000 numbers per scheme with python-stdnum (pip install python-stdnum)
python scripts/embed.py resources --tier lite --name companygen --output dasmig/companygen_data.hpp
```

A rebuild from the same downloads is byte-identical (also under a random `PYTHONHASHSEED`).

## Known Limitations

- **Names may exist.** A name made of a common surname and a trade word ("Müller Bau GmbH") surely belongs to a real firm somewhere. Safe mode changes numbers and domains, not names; nothing in the data can rule a name out without shipping the registers.
- **Size outside the OECD** is the subregion's pooled distribution for most countries (82% of the population).
- **Legal forms outside Europe** come from an overall split and Europe's odds by section and size; within groups from LEI counts, which over-represent large firms (damped, not removed).
- **Some code ranges are estimated.** The highest Nigerian RC and BN numbers and the block numbering of some German register courts are estimated from published examples, not read from a register. Japanese registry offices, Korean tax offices, Vietnamese provinces and Indonesian tax offices are drawn from ranges, not from lists of the codes in use.
- **A number can be held.** Outside safe mode a number passes its check and lies in the issued range, so it may belong to a real firm. Use safe mode for anything that leaves a test environment.
- **Name grammar is simple.** Templates join words without inflection: German names avoid adjectival place forms ("Münchner") rather than build them, and a German sole trader can still be "& Co.", which a register would refuse as misleading.
