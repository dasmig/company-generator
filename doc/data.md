# Data and Method

The data in `resources/` and how company-generator uses it (rule 6 of the dasmig standard). The design is in `designs/company-generator.md` of the meta repository; this page describes what was built.

**Status:** the tables are built (Milestone 3). The engine that draws from them is Milestone 4; until then the library still loads the scaffold's sample `company.tsv`.

## Tiers

| Tier | Size | What it holds |
| --- | --- | --- |
| `full` | 4.05 MB | Every table at ISIC division level, source texts, 1,000 surnames per culture, 40 places per country, the whole word lists |
| `lite` | 1.03 MB (compiled in with `DASMIG_COMPANY_EMBED`) | Sizes by ISIC section, three significant digits, no source texts, legal forms under 1% of their group left out, 30 surnames per culture, 8 places per country, shorter word lists |

Readers normalise every row, so the rounding and the left-out entries do not bias a draw.

## Tables

All countries are the 199 the other Tier 5 libraries serve, keyed by `country_code` (ISO alpha-2). A `*` row is the pooled default for countries without one of their own.

| Table | Key | Values |
| --- | --- | --- |
| `countries.tsv` | country | enterprises, persons employed, the languages business names are written in, the surname culture, the evidence (and in `full` the source) of each field, the share of firms up to five years old, the share of firms without a website by size band |
| `industries.tsv` | country, ISIC Rev. 4 division | enterprises and persons employed per million of the country's total |
| `sizes.tsv` | country, division (`lite`: section), band | share of enterprises and of persons employed, mean head count in the band |
| `ages.tsv` | country, section, band | share of firms by age: 0 to 5 years, then 6-10, 11-15, 16-20, 21-25, 26+ |
| `occupations.tsv` | country, ISCO-08 major group | persons employed per million of the country's total, by ISIC section |
| `legal_forms.tsv` | country, group, form | ISO 20275 code, abbreviation and name (with Latin forms), the dates the form was open to new firms, the sections it is kept to or out of, its share of the group among small and among large firms |
| `groups.tsv` | country, section | the base weight of each legal-form group |
| `form_odds.tsv` | section, band | the odds of each legal-form group by size |
| `words.tsv` | language | name styles, templates, trade words per division, adjectives, region words, places, family words, joiners, coined-name syllables |
| `patterns.tsv` | language, group, band | the share of each name pattern |
| `surnames.tsv` | culture | surnames for countries whose name language has none of its own |
| `places.tsv` | country | places for names, where the name language has none of its own |
| `domains.tsv` | country | top-level domains of company websites |
| `schemes.tsv` | scheme | how each registration and tax number is built, who holds it, its safe method |
| `anchors.tsv` | scheme, series, year | the distribution of numbers issued each year, and the highest issued |

## Sources

| Source | Licence | Used for |
| --- | --- | --- |
| Eurostat: `sbs_sc_ovw` (SBS), `bd_l_form`, `bd_size_ent`, `bd_size_brth`, `bd_size_srvl`, `lfsa_eisn2` | Eurostat reuse policy (free reuse with attribution) | Europe's enterprises and employment by division and size, legal-form groups by section, the share of young firms |
| OECD Structural and Demographic Business Statistics (SDBS, ISIC Rev. 4) | CC BY 4.0 | Enterprises and employment by division and size outside the EU |
| ILOSTAT: `EMP_TEMP_SEX_EC2_NB_A`, `EMP_TEMP_SEX_ECO_NB_A`, `EMP_TEMP_ECO_OCU_NB_A` | CC BY 4.0 | Employment by division (153 areas) or section, and by occupation × section |
| US Census Bureau: Business Dynamics Statistics 2023, Nonemployer Statistics 2022, the NAICS 2017 to ISIC 4 concordance | Public domain | Firm age by sector and size; nonemployers by sector |
| IRS Statistics of Income (returns by entity type, 2021) and exempt organisations | Public domain | The US's legal-form split (`scripts/curated/group_splits.tsv`) |
| GLEIF: the Entity Legal Forms code list (ISO 20275, v1.6) and the LEI golden copy | CC0 | Legal forms, and their shares where no register is open |
| Companies House, Basic Company Data (2026-11-01) | Open Government Licence v3.0 | UK legal forms by industry and age, issue anchors of company numbers, English trade words |
| INSEE, SIRENE stock of legal units | Licence Ouverte / Etalab 2.0 | French legal forms by industry, size and age, SIREN anchors, French trade words |
| Brønnøysund Register Centre, Enhetsregisteret | NLOD 2.0 | Norwegian legal forms by industry, size and age, organisation-number anchors, Norwegian trade words |
| National economic censuses and business counts (CN, IN, ID, PK, BD, RU, BR, MX, JP, VN, EG, GB, AU, CA, KR), read by hand | Facts reported by the statistics offices; see `scripts/curated/census.tsv` | Enterprise totals, sector shares and legal-form splits where no machine-readable source exists |
| name-generator surnames (dasmig) | MIT | Surnames by culture (decision 7) |
| address-generator places (GeoNames) | CC BY 4.0 | Places for names |
| country-generator (dasmig) | MIT | Countries, regions, income groups, ccTLDs |
| The number survey (`designs/company-generator-numbers.tsv`, copied to `scripts/curated/survey_numbers.tsv`) | MIT | Every registration and tax number: structure, check, worked examples, sources per row |

Only counts and word statistics are kept from the registers and GLEIF, never a name or a number of a real firm.

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

The division from `industries.tsv`, by enterprises or by employment. The size band from `sizes.tsv` (the country's row for the division, else its section, else the pooled row); the head count within the band from a discrete power law fitted to the band's mean. A mean outside its band in the source (confidential SBS cells, or persons employed counted without the owner) is replaced by the pooled mean of the division, else a default for the band (2, 19, 100, 900).

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

Constraints: `from` and `until` (Germany's UG from 2008, France's SAS from 1994, the US LLC from 1977, Japan's 有限会社 until 2006, Russia's ОАО until 2014; `scripts/curated/form_dates.tsv`), `only` (banks and insurers to section K, agricultural forms to A, professional forms to M and Q, public bodies to O, P, Q, R, E and H, associations and foundations to P, Q, R, S, M, N and I) and `not` (no sole trader in divisions 64 and 65, no partnership in 65).

### Age

The US's age shape by section and band (BDS), or the register's own (France, Norway, the UK). Countries with Eurostat's measure scale the first five years to their `young_share`.

### Name

A pattern (founder, founder and trade, two founders, descriptive, coined, initials) from `patterns.tsv` by the name language, the group and the band; then the language's template for the pattern, filled with surnames, trade words of the division, adjectives, places and coined syllables from `words.tsv`, `surnames.tsv` and `places.tsv`; the legal form placed as the language's style says.

The trade words were read by hand: for English, French and Norwegian from the words most over-represented in each division's names in the register (lift of 3 or more, in 40 or more names), for the other 35 languages against GLEIF's most frequent words. Brands and place names were left out.

### Numbers

Each country's schemes in `schemes.tsv`, held by the legal-form groups named in `groups` (a sole trader has no UK company number). Numbers that encode the company (Mexico's RFC its initials and date, India's CIN its year and form, Russia's OGRN its year and region, Spain's NIF its form, South Africa's its year and type) are built from it. Sequential numbers are drawn from their founding year's distribution in `anchors.tsv`. Safe numbers: a wrong check digit, a reserved range (the 17 EIN prefixes the IRS gives no campus), or a number above the highest issued; a scheme with none of these is left out in safe mode (decision 10).

### Domain

The name's Latin form, lower-cased, with a TLD from `domains.tsv`; a firm has none at the rate of `no_domain_<band>` in `countries.tsv`. Safe mode uses `.example` (RFC 2606).

## Curated Estimates

These are set by hand and not measured; each file says so:

- The name patterns by group and band, and their language tilts (`patterns.tsv`, `pattern_tilts.tsv`).
- The TLD shares (`domains.tsv`): registries publish their own counts, not company websites by TLD.
- The share of firms without a website: guided by Eurostat's `isoc_ciweb` (about 78%, 91% and 95% of EU firms with 10-49, 50-249 and 250+ persons had a website in 2023; recalled, not downloaded) for high-income countries; micro firms and other income groups are estimated.
- The income-group legal-form splits, the template forms and their shares.

## Rebuilding the Data

```
python scripts/fetch_raw.py          # downloads to raw_data/ (about 2.5 GB, not in the repository)
python scripts/read_sources.py       # Eurostat, SDBS, ILOSTAT, US: raw_data/work/stats.json
python scripts/read_registers.py     # UK, FR, NO registers and GLEIF: counts and word statistics
python scripts/read_anchors.py       # issue anchors of sequential numbers
python scripts/prepare_words.py      # checks the word lists
python scripts/prepare_tables.py     # writes resources/full and resources/lite
python scripts/check_company.py      # checks both tiers, prints the coverage
python scripts/embed.py resources --tier lite --name companygen --output dasmig/companygen_data.hpp
```

A rebuild from the same downloads is byte-identical (also under a random `PYTHONHASHSEED`).

## Known Limitations

- **Names may exist.** A name made of a common surname and a trade word ("Müller Bau GmbH") surely belongs to a real firm somewhere. Safe mode changes numbers and domains, not names; nothing in the data can rule a name out without shipping the registers.
- **Size outside the OECD** is the subregion's pooled distribution for most countries (82% of the population).
- **Legal forms outside Europe** come from an overall split and Europe's odds by section and size; within groups from LEI counts, which over-represent large firms (damped, not removed).
- **Some places** are GeoNames' English names ("Munich" beside "Köln").
- **Number code lists** some builders need (German courts, Chinese counties, Japanese registry offices, Korean tax offices, Russian regions, Vietnamese provinces, Indian states) are still to be added (`TBD` in `scripts/curated/schemes.tsv`); until then those parts are drawn from their patterns.
