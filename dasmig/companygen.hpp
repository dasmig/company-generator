// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Diego Dasso Migotto

#ifndef DASMIG_COMPANYGEN_HPP
#define DASMIG_COMPANYGEN_HPP

#include "core.hpp"
#include <algorithm>
#include <array>
#include <bitset>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <initializer_list>
#include <map>
#include <optional>
#include <ostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef DASMIG_COMPANY_EMBED
#include "companygen_data.hpp"
#endif

/// @file companygen.hpp
/// @brief Company Generator library — companies by country: industry, size, legal form, founding date, name, registration numbers and domain for C++23.
/// @author Diego Dasso Migotto (diegomigotto at hotmail dot com)
/// @see See doc/usage.md for the narrative tutorial and doc/data.md for the sources.

DASMIG_CORE_REQUIRE(1, 1);

namespace dasmig
{

/// @brief The size band of a company, by persons employed (the owner counted).
enum class size_band : std::uint8_t
{
    micro,  ///< 1 to 9 persons (with no employee: a sole trader or a shell).
    small,  ///< 10 to 49.
    medium, ///< 50 to 249.
    large   ///< 250 and more.
};

/// @brief What a draw counts.
enum class company_weight : std::uint8_t
{
    enterprises, ///< Each business counts once: the register's mix, mostly micro (the default).
    employment   ///< Each business counts for its staff: a worker's employer.
};

/// @brief The data tables (see doc/data.md).
enum class company_table : std::uint8_t
{
    countries,   ///< countries.tsv: enterprises, persons employed, languages, evidence per field.
    isic,        ///< isic.tsv: ISIC Rev. 4 sections and divisions with their titles.
    industries,  ///< industries.tsv: enterprises and employment by division.
    sizes,       ///< sizes.tsv: size bands by division or section.
    ages,        ///< ages.tsv: firm age by section and band.
    groups,      ///< groups.tsv: the base weight of each legal-form group.
    form_odds,   ///< form_odds.tsv: the odds of each group by section and band.
    legal_forms, ///< legal_forms.tsv: the legal forms of each country.
    occupations, ///< occupations.tsv: persons employed by occupation and section.
    words,       ///< words.tsv: name styles, templates and trade words.
    patterns,    ///< patterns.tsv: the share of each name pattern.
    surnames,    ///< surnames.tsv: surnames by culture.
    places,      ///< places.tsv: places for names.
    domains,     ///< domains.tsv: top-level domains of company websites.
    schemes,     ///< schemes.tsv: registration and tax numbers.
    anchors,     ///< anchors.tsv: issue anchors of sequential numbers.
    codes,       ///< codes.tsv: code lists the number builders draw from.
    fold         ///< fold.tsv: letters and their ASCII form, for domains.
};

/// @brief The key of a size band, as in the tables and JSON.
/// @param b The band.
/// @return `"micro"`, `"small"`, `"medium"` or `"large"`.
[[nodiscard]] constexpr std::string_view to_string(size_band b) noexcept
{
    constexpr std::array<std::string_view, 4> names{"micro", "small", "medium", "large"};
    return names.at(static_cast<std::size_t>(b));
}

/// @brief What a company call asks for.
///
/// Every generation call takes one; `company_request{}` asks for the
/// defaults. Choices are never stored on the generator, so one call cannot
/// change the next. A forced field skips its draw and leaves every other
/// field's draw unchanged (its value may still move, since later fields are
/// conditioned on it).
struct company_request
{
    /// The reference date used when none is set: 2025-07-01.
    static constexpr std::chrono::year_month_day default_reference_date{
        std::chrono::year{2025}, std::chrono::month{7}, std::chrono::day{1}};

    std::optional<std::string> country{};         ///< ISO alpha-2, any case; unset: drawn by enterprises or employment.
    company_weight weight{company_weight::enterprises}; ///< What the draws count.
    std::optional<std::string> isic{};            ///< Forced: a section letter ("F") or a division ("43").
    std::optional<std::string> occupation_isco{}; ///< An ISCO-08 code: draws the industry of a worker in it (implies employment).
    std::optional<size_band> size{};              ///< Forced band.
    std::optional<std::uint32_t> employees{};     ///< Forced head count (1 or more); sets the band.
    std::optional<std::string> legal_form{};      ///< Forced: an ISO 20275 code or the country's abbreviation ("GmbH").
    bool incorporated_only{false};                ///< No sole traders or partnerships.
    std::optional<std::string> founder_surname{}; ///< Used in the name where a template has a surname and the script fits.
    std::optional<std::string> place{};           ///< Used in the name where a template has a place and the script fits.
    std::chrono::year_month_day reference_date{default_reference_date}; ///< Bounds the founding date and picks the number rules in force.
    bool safe{false};                             ///< Numbers nobody holds and `.example` domains; schemes with no safe method are left out.
    bool weighted{true};                          ///< False: countries uniform, the rest by the data.
};

/// @brief One number a company holds.
struct registration
{
    std::string scheme;  ///< The scheme id: `"fr-siren"`, `"de-ustid"`, `"lei"`.
    std::string label;   ///< `"SIREN"`, `"USt-IdNr."`, `"LEI"`.
    std::string value;   ///< As printed: `"552 100 554"`, `"DE123456789"`.
    std::string compact; ///< Digits and letters only (a German register number keeps its court and section).
    bool reserved{false}; ///< Drawn so that no company holds it (safe mode).
};

/// @brief Where the data behind each field of a country comes from.
///
/// Each is `"measured"`, `"modelled"` or `"filled"`; `numbers` is `"none"`
/// for a country with no number schemes.
struct company_evidence
{
    std::string industry; ///< Industry shares.
    std::string size;     ///< Size bands and head counts.
    std::string legal_form; ///< Legal-form shares.
    std::string age;      ///< Firm age.
    std::string name;     ///< Names.
    std::string numbers;  ///< Registration numbers.
    std::string domain;   ///< Domains.
};

/// @brief A legal form a country has.
struct legal_form_info
{
    std::string code;         ///< ISO 20275 code; empty for forms the standard lacks.
    std::string abbreviation; ///< `"GmbH"`; empty when the form has none.
    std::string name;         ///< The local name.
    std::string group;        ///< `"sole_proprietorship"`, `"partnership"`, `"limited_liability"` or `"other"`.
    std::optional<std::chrono::year_month_day> from{}; ///< Open to new firms from this date.
    std::optional<std::chrono::year_month_day> until{}; ///< Closed to new firms after this date.
};

/// @brief Return type for company generation.
///
/// Supports implicit conversion to std::string (a one-line summary),
/// streaming via operator<< and JSON output via to_json().
class company
{
  public:
    std::string country_code;                  ///< ISO alpha-2.
    std::string name;                          ///< `"Bäckerei Hoffmann GmbH"`.
    std::string short_name;                    ///< The name without the legal form.
    std::string isic;                          ///< The ISIC Rev. 4 division: `"10"`.
    std::string isic_label;                    ///< Its title: `"manufacture of food products"`.
    char isic_section{'A'};                    ///< Its section: `'C'`.
    size_band size{size_band::micro};          ///< The band of employees.
    std::uint32_t employees{1};                ///< Persons employed, the owner counted.
    std::string legal_form;                    ///< The abbreviation (the name when the form has none): `"GmbH"`.
    std::string legal_form_code;               ///< ISO 20275 code: `"2HBR"`; empty for curated forms.
    std::string legal_form_group;              ///< `"sole_proprietorship"`, `"partnership"`, `"limited_liability"` or `"other"`.
    std::chrono::year_month_day founded{};     ///< The founding date.
    std::vector<registration> numbers;         ///< The register's, then statistical, tax and VAT, then the LEI.
    std::string domain;                        ///< `"baeckerei-hoffmann.de"`.
    std::string evidence;                      ///< The weakest of the statistical fields: `"measured"`, `"modelled"` or `"filled"`.

    /// @brief Retrieve the random seed used to generate this company.
    /// @return The call seed; pass it to get_company(request, seed) with
    ///         the same request and data to replay this company.
    [[nodiscard]] std::uint64_t seed() const { return _signature.seed; }

    /// @brief The seed and the data this company was generated from.
    /// @return The replay signature; `data` is cpg::data_hash() at the
    ///         time of generation.
    [[nodiscard]] const core::seed_signature& signature() const { return _signature; }

    /// @brief The founding date as `YYYY-MM-DD`.
    /// @return For example `"2011-03-14"`.
    [[nodiscard]] std::string founded_text() const
    {
        return std::format("{:04}-{:02}-{:02}", static_cast<int>(founded.year()),
                           static_cast<unsigned>(founded.month()), static_cast<unsigned>(founded.day()));
    }

    /// @brief Human-readable summary string.
    /// @return For example `"DE, Bäckerei Hoffmann GmbH, manufacture of food
    ///         products (10), 12 persons, founded 2011-03-14"`; the legal form
    ///         stands in for the name until names are drawn.
    [[nodiscard]] std::string to_string() const
    {
        std::string out = country_code + ", " + (name.empty() ? legal_form : name) + ", " + isic_label + " (" + isic +
                          "), " + std::to_string(employees) + (employees == 1 ? " person" : " persons") +
                          ", founded " + founded_text();
        return out;
    }

    /// @brief Write this company as a JSON object.
    /// @param w Writer positioned where a value is allowed.
    void to_json(core::json_writer& w) const
    {
        w.begin_object()
            .field("country_code", country_code)
            .field("name", name)
            .field("short_name", short_name)
            .field("isic", isic)
            .field("isic_label", isic_label)
            .field("isic_section", std::string(1, isic_section))
            .field("size", dasmig::to_string(size))
            .field("employees", employees)
            .field("legal_form", legal_form)
            .field("legal_form_code", legal_form_code)
            .field("legal_form_group", legal_form_group)
            .field("founded", founded_text())
            .key("numbers")
            .begin_array();
        for (const auto& n : numbers)
        {
            w.begin_object()
                .field("scheme", n.scheme)
                .field("label", n.label)
                .field("value", n.value)
                .field("compact", n.compact)
                .field("reserved", n.reserved)
                .end_object();
        }
        w.end_array().field("domain", domain).field("evidence", evidence).key("signature");
        _signature.to_json(w);
        w.end_object();
    }

    /// @brief This company as a JSON document.
    /// @return Compact JSON, for example `{"country_code":"DE","name":"...",...}`.
    [[nodiscard]] std::string to_json() const
    {
        core::json_writer w;
        to_json(w);
        return w.str();
    }

    /// @brief Implicit conversion to std::string.
    /// @return Summary string.
    operator std::string() const // NOLINT(hicpp-explicit-conversions)
    {
        return to_string();
    }

    /// @brief Stream the summary to an output stream.
    /// @param os Output stream.
    /// @param v The company to stream.
    /// @return The stream.
    friend std::ostream& operator<<(std::ostream& os, const company& v)
    {
        os << v.to_string();
        return os;
    }

  private:
    core::seed_signature _signature;
    friend class cpg;
};

/// @cond INTERNAL
namespace companygen_detail
{

inline constexpr std::size_t band_count = 4;
inline constexpr std::size_t group_count = 4;
inline constexpr std::size_t section_count = 19;  // ISIC sections A to S
inline constexpr std::size_t max_divisions = 100; // division numbers 1 to 99
inline constexpr std::size_t age_class_count = 11;
inline constexpr std::size_t isco_groups = 10;
inline constexpr std::size_t grid_size = 101;     // head-count laws at exponents 0, 0.1, ..., 10
inline constexpr std::size_t max_age = 125;       // ages 0 to 125: 126 founding years
inline constexpr std::size_t old_ages = max_age - 25;
inline constexpr std::size_t no_index = static_cast<std::size_t>(-1);

inline constexpr std::array<std::string_view, band_count> band_keys{"micro", "small", "medium", "large"};
inline constexpr std::array<std::string_view, group_count> group_keys{"sole_proprietorship", "partnership",
                                                                      "limited_liability", "other"};
inline constexpr std::array<std::string_view, age_class_count> age_columns{
    "age_0", "age_1", "age_2", "age_3", "age_4", "age_5", "age_6-10", "age_11-15", "age_16-20", "age_21-25", "age_26+"};
// The first and last age in years of each class (the last class runs to max_age).
inline constexpr std::array<std::pair<int, int>, age_class_count> age_span{
    {{0, 0}, {1, 1}, {2, 2}, {3, 3}, {4, 4}, {5, 5}, {6, 10}, {11, 15}, {16, 20}, {21, 25}, {26, 125}}};
inline constexpr std::array<int, band_count> band_lo{1, 10, 50, 250};
inline constexpr std::array<int, 3> band_hi{9, 49, 249};
inline constexpr std::array<double, band_count> band_default_mean{2.0, 19.0, 100.0, 900.0};
// The divisions of each ISIC Rev. 4 section A to S.
inline constexpr std::array<std::pair<int, int>, section_count> section_ranges{
    {{1, 3},   {5, 9},   {10, 33}, {35, 35}, {36, 39}, {41, 43}, {45, 47}, {49, 53}, {55, 56}, {58, 63},
     {64, 66}, {68, 68}, {69, 75}, {77, 82}, {84, 84}, {85, 85}, {86, 88}, {90, 93}, {94, 96}}};

constexpr std::size_t section_of_division(int division) noexcept
{
    for (std::size_t s = 0; s < section_count; ++s)
    {
        if (division >= section_ranges.at(s).first && division <= section_ranges.at(s).second) return s;
    }
    return no_index;
}

constexpr std::size_t section_of_letter(char letter) noexcept
{
    return (letter >= 'A' && letter <= 'S') ? static_cast<std::size_t>(letter - 'A') : no_index;
}

enum class evidence_level : std::uint8_t
{
    measured,
    modelled,
    filled
};

// measured: a statistics office's table, a register or a census; modelled: employment by
// division with the subregion's enterprises per worker, curated shares, GLEIF's counts;
// filled: the subregion's or the pooled default.
inline evidence_level parse_evidence(std::string_view text)
{
    if (text == "measured" || text == "register" || text == "census" || text == "measured_section")
    {
        return evidence_level::measured;
    }
    if (text == "modelled" || text == "employment" || text == "curated" || text == "gleif")
    {
        return evidence_level::modelled;
    }
    if (text == "filled" || text == "pooled") return evidence_level::filled;
    throw std::invalid_argument("unknown evidence '" + std::string{text} + "'");
}

inline std::string evidence_name(evidence_level e)
{
    constexpr std::array<std::string_view, 3> names{"measured", "modelled", "filled"};
    return std::string{names.at(static_cast<std::size_t>(e))};
}

inline void weaken(evidence_level& e, evidence_level by) { e = std::max(e, by); }

inline std::string upper(std::string_view text)
{
    std::string out{text};
    for (auto& c : out)
    {
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    }
    return out;
}

inline bool equal_nocase(std::string_view a, std::string_view b)
{
    return a.size() == b.size() && upper(a) == upper(b);
}

template <std::size_t N>
std::optional<std::size_t> index_of(const std::array<std::string_view, N>& keys, std::string_view text)
{
    for (std::size_t i = 0; i < N; ++i)
    {
        if (keys.at(i) == text) return i;
    }
    return std::nullopt;
}

// A two-digit division number from text, or nullopt.
inline std::optional<int> parse_division(std::string_view text)
{
    if (text.size() != 2 || text[0] < '0' || text[0] > '9' || text[1] < '0' || text[1] > '9') return std::nullopt;
    const int d = (text[0] - '0') * 10 + (text[1] - '0');
    if (d < 1) return std::nullopt;
    return d;
}

// "YYYY-MM-DD" as days since the epoch; nullopt for anything else.
inline std::optional<std::chrono::sys_days> parse_date(std::string_view text)
{
    if (text.size() != 10 || text[4] != '-' || text[7] != '-') return std::nullopt;
    int y = 0;
    unsigned m = 0;
    unsigned d = 0;
    const auto ok = [&](std::string_view part, auto& out)
    {
        const auto [ptr, ec] = std::from_chars(part.data(), part.data() + part.size(), out);
        return ec == std::errc{} && ptr == part.data() + part.size();
    };
    if (!ok(text.substr(0, 4), y) || !ok(text.substr(5, 2), m) || !ok(text.substr(8, 2), d)) return std::nullopt;
    const std::chrono::year_month_day ymd{std::chrono::year{y}, std::chrono::month{m}, std::chrono::day{d}};
    if (!ymd.ok()) return std::nullopt;
    return std::chrono::sys_days{ymd};
}

// The size band of a head count.
inline std::size_t band_of(std::uint64_t persons) noexcept
{
    if (persons < 10) return 0;
    if (persons < 50) return 1;
    if (persons < 250) return 2;
    return 3;
}

// ------------------------------------------------------------------ head counts

// A discrete power law on [lo, hi]: weight n^-alpha. cdf is cumulative and unnormalised.
struct discrete_law
{
    double alpha{0.0};
    double mean{0.0};
    std::vector<double> cdf;
};

// A truncated Pareto on [lo, hi]: density x^-alpha, for the open top band.
struct pareto_law
{
    double alpha{1.5};
    double lo{250.0};
    double hi{100000.0};
};

// A band's head count in one size cell: a mixture of two grid laws (bands micro to medium).
struct head_mix
{
    std::uint32_t first{0};
    std::uint32_t second{0};
    double t{0.0}; // the share of the second
};

// The integral of x^p on [lo, hi].
inline double power_integral(double p, double lo, double hi)
{
    if (std::abs(p + 1.0) < 1e-9) return core::math::log(hi / lo);
    return (core::math::exp((p + 1.0) * core::math::log(hi)) - core::math::exp((p + 1.0) * core::math::log(lo))) / (p + 1.0);
}

inline double pareto_mean(double alpha, double lo, double hi)
{
    return power_integral(1.0 - alpha, lo, hi) / power_integral(-alpha, lo, hi);
}

// The exponent whose truncated Pareto on [lo, hi] has the given mean (clamped to the exponents tried).
inline double pareto_alpha_for(double mean, double lo, double hi)
{
    constexpr double alpha_min = 0.05;
    constexpr double alpha_max = 12.0;
    if (mean >= pareto_mean(alpha_min, lo, hi)) return alpha_min;
    if (mean <= pareto_mean(alpha_max, lo, hi)) return alpha_max;
    double a = alpha_min;
    double b = alpha_max;
    for (int i = 0; i < 60; ++i)
    {
        const double mid = 0.5 * (a + b);
        // A mean falls as the exponent rises.
        if (pareto_mean(mid, lo, hi) > mean) a = mid;
        else b = mid;
    }
    return 0.5 * (a + b);
}

inline double pareto_draw(const pareto_law& law, double u)
{
    const double p = 1.0 - law.alpha;
    if (std::abs(p) < 1e-9) return core::math::exp(core::math::log(law.lo) + u * core::math::log(law.hi / law.lo));
    const double lo_p = core::math::exp(p * core::math::log(law.lo));
    const double hi_p = core::math::exp(p * core::math::log(law.hi));
    return core::math::exp(core::math::log(lo_p + u * (hi_p - lo_p)) / p);
}

// ------------------------------------------------------------------ the model

struct size_cell
{
    std::array<double, band_count> ent{};  // shares of enterprises by band
    std::array<double, band_count> emp{};  // shares of persons employed by band
    std::array<double, band_count> mean{}; // mean head count in the band
    std::array<head_mix, 3> head{};        // micro, small and medium head counts
    unsigned seen{0};                      // a bit per band
};

struct age_ref
{
    std::uint32_t index{0}; // into model.ages
    bool scaled{false};     // the country's young share applies
};

struct form_row
{
    std::string key;
    std::string code;
    std::string abbreviation;
    std::string latin_abbreviation;
    std::string name;
    std::string latin_name;
    std::string in_name;           // how the form is written in a name (legal_forms.tsv)
    std::uint8_t group{0};
    std::optional<std::chrono::sys_days> from;
    std::optional<std::chrono::sys_days> until;
    std::uint32_t only{0};         // sections the form is kept to (a bit per section); 0: any
    std::uint32_t barred_sections{0};
    std::bitset<max_divisions> barred_divisions;
    double share_small{0.0};       // among micro and small firms of the group
    double share_large{0.0};       // among medium and large firms
    evidence_level evidence{evidence_level::filled};
};

struct country_model
{
    std::string code;
    double enterprises{0.0};
    double persons{0.0};
    std::string languages;
    std::string culture;
    evidence_level industry_evidence{evidence_level::filled};
    evidence_level employment_evidence{evidence_level::filled};
    evidence_level size_evidence{evidence_level::filled};
    evidence_level form_evidence{evidence_level::filled};
    evidence_level age_evidence{evidence_level::filled};
    evidence_level domain_evidence{evidence_level::filled};
    std::optional<double> young_share;
    double young_scale{1.0};
    std::array<double, max_divisions> ent{};   // enterprises per million by division number
    std::array<double, max_divisions> emp{};   // persons employed per million by division number
    std::array<double, section_count> section_emp{};
    std::array<std::uint32_t, max_divisions> cell{};  // into model.cells
    std::array<std::uint32_t, max_divisions> large{}; // into model.pareto
    std::array<std::array<age_ref, band_count>, section_count> age{};
    // The base weight of each group: [0] for micro and small firms, [1] for medium and large.
    std::array<std::array<std::array<double, group_count>, section_count>, 2> base{};
    std::vector<form_row> forms;
    std::size_t occupation_table{0}; // into model.occupations
    std::vector<std::pair<std::size_t, double>> name_languages; // into model.languages, with weights
    std::size_t surnames{no_index};  // into model.surnames: the culture's
    std::size_t places{no_index};    // into model.places
    std::vector<std::pair<std::string, double>> tlds;
    std::vector<std::size_t> schemes; // into model.schemes, in drawing order
    std::optional<evidence_level> numbers_evidence; // none without a scheme
    evidence_level name_evidence{evidence_level::filled};
};

using isco_row = std::array<double, section_count>;
struct occupation_table
{
    std::array<isco_row, isco_groups> rows{};
    std::array<bool, isco_groups> present{};
};


// ------------------------------------------------------------------ text

// The code point at text[i]; `used` gets its length (1 for a byte that starts none, read as U+FFFD).
inline char32_t decode_utf8(std::string_view text, std::size_t i, std::size_t& used)
{
    const auto b0 = static_cast<unsigned char>(text[i]);
    used = 1;
    if (b0 < 0x80U) return b0;
    std::size_t len = 0;
    char32_t cp = 0;
    if ((b0 & 0xE0U) == 0xC0U) { len = 2; cp = b0 & 0x1FU; }
    else if ((b0 & 0xF0U) == 0xE0U) { len = 3; cp = b0 & 0x0FU; }
    else if ((b0 & 0xF8U) == 0xF0U) { len = 4; cp = b0 & 0x07U; }
    else return 0xFFFD;
    if (i + len > text.size()) return 0xFFFD;
    for (std::size_t k = 1; k < len; ++k)
    {
        const auto b = static_cast<unsigned char>(text[i + k]);
        if ((b & 0xC0U) != 0x80U) return 0xFFFD;
        cp = (cp << 6U) | (b & 0x3FU);
    }
    used = len;
    return cp;
}

// A letter of a script other than Latin: punctuation, symbols, CJK punctuation and full-width forms
// count for no script.
constexpr bool foreign_letter(char32_t cp) noexcept
{
    if (cp < 0x370) return false;
    if (cp >= 0x1E00 && cp <= 0x1EFF) return false; // Latin Extended Additional (Vietnamese)
    if (cp >= 0x2000 && cp <= 0x2BFF) return false; // punctuation, symbols, enclosed forms
    if (cp >= 0x3000 && cp <= 0x303F) return false; // CJK punctuation
    if (cp >= 0xFF00 && cp <= 0xFF20) return false; // full-width punctuation
    return true;
}

// Whether text is written in Latin letters (or has no letters at all).
inline bool latin_text(std::string_view text)
{
    for (std::size_t i = 0; i < text.size();)
    {
        std::size_t used = 1;
        const char32_t cp = decode_utf8(text, i, used);
        if (foreign_letter(cp)) return false;
        i += used;
    }
    return true;
}

// The text before the first and after the last occurrence of `sep`-separated items.
inline std::vector<std::string_view> split_view(std::string_view text, char sep)
{
    std::vector<std::string_view> out;
    std::size_t start = 0;
    while (true)
    {
        const auto at = text.find(sep, start);
        out.push_back(text.substr(start, at == std::string_view::npos ? std::string_view::npos : at - start));
        if (at == std::string_view::npos) break;
        start = at + 1;
    }
    return out;
}

inline bool all_digits(std::string_view s) noexcept
{
    return !s.empty() && std::ranges::all_of(s, [](char c) { return c >= '0' && c <= '9'; });
}

inline std::uint64_t to_number(std::string_view s)
{
    std::uint64_t v = 0;
    const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    if (ec != std::errc{} || ptr != s.data() + s.size()) throw std::invalid_argument("not a number: '" + std::string{s} + "'");
    return v;
}

inline double to_double(std::string_view s)
{
    const auto v = core::detail::parse_decimal_fast(s);
    if (!v) throw std::invalid_argument("not a number: '" + std::string{s} + "'");
    return *v;
}

inline std::string zero_pad(std::uint64_t v, std::size_t width)
{
    std::string s = std::to_string(v);
    if (s.size() < width) s.insert(0, width - s.size(), '0');
    return s;
}

// ------------------------------------------------------------------ check digits

// How a scheme's check characters are computed (schemes.tsv, column check).
struct check_rule
{
    enum class kind : std::uint8_t { none, unpublished, luhn, gtin, mod11_10, mod97_10, weights, named };
    enum class base : std::uint8_t { eleven_minus_r, r, r_mod_10, eleven_minus_r_mod_10, ten_minus_s, ten_minus_s_mod_10 };
    kind type{kind::none};
    std::string name;  // a named algorithm
    std::vector<unsigned> weights;
    base rule{base::r};
    std::vector<std::pair<int, std::string>> except; // a result and how it is written ("" for redraw)
};

inline check_rule parse_check(std::string_view text)
{
    using k = check_rule::kind;
    using b = check_rule::base;
    check_rule c;
    if (text.empty() || text == "none") return c;
    if (text == "unpublished") { c.type = k::unpublished; return c; }
    if (text == "luhn") { c.type = k::luhn; return c; }
    if (text == "gtin") { c.type = k::gtin; return c; }
    if (text == "mod11_10") { c.type = k::mod11_10; return c; }
    if (text == "mod97_10") { c.type = k::mod97_10; return c; }
    if (!text.starts_with("weights:"))
    {
        c.type = k::named;
        c.name = std::string{text};
        return c;
    }
    const auto rest = text.substr(8);
    const auto colon = rest.find(':');
    if (colon == std::string_view::npos) throw std::invalid_argument("weights need a rule");
    for (const auto w : split_view(rest.substr(0, colon), ' ')) c.weights.push_back(static_cast<unsigned>(to_number(w)));
    const auto parts = split_view(rest.substr(colon + 1), ',');
    const auto rule = parts.front();
    if (rule == "11-r") c.rule = b::eleven_minus_r;
    else if (rule == "r") c.rule = b::r;
    else if (rule == "r%10") c.rule = b::r_mod_10;
    else if (rule == "(11-r)%10") c.rule = b::eleven_minus_r_mod_10;
    else if (rule == "10-s%10") c.rule = b::ten_minus_s;
    else if (rule == "(10-s%10)%10") c.rule = b::ten_minus_s_mod_10;
    else throw std::invalid_argument("unknown check rule '" + std::string{rule} + "'");
    for (std::size_t i = 1; i < parts.size(); ++i)
    {
        const auto eq = parts[i].find('=');
        if (eq == std::string_view::npos) throw std::invalid_argument("a check exception needs a=b");
        const auto to = parts[i].substr(eq + 1);
        c.except.emplace_back(static_cast<int>(to_number(parts[i].substr(0, eq))), to == "redraw" ? std::string{} : std::string{to});
    }
    c.type = k::weights;
    return c;
}

inline unsigned luhn_checksum(std::string_view digits)
{
    unsigned sum = 0;
    for (std::size_t i = 0; i < digits.size(); ++i)
    {
        unsigned d = static_cast<unsigned>(digits[digits.size() - 1 - i] - '0');
        if (i % 2 == 1)
        {
            d *= 2;
            if (d > 9) d -= 9;
        }
        sum += d;
    }
    return sum % 10;
}

// The check characters of a body of digits under a generic rule; nullopt when no number has this body.
inline std::optional<std::string> compute_check(const check_rule& c, std::string_view body)
{
    using k = check_rule::kind;
    using b = check_rule::base;
    switch (c.type)
    {
    case k::none:
    case k::unpublished: return std::string{};
    case k::luhn: return std::string(1, core::luhn_digit(body));
    case k::gtin: return std::string(1, core::gtin_digit(body));
    case k::mod11_10: return std::string(1, core::mod11_10_digit(body));
    case k::mod97_10:
    {
        const auto d = core::mod97_10_digits(body);
        return std::string{d[0], d[1]};
    }
    case k::weights:
    {
        std::uint64_t s = 0;
        for (std::size_t i = 0; i < body.size() && i < c.weights.size(); ++i)
        {
            s += static_cast<std::uint64_t>(body[i] - '0') * c.weights[i];
        }
        const int r = static_cast<int>(s % 11);
        const int s10 = static_cast<int>(s % 10);
        int v = 0;
        switch (c.rule)
        {
        case b::eleven_minus_r: v = 11 - r; break;
        case b::r: v = r; break;
        case b::r_mod_10: v = r % 10; break;
        case b::eleven_minus_r_mod_10: v = (11 - r) % 10; break;
        case b::ten_minus_s: v = 10 - s10; break;
        case b::ten_minus_s_mod_10: v = (10 - s10) % 10; break;
        }
        for (const auto& [from, to] : c.except)
        {
            if (from == v)
            {
                if (to.empty()) return std::nullopt;
                return to;
            }
        }
        if (v > 9) return std::nullopt;
        return std::string(1, static_cast<char>('0' + v));
    }
    case k::named:
    {
        if (c.name == "at_uid") return std::string(1, static_cast<char>('0' + (16 - luhn_checksum(body)) % 10));
        if (c.name == "be_kbo") return zero_pad(97 - to_number(body) % 97, 2);
        if (c.name == "ro_cui")
        {
            constexpr std::array<unsigned, 9> w{7, 5, 3, 2, 1, 7, 5, 3, 2};
            const std::string padded = zero_pad(to_number(body), 9);
            std::uint64_t s = 0;
            for (std::size_t i = 0; i < 9; ++i) s += static_cast<std::uint64_t>(padded[i] - '0') * w.at(i);
            return std::string(1, static_cast<char>('0' + (10 * s) % 11 % 10));
        }
        if (c.name == "tr_vkn")
        {
            unsigned s = 0;
            for (std::size_t i = 0; i < 9; ++i)
            {
                const unsigned c1 = (static_cast<unsigned>(body[i] - '0') + 9 - static_cast<unsigned>(i)) % 10;
                unsigned c2 = (c1 * (1U << (9 - i))) % 9;
                if (c1 != 0 && c2 == 0) c2 = 9;
                s += c2;
            }
            return std::string(1, static_cast<char>('0' + (10 - s % 10) % 10));
        }
        if (c.name == "tw_ubn")
        {
            // the digit that makes the sum of the digits of the weighted products a multiple of 10
            constexpr std::array<unsigned, 7> w{1, 2, 1, 2, 1, 2, 4};
            unsigned s = 0;
            for (std::size_t i = 0; i < 7; ++i)
            {
                const unsigned p = static_cast<unsigned>(body[i] - '0') * w.at(i);
                s += p / 10 + p % 10;
            }
            return std::string(1, static_cast<char>('0' + (10 - s % 10) % 10));
        }
        if (c.name == "ua_edrpou")
        {
            std::array<unsigned, 7> w{1, 2, 3, 4, 5, 6, 7};
            if (body[0] == '3' || body[0] == '4' || body[0] == '5') w = {7, 1, 2, 3, 4, 5, 6};
            const auto total = [&]
            {
                unsigned s = 0;
                for (std::size_t i = 0; i < 7; ++i) s += static_cast<unsigned>(body[i] - '0') * w.at(i);
                return s;
            };
            unsigned s = total();
            if (s % 11 < 10) return std::string(1, static_cast<char>('0' + s % 11));
            for (auto& x : w) x += 2;
            s = total();
            return std::string(1, static_cast<char>('0' + s % 11 % 10));
        }
        throw std::invalid_argument("unknown check '" + c.name + "'");
    }
    }
    return std::string{};
}

// Another character of the same kind: a digit for a digit, a letter for a letter (the case kept).
inline char other_char(char c, core::rng& r)
{
    const auto step = core::uniform_int<int>(r, 1, c >= '0' && c <= '9' ? 9 : 25);
    if (c >= '0' && c <= '9') return static_cast<char>('0' + (c - '0' + step) % 10);
    if (c >= 'a' && c <= 'z') return static_cast<char>('a' + (c - 'a' + step) % 26);
    if (c >= 'A' && c <= 'Z') return static_cast<char>('A' + (c - 'A' + step) % 26);
    return c;
}

// ------------------------------------------------------------------ names and numbers

inline constexpr std::size_t pattern_count = 6;
inline constexpr std::array<std::string_view, pattern_count> pattern_keys{"founder", "founder_trade", "two_founders",
                                                                         "descriptive", "coined", "initials"};

// The parts a name template can ask for, as bits.
enum part : std::uint16_t
{
    p_surname = 1U << 0U, p_surname2 = 1U << 1U, p_trade = 1U << 2U, p_adjective = 1U << 3U,
    p_adjective2 = 1U << 4U, p_adjective3 = 1U << 5U, p_region = 1U << 6U, p_place = 1U << 7U,
    p_family = 1U << 8U, p_joiner = 1U << 9U, p_coined = 1U << 10U, p_coined_head = 1U << 11U,
    p_coined_tail = 1U << 12U, p_initials = 1U << 13U
};
inline constexpr std::array<std::pair<std::string_view, std::uint16_t>, 14> part_names{
    {{"surname", p_surname}, {"surname2", p_surname2}, {"trade", p_trade}, {"adjective", p_adjective},
     {"adjective2", p_adjective2}, {"adjective3", p_adjective3}, {"region", p_region}, {"place", p_place},
     {"family", p_family}, {"joiner", p_joiner}, {"coined", p_coined}, {"coined_head", p_coined_head},
     {"coined_tail", p_coined_tail}, {"initials", p_initials}}};

// A piece of a name template: literal text, or a part.
struct template_piece
{
    std::string text;
    std::uint16_t part{0};
};

struct name_template
{
    std::vector<template_piece> pieces;
    std::uint16_t needs{0};
    double weight{1.0};
};

// A word with its Latin form (empty: the word itself) and weight.
struct word_entry
{
    std::string word;
    std::string latin;
    double weight{1.0};
};

struct word_list
{
    std::vector<word_entry> items;
    core::weighted_table<std::uint32_t> table;
    bool latin{true}; // every word in Latin letters
    void add(word_entry e)
    {
        if (latin && !latin_text(e.word)) latin = false;
        table.add(static_cast<std::uint32_t>(items.size()), e.weight);
        items.push_back(std::move(e));
    }
    [[nodiscard]] bool empty() const { return items.empty() || !(table.total_weight() > 0.0); }
};

struct language_model
{
    std::string code;
    std::string script{"Latn"};
    bool latin{true};
    int form_position{0}; // 0 suffix, 1 prefix, 2 either
    bool spaced{true};
    std::string coined_join;
    std::string quote_open;
    std::string quote_close;
    std::array<std::vector<name_template>, pattern_count> templates;
    std::unordered_map<std::string, word_list> trade; // by division or section letter
    word_list adjective, family, joiner, coined_head, coined_tail, surname;
    word_list family_sole; // the family words a sole trader may use (key *; key company names partners)
    std::unordered_map<std::string, word_list> region; // by country or "*"
    std::unordered_map<std::string, word_list> place;  // by country or "*"
};

// A code list (codes.tsv).
struct code_list
{
    std::vector<std::string> code;
    std::vector<std::string> label;
    std::vector<std::string> extra;
    core::weighted_table<std::uint32_t> table;
};

// The issue anchors of one series: by year, the quantiles of the numbers issued.
struct anchor_series
{
    std::vector<int> years;
    std::vector<std::array<double, 7>> points;
    std::optional<std::uint64_t> highest;
};
inline constexpr std::array<double, 7> anchor_probs{0.001, 0.1, 0.25, 0.5, 0.75, 0.9, 0.999};

enum class safe_method : std::uint8_t { none, wrong_check, reserved_range, above_issued };

struct scheme_model
{
    std::string key;
    std::string country;
    int kind_order{0}; // register, statistical, tax, vat, lei
    std::string short_label;
    std::string builder;
    std::string display;
    std::optional<core::pattern> pattern;
    check_rule check;
    unsigned groups{0}; // a bit per legal-form group
    safe_method safe{safe_method::none};
    std::optional<std::chrono::sys_days> from;
    std::optional<std::chrono::sys_days> until;
    std::vector<std::pair<std::string, std::string>> params;
    evidence_level evidence{evidence_level::modelled};

    [[nodiscard]] std::string_view param(std::string_view name) const
    {
        for (const auto& [k, v] : params)
        {
            if (k == name) return v;
        }
        return {};
    }
};

// The value a mapping "K:V,K:V" gives the keys tried in turn; nullopt when none matches.
inline std::optional<std::string> map_lookup(std::string_view mapping, std::initializer_list<std::string_view> keys)
{
    const auto items = split_view(mapping, ',');
    for (const auto key : keys)
    {
        if (key.empty()) continue;
        for (const auto item : items)
        {
            const auto colon = item.find(':');
            if (colon != std::string_view::npos && item.substr(0, colon) == key) return std::string{item.substr(colon + 1)};
        }
    }
    return std::nullopt;
}

// One alternative of "a|b|c", drawn evenly; then a range "lo-hi" of numbers drawn evenly, zero-padded
// to the width of lo.
inline std::string draw_choice(std::string_view text, core::rng& r)
{
    const auto alts = split_view(text, '|');
    std::string_view pick = alts.front();
    if (alts.size() > 1) pick = alts.at(static_cast<std::size_t>(core::uniform_int<std::size_t>(r, 0, alts.size() - 1)));
    const auto dash = pick.find('-');
    if (dash != std::string_view::npos && dash > 0 && all_digits(pick.substr(0, dash)) && all_digits(pick.substr(dash + 1)))
    {
        const auto lo = to_number(pick.substr(0, dash));
        const auto hi = to_number(pick.substr(dash + 1));
        return zero_pad(core::uniform_int<std::uint64_t>(r, lo, std::max(lo, hi)), dash);
    }
    return std::string{pick};
}

// A number in a "lo-hi" range.
inline std::pair<std::uint64_t, std::uint64_t> parse_range(std::string_view text)
{
    const auto dash = text.find('-');
    if (dash == std::string_view::npos) throw std::invalid_argument("expected a range lo-hi, got '" + std::string{text} + "'");
    return {to_number(text.substr(0, dash)), to_number(text.substr(dash + 1))};
}

// The index in the compact form (the alphanumeric characters of the printed number) of each of the
// mask's `slots` slots; position k when the mask has another number of slots.
inline std::vector<std::size_t> slot_positions(std::string_view mask, std::size_t slots)
{
    std::vector<std::size_t> out;
    const auto n = static_cast<std::size_t>(std::ranges::count_if(mask, [](char c) { return c == '#' || c == '@'; }));
    if (mask.empty() || n != slots)
    {
        for (std::size_t k = 0; k < slots; ++k) out.push_back(k);
        return out;
    }
    std::size_t alnum = 0;
    for (const char c : mask)
    {
        if (c == '#' || c == '@') out.push_back(alnum++);
        else if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) ++alnum;
    }
    return out;
}

inline bool is_alnum(char c) noexcept
{
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

// The digits and letters of a printed number.
inline std::string alnum_of(std::string_view text)
{
    std::string out;
    for (const char c : text)
    {
        if (is_alnum(c)) out += c;
    }
    return out;
}

// Luhn over the alphabet 0-9A-Z (mod 36), as python-stdnum's luhn with that alphabet: the checksum.
inline unsigned luhn36_checksum(std::string_view s)
{
    unsigned sum = 0;
    for (std::size_t i = 0; i < s.size(); ++i)
    {
        const unsigned v = core::alnum_value(s[s.size() - 1 - i]);
        if (i % 2 == 0) sum += v;
        else sum += (v * 2) / 36 + (v * 2) % 36;
    }
    return sum % 36;
}

inline char luhn36_char(std::string_view body)
{
    constexpr std::string_view alphabet = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    const unsigned ck = luhn36_checksum(std::string{body} + "0");
    return alphabet[(36 - ck) % 36];
}

// Fills a display mask: each '#' or '@' takes the next character of `compact`; other characters are
// copied. A mask with another number of slots than `compact` has characters gives `compact` itself.
inline std::string apply_display(std::string_view mask, std::string_view compact)
{
    if (mask.empty()) return std::string{compact};
    const auto slots = static_cast<std::size_t>(std::ranges::count_if(mask, [](char c) { return c == '#' || c == '@'; }));
    if (slots != compact.size()) return std::string{compact};
    std::string out;
    std::size_t k = 0;
    for (const char c : mask)
    {
        if (c == '#' || c == '@') out += compact[k++];
        else out += c;
    }
    return out;
}

struct model
{
    std::vector<country_model> countries; // sorted by code
    std::unordered_map<std::string, std::uint32_t> index;
    core::weighted_table<std::size_t> by_enterprises;
    core::weighted_table<std::size_t> by_employment;
    core::weighted_table<std::size_t> uniform;
    std::vector<std::uint8_t> divisions;  // division numbers, ascending
    std::array<std::string, max_divisions> division_label{};
    std::array<std::string, section_count> section_label{};
    std::vector<size_cell> cells;
    std::array<std::vector<discrete_law>, 3> grid;
    std::vector<pareto_law> pareto;
    std::vector<std::array<double, age_class_count>> ages;
    std::array<double, old_ages> old_decay{}; // the 26+ class spread over ages 26 to max_age
    // The odds of each group: [section or 19 for any][band]; present when set.
    std::array<std::array<std::array<double, group_count>, band_count>, section_count + 1> odds{};
    std::array<std::array<bool, band_count>, section_count + 1> odds_present{};
    std::vector<occupation_table> occupations; // [0] is the pooled table (country "*")

    // Names, domains and numbers
    std::vector<language_model> languages;
    std::unordered_map<std::string, std::size_t> language_index;
    std::unordered_map<std::string, std::array<double, pattern_count>> patterns; // "language|group|band"
    std::vector<word_list> surnames;                                     // by culture
    std::unordered_map<std::string, std::size_t> surname_index;
    std::vector<word_list> places;                                       // by country
    std::vector<scheme_model> schemes;
    std::size_t lei{no_index};                                           // the LEI scheme
    std::unordered_map<std::string, code_list> codes;
    std::unordered_map<std::string, anchor_series> anchors;             // "scheme|series"
    std::unordered_map<char32_t, std::string> fold;
};

} // namespace companygen_detail
/// @endcond

/// @brief Company Generator: companies by country.
///
/// The call seed splits into the streams `"country"`, `"industry"`, `"size"`,
/// `"employees"`, `"form"`, `"founded"`, `"name"`, `"numbers"` (one split per
/// scheme), `"domain"` and `"lei"`. A given field skips its draw and
/// leaves every other stream's draws unchanged. Output is identical on every
/// compiler and platform (dasmig-core).
///
/// Can be used as a singleton via instance() or constructed independently.
///
/// @par Thread safety
/// Each instance is independent. Concurrent calls to get_company(request) on
/// the **same** instance require external synchronisation; the seeded
/// overloads are const and may run concurrently.
class cpg
{
  public:
    /// @brief Default constructor — creates an empty generator with no data.
    cpg() = default;

    cpg(const cpg&) = delete;                 ///< Not copyable.
    cpg& operator=(const cpg&) = delete;      ///< Not copyable.
    cpg(cpg&&) noexcept = default;            ///< Movable.
    cpg& operator=(cpg&&) noexcept = default; ///< Movable.
    ~cpg() = default;                         ///< Destructor.

    /// @brief Access the global singleton instance.
    ///
    /// Loads the `lite` tier (or `full` if `lite` is missing) on first
    /// access, from compiled-in data or an auto-probed `resources/`.
    /// @return The singleton.
    static cpg& instance()
    {
        static cpg inst{auto_probe_tag{}};
        return inst;
    }

    // -- Generation -------------------------------------------------------

    /// @brief Generate a random company.
    /// @param request What to generate; company_request{} for the defaults.
    /// @return The company.
    /// @throws std::runtime_error If no data has been loaded.
    /// @throws std::invalid_argument If the request is invalid.
    [[nodiscard]] company get_company(const company_request& request)
    {
        return get_company(request, draw_seed_());
    }

    /// @brief Generate a deterministic company.
    /// @param request What to generate.
    /// @param call_seed Seed; the same request, seed and data give the same
    ///        result. Does not touch the instance engine.
    /// @return The company.
    /// @throws std::runtime_error If no data has been loaded.
    /// @throws std::invalid_argument If the request is invalid: an unknown
    ///         country or ISIC code, a legal form the country does not have
    ///         or that is not yet in force at the reference date, an
    ///         unincorporated form with `incorporated_only`, a head count
    ///         outside the forced band, an occupation that is not ISCO-08,
    ///         an invalid reference date, or a combination with no weight (a
    ///         forced form barred from the forced industry).
    [[nodiscard]] company get_company(const company_request& request, std::uint64_t call_seed) const
    {
        namespace d = companygen_detail;
        require_data_();
        const plan p = plan_of_(request);
        const core::rng call{call_seed};

        const d::country_model& c = request.country ? country_or_throw_(*request.country)
                                                    : draw_country_(call.split("country"), request.weighted, p.employment);
        const std::optional<std::size_t> forced_form = forced_form_of_(c, request, p);

        auto evidence = d::evidence_level::measured;
        const std::size_t division = pick_division_(c, p, forced_form, call, evidence);
        const std::size_t section = d::section_of_division(static_cast<int>(division));
        const std::size_t band = pick_band_(c, p, division, section, forced_form, call, evidence);
        const std::uint32_t employees = request.employees ? *request.employees : draw_employees_(c, division, band, call);
        const std::size_t form = pick_form_(c, p, division, section, band, forced_form, call, evidence);
        const auto founded = draw_founded_(c, p, section, band, c.forms.at(form), call);
        d::weaken(evidence, c.age_evidence);

        const d::form_row& f = c.forms.at(form);
        const drawn_name name = draw_name_(c, request, f, division, section, band, call);
        number_context numbers{.c = c, .f = f, .band = band, .division = division, .section = section,
                               .employees = employees, .founded = founded, .p = p, .safe = request.safe, .name = name,
                               .numbers = call.split("numbers"), .lei = call.split("lei"), .chosen = {}, .built = {}};
        company out;
        out._signature = {.seed = call_seed, .data = data_hash()};
        out.country_code = c.code;
        out.isic = two_digits_(division);
        out.isic_label = _model.division_label.at(division);
        out.isic_section = static_cast<char>('A' + section);
        out.size = static_cast<size_band>(band);
        out.employees = employees;
        out.legal_form = f.abbreviation.empty() ? f.name : f.abbreviation;
        out.legal_form_code = f.code;
        out.legal_form_group = std::string{d::group_keys.at(f.group)};
        out.founded = founded;
        out.name = name.name;
        out.short_name = name.short_name;
        out.numbers = draw_numbers_(numbers);
        out.domain = draw_domain_(c, name, request.safe, call);
        out.evidence = d::evidence_name(evidence);
        return out;
    }

    // -- Batch generation -------------------------------------------------

    /// @brief Generate a batch of company results from a seed drawn like
    ///        get_company() draws its own.
    /// @param request What every item is generated from.
    /// @param options Count, first index, threads and uniqueness.
    /// @return The results, in index order.
    [[nodiscard]] std::vector<company> get_company_batch(const company_request& request,
                                                         const core::batch_options& options)
    {
        return get_company_batch(request, options, draw_seed_());
    }

    /// @brief Generate a batch of company results, deterministically.
    ///
    /// Item `i` is exactly `get_company(request, core::item_seed(batch_seed,
    /// options.first + i))`, so an item replays on its own, a range can be
    /// generated directly, two batches that follow each other equal one, and
    /// the output does not depend on `options.threads`.
    ///
    /// @param request What every item is generated from.
    /// @param options Count, first index, threads and uniqueness (see
    ///        core::unique_rule; the rule must be made for `company`).
    /// @param batch_seed Seed of the whole batch.
    /// @return The results, in index order.
    /// @throws std::invalid_argument If the request or options are invalid.
    /// @throws std::runtime_error If no data has been loaded, or a unique
    ///         batch runs out of attempts.
    [[nodiscard]] std::vector<company> get_company_batch(const company_request& request,
                                                         const core::batch_options& options,
                                                         std::uint64_t batch_seed) const
    {
        return core::run_batch(options, batch_seed,
                               [&](std::uint64_t item_seed) { return get_company(request, item_seed); });
    }

    /// @brief Stream a batch of company results to a sink without holding the
    ///        whole batch.
    ///
    /// Generates the same items as get_company_batch() and calls
    /// `sink(index, item)` in index order on the calling thread; `index`
    /// includes `options.first`.
    ///
    /// @tparam Sink Callable taking `(std::uint64_t, company&)`; for example
    ///         core::jsonl_writer or core::csv_writer.
    /// @param request What every item is generated from.
    /// @param options Count, first index, threads and uniqueness.
    /// @param batch_seed Seed of the whole batch.
    /// @param sink Receives the items.
    template <typename Sink>
    void for_each_company(const company_request& request, const core::batch_options& options,
                          std::uint64_t batch_seed, Sink&& sink) const
    {
        core::for_each_batch(options, batch_seed,
                             [&](std::uint64_t item_seed) { return get_company(request, item_seed); },
                             std::forward<Sink>(sink));
    }

    /// @brief Stream a batch of company results with a batch seed drawn like
    ///        get_company() draws its own.
    /// @tparam Sink Callable taking `(std::uint64_t, company&)`.
    /// @param request What every item is generated from.
    /// @param options Count, first index, threads and uniqueness.
    /// @param sink Receives the items.
    template <typename Sink>
    void for_each_company(const company_request& request, const core::batch_options& options, Sink&& sink)
    {
        for_each_company(request, options, draw_seed_(), std::forward<Sink>(sink));
    }

    // -- Queries ----------------------------------------------------------

    /// @brief The countries served.
    /// @return ISO alpha-2 codes, sorted.
    [[nodiscard]] std::vector<std::string> countries() const
    {
        std::vector<std::string> out;
        out.reserve(_model.countries.size());
        for (const auto& c : _model.countries) out.push_back(c.code);
        return out;
    }

    /// @brief The legal forms a country has.
    /// @param country ISO alpha-2, any case.
    /// @return The forms, in table order.
    /// @throws std::invalid_argument For an unknown country.
    [[nodiscard]] std::vector<legal_form_info> legal_forms(std::string_view country) const
    {
        namespace d = companygen_detail;
        std::vector<legal_form_info> out;
        for (const auto& f : country_or_throw_(country).forms)
        {
            legal_form_info info{.code = f.code, .abbreviation = f.abbreviation, .name = f.name,
                                 .group = std::string{d::group_keys.at(f.group)}};
            if (f.from) info.from = std::chrono::year_month_day{*f.from};
            if (f.until) info.until = std::chrono::year_month_day{*f.until};
            out.push_back(std::move(info));
        }
        return out;
    }

    /// @brief The ISIC Rev. 4 divisions served.
    /// @return Two-digit codes, ascending.
    [[nodiscard]] std::vector<std::string> divisions() const
    {
        std::vector<std::string> out;
        out.reserve(_model.divisions.size());
        for (const auto d : _model.divisions) out.push_back(two_digits_(d));
        return out;
    }

    /// @brief The evidence behind each field of a country's data.
    /// @param country ISO alpha-2, any case.
    /// @return Per field: measured, modelled or filled; numbers is "none" for
    ///         a country with no number schemes.
    /// @throws std::invalid_argument For an unknown country.
    [[nodiscard]] company_evidence evidence(std::string_view country) const
    {
        namespace d = companygen_detail;
        const auto& c = country_or_throw_(country);
        return {.industry = d::evidence_name(c.industry_evidence),
                .size = d::evidence_name(c.size_evidence),
                .legal_form = d::evidence_name(c.form_evidence),
                .age = d::evidence_name(c.age_evidence),
                .name = d::evidence_name(c.name_evidence),
                .numbers = c.numbers_evidence ? std::string{d::evidence_name(*c.numbers_evidence)} : std::string{"none"},
                .domain = d::evidence_name(c.domain_evidence)};
    }

    /// @brief Whether a number passes its scheme's check, as python-stdnum
    ///        checks it (for a scheme without a check: its structure).
    ///
    /// Cannot tell whether a number is in use.
    /// @param scheme A scheme id, such as `"fr-siren"` or `"lei"`.
    /// @param number The number, printed (`"552 008 443"`) or compact.
    /// @return True when it passes.
    /// @throws std::invalid_argument For an unknown scheme.
    [[nodiscard]] bool is_valid(std::string_view scheme, std::string_view number) const
    {
        for (const auto& s : _model.schemes)
        {
            if (s.key != scheme) continue;
            return number_valid_(s, s.builder == "de_hrn" ? std::string{number} : companygen_detail::alnum_of(number));
        }
        throw std::invalid_argument("unknown number scheme '" + std::string{scheme} + "'");
    }

    // -- Data management --------------------------------------------------

    /// @brief Check whether any data has been loaded.
    /// @return True when the tables are loaded.
    [[nodiscard]] bool has_data() const { return !_model.countries.empty(); }

    /// @brief Load data from a resource directory, replacing all data and
    ///        layers loaded before.
    ///
    /// The directory must contain `countries.tsv`; the other tables are read
    /// when present and the ones the model draws from must, together, cover
    /// every country. A path without `countries.tsv` is ignored.
    ///
    /// @param dir Directory such as `resources/lite`.
    /// @throws core::tsv_error If a file is malformed; the data is then
    ///         unchanged.
    /// @throws std::invalid_argument If the tables do not fit together.
    void load(const std::filesystem::path& dir)
    {
        std::error_code ec;
        if (!std::filesystem::is_directory(dir, ec) ||
            !std::filesystem::is_regular_file(dir / file_name_(table::countries), ec))
        {
            return;
        }
        apply_(read_directory_(dir), core::layer_mode::replace, true);
    }

    /// @brief Load a dataset tier, replacing all data and layers loaded
    ///        before.
    ///
    /// Uses the compiled-in copy when the library is built with
    /// `DASMIG_COMPANY_EMBED`, otherwise searches `resources/`, `../resources`,
    /// `company-generator/resources` and `../company-generator/resources`.
    ///
    /// @param tier The tier.
    /// @return False when the tier was not found.
    /// @throws core::tsv_error If a file is malformed.
    [[nodiscard]] bool load(dataset tier)
    {
        const std::string tier_dir{core::tier_name(tier)};
        const auto marker = tier_dir + "/" + std::string{file_name_(table::countries)};
        const auto root = core::find_resources(library_name_, marker);
        documents docs;
        for (const auto t : all_tables_)
        {
            docs.at(index_(t)) = core::load_resource(tier_dir + "/" + std::string{file_name_(t)}, embedded_files_(), root);
        }
        if (!docs.at(index_(table::countries))) return false;
        apply_(std::move(docs), core::layer_mode::replace, true);
        return true;
    }

    /// @brief Add data layers from a directory or a file.
    ///
    /// A directory may hold any of the table files; all are applied together
    /// or, if any fails, none. A file must have one of those names.
    ///
    /// @param path Directory or file.
    /// @param mode How the layers combine with the data loaded before.
    /// @throws core::tsv_error If a file cannot be read or a row is invalid;
    ///         the data is then unchanged.
    /// @throws std::invalid_argument If @p path is a file with another name,
    ///         or the tables do not fit together.
    void extend(const std::filesystem::path& path, core::layer_mode mode = core::layer_mode::override)
    {
        std::error_code ec;
        if (std::filesystem::is_directory(path, ec))
        {
            apply_(read_directory_(path), mode, false);
            return;
        }
        for (const auto t : all_tables_)
        {
            if (path.filename() == file_name_(t))
            {
                (void)extend(t, core::tsv_document::load(path), mode);
                return;
            }
        }
        throw std::invalid_argument("extend: expected one of the company tables (countries.tsv, sizes.tsv, ...), got " +
                                    path.filename().string());
    }

    /// @brief Add a data layer to one table.
    ///
    /// Rows are keyed by the table's key columns (see doc/data.md). In the
    /// default override mode a row replaces the loaded row with the same key
    /// and new keys are added; a row with `disabled` set to 1 removes its key.
    ///
    /// @param which The table.
    /// @param doc TSV with that table's columns.
    /// @param mode How the layer combines with the data loaded before.
    /// @return What the layer did (rows added, replaced, removed, warnings).
    /// @throws core::tsv_error If a row is invalid; the data is unchanged.
    /// @throws std::invalid_argument If the tables no longer fit together.
    const core::layer_info& extend(company_table which, core::tsv_document doc,
                                   core::layer_mode mode = core::layer_mode::override)
    {
        documents docs;
        docs.at(index_(which)).emplace(std::move(doc));
        apply_(std::move(docs), mode, false);
        return _tables.at(index_(which)).layers().back();
    }

    /// @brief Add a data layer to one table from TSV text (for example from
    ///        a WebAssembly host).
    /// @param which The table.
    /// @param text TSV with that table's columns.
    /// @param source Name used in errors.
    /// @param mode How the layer combines with the data loaded before.
    /// @return What the layer did.
    /// @throws core::tsv_error If a row is invalid; the data is unchanged.
    const core::layer_info& extend_from_string(company_table which, std::string_view text, std::string source,
                                               core::layer_mode mode = core::layer_mode::override)
    {
        return extend(which, core::tsv_document::parse(text, std::move(source)), mode);
    }

    /// @brief The layers in effect for one table, in order.
    /// @param which The table.
    /// @return The layer records.
    [[nodiscard]] const std::vector<core::layer_info>& layers(company_table which) const
    {
        return _tables.at(index_(which)).layers();
    }

    /// @brief Hash of the loaded data, recorded in each result's signature().
    /// @return A hash that changes when any table does; 0 when nothing is
    ///         loaded.
    [[nodiscard]] std::uint64_t data_hash() const
    {
        std::uint64_t h = 0;
        bool any = false;
        for (const auto& t : _tables)
        {
            any = any || !t.layers().empty();
            h = core::hash_combine(h, t.data_hash());
        }
        return any ? h : 0;
    }

  private:
    using table = company_table;
    using country_model = companygen_detail::country_model;
    using evidence_level = companygen_detail::evidence_level;
    using form_row = companygen_detail::form_row;

    // -- Tables -----------------------------------------------------------

    static constexpr std::string_view library_name_ = "company-generator";
    static constexpr std::size_t table_count_ = 18;
    static constexpr std::array<table, table_count_> all_tables_{
        table::countries, table::isic,        table::industries, table::sizes,   table::ages,    table::groups,
        table::form_odds, table::legal_forms, table::occupations, table::words,  table::patterns, table::surnames,
        table::places,    table::domains,     table::schemes,    table::anchors, table::codes,   table::fold};

    using documents = std::array<std::optional<core::tsv_document>, table_count_>;
    using tables = std::array<core::layered_table, table_count_>;

    static constexpr std::size_t index_(table t) { return static_cast<std::size_t>(t); }

    static constexpr std::string_view file_name_(table t)
    {
        constexpr std::array<std::string_view, table_count_> names{
            "countries.tsv",   "isic.tsv",    "industries.tsv", "sizes.tsv",  "ages.tsv",     "groups.tsv",
            "form_odds.tsv",   "legal_forms.tsv", "occupations.tsv", "words.tsv", "patterns.tsv", "surnames.tsv",
            "places.tsv",      "domains.tsv", "schemes.tsv",    "anchors.tsv", "codes.tsv",    "fold.tsv"};
        return names.at(index_(t));
    }

    static std::vector<std::string> section_letters_()
    {
        std::vector<std::string> out;
        for (std::size_t s = 0; s < companygen_detail::section_count; ++s) out.emplace_back(1, static_cast<char>('A' + s));
        return out;
    }

    static core::table_schema schema_(table t)
    {
        switch (t)
        {
        case table::countries:
            return {.key = {"country_code"},
                    .required = {"enterprises", "persons_employed", "languages", "culture", "industry_evidence",
                                 "employment_evidence", "size_evidence", "form_evidence", "age_evidence",
                                 "young_share", "domain_evidence"},
                    .optional = {"industry_source", "employment_source", "size_source", "form_source", "age_source"}};
        case table::isic:
            return {.key = {"code"}, .required = {"label"}, .optional = {}};
        case table::industries:
            return {.key = {"country_code", "division"}, .required = {"enterprises", "employment"}, .optional = {}};
        case table::sizes:
            return {.key = {"country_code", "isic", "band"},
                    .required = {"enterprise_share", "employment_share", "mean_persons"},
                    .optional = {}};
        case table::ages:
        {
            std::vector<std::string> required;
            for (const auto c : companygen_detail::age_columns) required.emplace_back(c);
            return {.key = {"country_code", "section", "band"}, .required = std::move(required), .optional = {}};
        }
        case table::groups:
            return {.key = {"country_code", "section"},
                    .required = {"sole_proprietorship", "partnership", "limited_liability", "other"},
                    .optional = {"evidence"}};
        case table::form_odds:
            return {.key = {"section", "band"},
                    .required = {"sole_proprietorship", "partnership", "limited_liability", "other"},
                    .optional = {}};
        case table::legal_forms:
            return {.key = {"country_code", "key"},
                    .required = {"code", "group", "abbreviation", "latin_abbreviation", "name", "latin_name", "from",
                                 "until", "only", "not", "share_small", "share_large", "evidence"},
                    .optional = {"in_name"}};
        case table::occupations:
            return {.key = {"country_code", "isco"}, .required = section_letters_(), .optional = {}};
        case table::words:
            return {.key = {"language", "kind", "key", "word"}, .required = {"latin", "weight"}, .optional = {}};
        case table::patterns:
            return {.key = {"language", "group", "band"},
                    .required = {"founder", "founder_trade", "two_founders", "descriptive", "coined", "initials"},
                    .optional = {}};
        case table::surnames:
            return {.key = {"culture", "surname"}, .required = {"latin", "weight"}, .optional = {}};
        case table::places:
            return {.key = {"country_code", "place"}, .required = {"weight"}, .optional = {}};
        case table::domains:
            return {.key = {"country_code", "tld"}, .required = {"share"}, .optional = {}};
        case table::schemes:
            return {.key = {"key"},
                    .required = {"country_code", "kind", "label", "native_label", "short", "builder", "display",
                                 "pattern", "check", "groups", "safe_method", "from", "until", "params", "evidence"},
                    .optional = {"source"}};
        case table::anchors:
            return {.key = {"scheme", "series", "year"},
                    .required = {"q001", "q10", "q25", "q50", "q75", "q90", "q999", "count"},
                    .optional = {"source"}};
        case table::codes:
            return {.key = {"list", "code"}, .required = {"label", "weight", "extra"}, .optional = {}};
        case table::fold:
            return {.key = {"from"}, .required = {"to"}, .optional = {}};
        }
        return {};
    }

    static tables make_tables_()
    {
        return {core::layered_table{schema_(table::countries)},   core::layered_table{schema_(table::isic)},
                core::layered_table{schema_(table::industries)},  core::layered_table{schema_(table::sizes)},
                core::layered_table{schema_(table::ages)},        core::layered_table{schema_(table::groups)},
                core::layered_table{schema_(table::form_odds)},   core::layered_table{schema_(table::legal_forms)},
                core::layered_table{schema_(table::occupations)}, core::layered_table{schema_(table::words)},
                core::layered_table{schema_(table::patterns)},    core::layered_table{schema_(table::surnames)},
                core::layered_table{schema_(table::places)},      core::layered_table{schema_(table::domains)},
                core::layered_table{schema_(table::schemes)},     core::layered_table{schema_(table::anchors)},
                core::layered_table{schema_(table::codes)},       core::layered_table{schema_(table::fold)}};
    }

    static documents read_directory_(const std::filesystem::path& dir)
    {
        documents docs;
        for (const auto t : all_tables_)
        {
            const auto file = dir / file_name_(t);
            std::error_code ec;
            if (std::filesystem::is_regular_file(file, ec)) docs.at(index_(t)) = core::tsv_document::load(file);
        }
        return docs;
    }

    void apply_(documents docs, core::layer_mode mode, bool reset)
    {
        tables next = _tables;
        for (const auto t : all_tables_)
        {
            auto& doc = docs.at(index_(t));
            auto& tbl = next.at(index_(t));
            if (!doc)
            {
                if (reset) tbl.clear();
                continue;
            }
            (void)tbl.extend(std::move(*doc), mode, [t](const core::tsv_row& row) { validate_(t, row); });
        }
        auto built = build_model_(next);
        _tables = std::move(next);
        _model = std::move(built);
    }

    // -- Row checks -------------------------------------------------------

    static double number_(const core::tsv_row& row, std::string_view column)
    {
        const auto v = row.get<double>(column);
        if (!std::isfinite(v) || v < 0.0) throw row.error(column, "must be a non-negative number");
        return v;
    }

    static void check_evidence_(const core::tsv_row& row, std::string_view column)
    {
        try
        {
            (void)companygen_detail::parse_evidence(row[column]);
        }
        catch (const std::invalid_argument& e)
        {
            throw row.error(column, e.what());
        }
    }

    static std::size_t section_letter_(const core::tsv_row& row, std::string_view column)
    {
        const auto text = row[column];
        const auto s = text.size() == 1 ? companygen_detail::section_of_letter(text[0]) : companygen_detail::no_index;
        if (s == companygen_detail::no_index) throw row.error(column, "must be an ISIC section letter A to S");
        return s;
    }

    static std::size_t band_(const core::tsv_row& row, std::string_view column)
    {
        const auto b = companygen_detail::index_of(companygen_detail::band_keys, row[column]);
        if (!b) throw row.error(column, "must be micro, small, medium or large");
        return *b;
    }

    static std::size_t group_(const core::tsv_row& row, std::string_view column)
    {
        const auto g = companygen_detail::index_of(companygen_detail::group_keys, row[column]);
        if (!g) throw row.error(column, "must be sole_proprietorship, partnership, limited_liability or other");
        return *g;
    }

    static void check_country_code_(const core::tsv_row& row, bool pooled_allowed)
    {
        const auto code = row["country_code"];
        if (pooled_allowed && code == "*") return;
        if (code.size() != 2 || code[0] < 'A' || code[0] > 'Z' || code[1] < 'A' || code[1] > 'Z')
        {
            throw row.error("country_code", "must be two capital letters");
        }
    }

    static void validate_(table t, const core::tsv_row& row)
    {
        namespace d = companygen_detail;
        switch (t)
        {
        case table::countries:
        {
            check_country_code_(row, false);
            (void)number_(row, "enterprises");
            (void)number_(row, "persons_employed");
            for (const auto c : {"industry_evidence", "employment_evidence", "size_evidence", "form_evidence",
                                 "age_evidence", "domain_evidence"})
            {
                check_evidence_(row, c);
            }
            if (!row["young_share"].empty() && number_(row, "young_share") > 1.0)
            {
                throw row.error("young_share", "must be 0 to 1");
            }
            break;
        }
        case table::isic:
        {
            const auto code = row["code"];
            const bool letter = code.size() == 1 && d::section_of_letter(code[0]) != d::no_index;
            const auto division = d::parse_division(code);
            if (!letter && !(division && d::section_of_division(*division) != d::no_index))
            {
                throw row.error("code", "must be a section letter A to S or a two-digit division of one");
            }
            if (row["label"].empty()) throw row.error("label", "must not be empty");
            break;
        }
        case table::industries:
        {
            check_country_code_(row, false);
            const auto division = d::parse_division(row["division"]);
            if (!division || d::section_of_division(*division) == d::no_index)
            {
                throw row.error("division", "must be a two-digit ISIC division");
            }
            (void)number_(row, "enterprises");
            (void)number_(row, "employment");
            break;
        }
        case table::sizes:
        {
            check_country_code_(row, true);
            const auto code = row["isic"];
            const bool letter = code.size() == 1 && d::section_of_letter(code[0]) != d::no_index;
            const auto division = d::parse_division(code);
            if (!letter && !(division && d::section_of_division(*division) != d::no_index))
            {
                throw row.error("isic", "must be a section letter A to S or a two-digit division");
            }
            (void)band_(row, "band");
            (void)number_(row, "enterprise_share");
            (void)number_(row, "employment_share");
            (void)number_(row, "mean_persons");
            break;
        }
        case table::ages:
        {
            check_country_code_(row, true);
            (void)section_letter_(row, "section");
            if (row["band"] != "*") (void)band_(row, "band");
            for (const auto c : d::age_columns) (void)number_(row, c);
            break;
        }
        case table::groups:
        {
            check_country_code_(row, true);
            if (row["section"] != "*") (void)section_letter_(row, "section");
            for (const auto c : d::group_keys) (void)number_(row, c);
            check_evidence_(row, "evidence");
            break;
        }
        case table::form_odds:
        {
            if (row["section"] != "*") (void)section_letter_(row, "section");
            (void)band_(row, "band");
            for (const auto c : d::group_keys) (void)number_(row, c);
            break;
        }
        case table::legal_forms:
        {
            check_country_code_(row, false);
            (void)group_(row, "group");
            (void)number_(row, "share_small");
            (void)number_(row, "share_large");
            check_evidence_(row, "evidence");
            (void)parse_form_(row);
            break;
        }
        case table::occupations:
        {
            check_country_code_(row, true);
            const auto isco = row.get<int>("isco");
            if (isco < 0 || std::cmp_greater(isco, d::isco_groups - 1)) throw row.error("isco", "must be 0 to 9");
            for (const auto& c : section_letters_()) (void)number_(row, c);
            break;
        }
        case table::words:
        case table::patterns:
        case table::surnames:
        case table::places:
        case table::domains:
        case table::schemes:
        case table::anchors:
        case table::codes:
        case table::fold:
            break;
        }
    }

    // A legal_forms row as a form_row (row errors located at the row).
    static form_row parse_form_(const core::tsv_row& row)
    {
        namespace d = companygen_detail;
        form_row f;
        f.key = std::string{row["key"]};
        f.code = std::string{row["code"]};
        f.abbreviation = std::string{row["abbreviation"]};
        f.latin_abbreviation = std::string{row["latin_abbreviation"]};
        f.name = std::string{row["name"]};
        f.latin_name = std::string{row["latin_name"]};
        f.in_name = std::string{row.has("in_name") ? row["in_name"] : std::string_view{}};
        if (f.name.empty() && f.abbreviation.empty()) throw row.error("name", "needs a name or an abbreviation");
        f.group = static_cast<std::uint8_t>(group_(row, "group"));
        const auto date = [&](std::string_view column, std::optional<std::chrono::sys_days>& slot)
        {
            if (row[column].empty()) return;
            slot = d::parse_date(row[column]);
            if (!slot) throw row.error(column, "must be a date YYYY-MM-DD");
        };
        date("from", f.from);
        date("until", f.until);
        if (f.from && f.until && *f.from > *f.until) throw row.error("until", "must not be before from");
        for (const auto& item : row.list("only", ','))
        {
            const auto s = item.size() == 1 ? d::section_of_letter(item[0]) : d::no_index;
            if (s == d::no_index) throw row.error("only", "must list ISIC section letters");
            f.only |= (1U << s);
        }
        for (const auto& item : row.list("not", ','))
        {
            const auto division = d::parse_division(item);
            const auto s = item.size() == 1 ? d::section_of_letter(item[0]) : d::no_index;
            if (s != d::no_index) f.barred_sections |= (1U << s);
            else if (division) f.barred_divisions.set(static_cast<std::size_t>(*division));
            else throw row.error("not", "must list ISIC section letters or divisions");
        }
        f.share_small = number_(row, "share_small");
        f.share_large = number_(row, "share_large");
        f.evidence = d::parse_evidence(row["evidence"]);
        return f;
    }

    // -- The model built from the tables ------------------------------------

    static std::invalid_argument bad_(const std::string& what) { return std::invalid_argument("company data: " + what); }

    // The head-count laws of the three finite bands, on a grid of exponents.
    static void build_grid_(companygen_detail::model& m)
    {
        namespace d = companygen_detail;
        for (std::size_t b = 0; b < 3; ++b)
        {
            const int lo = d::band_lo.at(b);
            const int hi = d::band_hi.at(b);
            std::vector<double> logs;
            for (int n = lo; n <= hi; ++n) logs.push_back(core::math::log(static_cast<double>(n)));
            for (std::size_t k = 0; k < d::grid_size; ++k)
            {
                d::discrete_law law;
                law.alpha = 0.1 * static_cast<double>(k);
                double total = 0.0;
                double weighted = 0.0;
                for (int n = lo; n <= hi; ++n)
                {
                    const double w = core::math::exp(-law.alpha * logs.at(static_cast<std::size_t>(n - lo)));
                    total += w;
                    weighted += w * n;
                    law.cdf.push_back(total);
                }
                law.mean = weighted / total;
                m.grid.at(b).push_back(std::move(law));
            }
        }
    }

    // The mixture of two neighbouring grid laws whose mean is `mean`.
    static companygen_detail::head_mix mix_for_(const companygen_detail::model& m, std::size_t band, double mean)
    {
        namespace d = companygen_detail;
        const auto& g = m.grid.at(band);
        d::head_mix h;
        if (!(mean < g.front().mean)) return h;
        if (!(mean > g.back().mean))
        {
            h.first = h.second = static_cast<std::uint32_t>(g.size() - 1);
            return h;
        }
        std::size_t i = 0;
        while (i + 2 < g.size() && g.at(i + 1).mean >= mean) ++i;
        h.first = static_cast<std::uint32_t>(i);
        h.second = static_cast<std::uint32_t>(i + 1);
        h.t = (g.at(i).mean - mean) / (g.at(i).mean - g.at(i + 1).mean);
        return h;
    }

    static companygen_detail::model build_model_(const tables& t)
    {
        namespace d = companygen_detail;
        d::model m;
        const auto& countries_table = t.at(index_(table::countries));
        if (countries_table.empty()) return m;
        for (const auto needed : {table::isic, table::industries, table::sizes, table::ages, table::groups,
                                  table::form_odds, table::legal_forms, table::occupations})
        {
            if (t.at(index_(needed)).empty()) throw bad_(std::string{file_name_(needed)} + " is empty or missing");
        }

        // ---- isic.tsv: the divisions and titles
        for (const auto& row : t.at(index_(table::isic)).rows())
        {
            const auto code = row["code"];
            if (code.size() == 1)
            {
                m.section_label.at(d::section_of_letter(code[0])) = std::string{row["label"]};
                continue;
            }
            const auto division = static_cast<std::size_t>(*d::parse_division(code));
            m.division_label.at(division) = std::string{row["label"]};
            m.divisions.push_back(static_cast<std::uint8_t>(division));
        }
        std::ranges::sort(m.divisions);
        if (m.divisions.empty()) throw bad_("isic.tsv has no divisions");
        const auto has_division = [&](std::size_t division) { return !m.division_label.at(division).empty(); };

        // ---- countries.tsv
        std::map<std::string, country_model> by_code;
        for (const auto& row : countries_table.rows())
        {
            country_model c;
            c.code = std::string{row["country_code"]};
            c.enterprises = row.get<double>("enterprises");
            c.persons = row.get<double>("persons_employed");
            c.languages = std::string{row["languages"]};
            c.culture = std::string{row["culture"]};
            c.industry_evidence = d::parse_evidence(row["industry_evidence"]);
            c.employment_evidence = d::parse_evidence(row["employment_evidence"]);
            c.size_evidence = d::parse_evidence(row["size_evidence"]);
            c.form_evidence = d::parse_evidence(row["form_evidence"]);
            c.age_evidence = d::parse_evidence(row["age_evidence"]);
            c.domain_evidence = d::parse_evidence(row["domain_evidence"]);
            if (!row["young_share"].empty()) c.young_share = row.get<double>("young_share");
            by_code.emplace(c.code, std::move(c));
        }

        // ---- industries.tsv
        for (const auto& row : t.at(index_(table::industries)).rows())
        {
            const auto it = by_code.find(std::string{row["country_code"]});
            if (it == by_code.end()) throw bad_("industries.tsv: unknown country " + std::string{row["country_code"]});
            const auto division = static_cast<std::size_t>(*d::parse_division(row["division"]));
            if (!has_division(division)) throw bad_("industries.tsv: division " + std::string{row["division"]} + " is not in isic.tsv");
            it->second.ent.at(division) = row.get<double>("enterprises");
            it->second.emp.at(division) = row.get<double>("employment");
        }
        for (auto& [code, c] : by_code)
        {
            double ent = 0.0;
            double emp = 0.0;
            for (const auto division : m.divisions)
            {
                ent += c.ent.at(division);
                emp += c.emp.at(division);
                c.section_emp.at(d::section_of_division(division)) += c.emp.at(division);
            }
            if (!(ent > 0.0) || !(emp > 0.0)) throw bad_("industries.tsv: " + code + " has no enterprises or no employment");
        }

        // ---- sizes.tsv: cells by (country or *, section or division)
        build_grid_(m);
        std::unordered_map<std::string, std::uint32_t> cell_of;
        for (const auto& row : t.at(index_(table::sizes)).rows())
        {
            const std::string key = std::string{row["country_code"]} + "|" + std::string{row["isic"]};
            auto [it, fresh] = cell_of.try_emplace(key, static_cast<std::uint32_t>(m.cells.size()));
            if (fresh) m.cells.emplace_back();
            auto& cell = m.cells.at(it->second);
            const auto b = band_(row, "band");
            cell.ent.at(b) = row.get<double>("enterprise_share");
            cell.emp.at(b) = row.get<double>("employment_share");
            cell.mean.at(b) = row.get<double>("mean_persons");
            cell.seen |= (1U << b);
        }
        for (auto& cell : m.cells)
        {
            if (cell.seen != 15U) continue; // checked where a country uses it
            // The mean head count of each band, as the laws below use it.
            std::array<double, d::band_count> mean = cell.mean;
            for (std::size_t b = 0; b < d::band_count; ++b)
            {
                const bool inside = mean.at(b) >= d::band_lo.at(b) && (b == 3 || mean.at(b) <= d::band_hi.at(b));
                if (!inside) mean.at(b) = d::band_default_mean.at(b);
            }
            const auto sum_of = [](const std::array<double, d::band_count>& shares)
            {
                double sum = 0.0;
                for (const auto s : shares) sum += s;
                return sum;
            };
            // A cell with shares of enterprises but none of persons employed (the UK register has no sizes of
            // persons) gets them from the mean head count of each band, and the reverse.
            if (!(sum_of(cell.emp) > 0.0))
            {
                for (std::size_t b = 0; b < d::band_count; ++b) cell.emp.at(b) = cell.ent.at(b) * mean.at(b);
            }
            if (!(sum_of(cell.ent) > 0.0))
            {
                for (std::size_t b = 0; b < d::band_count; ++b) cell.ent.at(b) = cell.emp.at(b) / mean.at(b);
            }
            for (auto* shares : {&cell.ent, &cell.emp})
            {
                const double sum = sum_of(*shares);
                if (sum > 0.0)
                {
                    for (auto& s : *shares) s /= sum;
                }
            }
            for (std::size_t b = 0; b < 3; ++b)
            {
                double mean = cell.mean.at(b);
                if (!(mean >= d::band_lo.at(b)) || mean > d::band_hi.at(b)) mean = d::band_default_mean.at(b);
                cell.head.at(b) = mix_for_(m, b, mean);
            }
        }
        const auto find_cell = [&](const std::string& country, const std::string& isic) -> std::optional<std::uint32_t>
        {
            const auto it = cell_of.find(country + "|" + isic);
            if (it == cell_of.end()) return std::nullopt;
            return it->second;
        };
        std::map<std::pair<double, double>, std::uint32_t> pareto_memo;
        for (auto& [code, c] : by_code)
        {
            // The largest employers of a country hold about 2% of its persons employed, within 1,000 and 100,000.
            const double cap = std::clamp(c.persons * 0.02, 1000.0, 100000.0);
            for (const auto division : m.divisions)
            {
                const std::string division_text = two_digits_(division);
                const std::string section_text(1, static_cast<char>('A' + d::section_of_division(division)));
                std::optional<std::uint32_t> id = find_cell(code, division_text);
                if (!id) id = find_cell(code, section_text);
                if (!id) id = find_cell("*", division_text);
                if (!id) id = find_cell("*", section_text);
                if (!id || m.cells.at(*id).seen != 15U)
                {
                    throw bad_("sizes.tsv: " + code + " division " + division_text + " has no complete size distribution");
                }
                c.cell.at(division) = *id;
                double mean = m.cells.at(*id).mean.at(3);
                if (!(mean >= d::band_lo.at(3))) mean = d::band_default_mean.at(3);
                const auto [it, fresh] = pareto_memo.try_emplace(std::pair<double, double>{mean, cap}, static_cast<std::uint32_t>(m.pareto.size()));
                if (fresh)
                {
                    const double lo = d::band_lo.at(3);
                    m.pareto.push_back({.alpha = d::pareto_alpha_for(mean, lo, cap), .lo = lo, .hi = cap});
                }
                c.large.at(division) = it->second;
            }
        }

        // ---- ages.tsv
        std::map<std::tuple<std::string, std::size_t, std::size_t>, std::uint32_t> age_of; // band 4: any
        for (const auto& row : t.at(index_(table::ages)).rows())
        {
            const std::size_t band = row["band"] == "*" ? d::band_count : band_(row, "band");
            std::array<double, d::age_class_count> shares{};
            double sum = 0.0;
            for (std::size_t i = 0; i < d::age_class_count; ++i)
            {
                shares.at(i) = row.get<double>(d::age_columns.at(i));
                sum += shares.at(i);
            }
            if (!(sum > 0.0)) throw bad_("ages.tsv: " + std::string{row["country_code"]} + " " + std::string{row["section"]} + " has no firms");
            for (auto& s : shares) s /= sum;
            age_of[{std::string{row["country_code"]}, section_letter_(row, "section"), band}] =
                static_cast<std::uint32_t>(m.ages.size());
            m.ages.push_back(shares);
        }
        for (auto& [code, c] : by_code)
        {
            for (std::size_t s = 0; s < d::section_count; ++s)
            {
                for (std::size_t b = 0; b < d::band_count; ++b)
                {
                    if (const auto own = age_of.find({code, s, b}); own != age_of.end())
                    {
                        c.age.at(s).at(b) = {.index = own->second, .scaled = false};
                    }
                    else if (const auto any = age_of.find({code, s, d::band_count}); any != age_of.end())
                    {
                        c.age.at(s).at(b) = {.index = any->second, .scaled = false};
                    }
                    else if (const auto pooled = age_of.find({"*", s, b}); pooled != age_of.end())
                    {
                        c.age.at(s).at(b) = {.index = pooled->second, .scaled = c.young_share && code != "US"};
                    }
                    else
                    {
                        throw bad_("ages.tsv: " + code + " has no ages for section " + std::string(1, static_cast<char>('A' + s)) +
                                   " " + std::string{d::band_keys.at(b)});
                    }
                }
            }
            fit_young_scale_(m, c);
        }
        double old_total = 0.0;
        for (std::size_t i = 0; i < d::old_ages; ++i)
        {
            m.old_decay.at(i) = core::math::exp(-static_cast<double>(i) / 25.0);
            old_total += m.old_decay.at(i);
        }
        for (auto& w : m.old_decay) w /= old_total;

        // ---- groups.tsv
        std::array<std::optional<std::array<double, d::group_count>>, d::section_count> pooled;
        std::map<std::string, std::array<double, d::group_count>> multiplier;
        std::map<std::string, std::array<std::optional<std::array<double, d::group_count>>, d::section_count>> own;
        for (const auto& row : t.at(index_(table::groups)).rows())
        {
            std::array<double, d::group_count> v{};
            for (std::size_t g = 0; g < d::group_count; ++g) v.at(g) = row.get<double>(d::group_keys.at(g));
            const std::string code{row["country_code"]};
            if (code != "*" && !by_code.contains(code)) throw bad_("groups.tsv: unknown country " + code);
            if (code == "*")
            {
                if (row["section"] != "*") pooled.at(section_letter_(row, "section")) = v;
            }
            else if (row["section"] == "*") multiplier[code] = v;
            else own[code].at(section_letter_(row, "section")) = v;
        }
        for (std::size_t s = 0; s < d::section_count; ++s)
        {
            if (!pooled.at(s)) throw bad_("groups.tsv: no pooled row for section " + std::string(1, static_cast<char>('A' + s)));
        }
        for (auto& [code, c] : by_code)
        {
            const auto mult = multiplier.find(code);
            const auto mine = own.find(code);
            for (std::size_t s = 0; s < d::section_count; ++s)
            {
                if (mine != own.end() && mine->second.at(s))
                {
                    c.base.at(0).at(s) = c.base.at(1).at(s) = *mine->second.at(s);
                    continue;
                }
                for (std::size_t g = 0; g < d::group_count; ++g)
                {
                    const double pooled_weight = pooled.at(s)->at(g);
                    c.base.at(0).at(s).at(g) = pooled_weight * (mult != multiplier.end() ? mult->second.at(g) : 1.0);
                    c.base.at(1).at(s).at(g) = pooled_weight;
                }
            }
        }

        // ---- form_odds.tsv
        for (const auto& row : t.at(index_(table::form_odds)).rows())
        {
            const std::size_t section = row["section"] == "*" ? d::section_count : section_letter_(row, "section");
            const std::size_t band = band_(row, "band");
            for (std::size_t g = 0; g < d::group_count; ++g) m.odds.at(section).at(band).at(g) = row.get<double>(d::group_keys.at(g));
            m.odds_present.at(section).at(band) = true;
        }
        for (std::size_t b = 0; b < d::band_count; ++b)
        {
            if (!m.odds_present.at(d::section_count).at(b)) throw bad_("form_odds.tsv: no row for section * " + std::string{d::band_keys.at(b)});
        }

        // ---- legal_forms.tsv
        for (const auto& row : t.at(index_(table::legal_forms)).rows())
        {
            const auto it = by_code.find(std::string{row["country_code"]});
            if (it == by_code.end()) throw bad_("legal_forms.tsv: unknown country " + std::string{row["country_code"]});
            it->second.forms.push_back(parse_form_(row));
        }
        for (const auto& [code, c] : by_code)
        {
            if (c.forms.empty()) throw bad_("legal_forms.tsv: " + code + " has no legal forms");
        }

        // ---- occupations.tsv: table 0 is the pooled one
        m.occupations.emplace_back();
        std::map<std::string, std::size_t> occupation_of{{"*", 0}};
        for (const auto& row : t.at(index_(table::occupations)).rows())
        {
            const std::string code{row["country_code"]};
            if (code != "*" && !by_code.contains(code)) throw bad_("occupations.tsv: unknown country " + code);
            auto [it, fresh] = occupation_of.try_emplace(code, m.occupations.size());
            if (fresh) m.occupations.emplace_back();
            auto& table_rows = m.occupations.at(it->second);
            const auto isco = static_cast<std::size_t>(row.get<int>("isco"));
            for (std::size_t s = 0; s < d::section_count; ++s) table_rows.rows.at(isco).at(s) = row.get<double>(std::string(1, static_cast<char>('A' + s)));
            table_rows.present.at(isco) = true;
        }
        for (auto& [code, c] : by_code)
        {
            if (const auto it = occupation_of.find(code); it != occupation_of.end()) c.occupation_table = it->second;
        }

        build_names_(t, m, by_code);
        build_numbers_(t, m, by_code);

        // ---- the countries, sorted by code, and their draw weights
        for (auto& [code, c] : by_code)
        {
            m.index.emplace(code, static_cast<std::uint32_t>(m.countries.size()));
            m.by_enterprises.add(m.countries.size(), c.enterprises);
            m.by_employment.add(m.countries.size(), c.persons);
            m.uniform.add(m.countries.size(), 1.0);
            m.countries.push_back(std::move(c));
        }
        return m;
    }

    // ---- names: words.tsv, patterns.tsv, surnames.tsv, places.tsv, domains.tsv, fold.tsv
    static companygen_detail::name_template parse_template_(std::string_view text, double weight)
    {
        namespace d = companygen_detail;
        d::name_template tpl;
        tpl.weight = weight;
        std::string literal;
        for (std::size_t i = 0; i < text.size();)
        {
            if (text[i] != '{')
            {
                literal += text[i++];
                continue;
            }
            const auto close = text.find('}', i);
            if (close == std::string_view::npos) throw bad_("words.tsv: unclosed { in the template '" + std::string{text} + "'");
            const auto name = text.substr(i + 1, close - i - 1);
            std::uint16_t bit = 0;
            for (const auto& [part_name, part_bit] : d::part_names)
            {
                if (part_name == name) bit = part_bit;
            }
            if (bit == 0) throw bad_("words.tsv: unknown part {" + std::string{name} + "} in the template '" + std::string{text} + "'");
            if (!literal.empty()) tpl.pieces.push_back({.text = std::exchange(literal, {}), .part = 0});
            tpl.pieces.push_back({.text = {}, .part = bit});
            tpl.needs |= bit;
            i = close + 1;
        }
        if (!literal.empty()) tpl.pieces.push_back({.text = literal, .part = 0});
        return tpl;
    }

    static void build_names_(const tables& t, companygen_detail::model& m, std::map<std::string, country_model>& by_code)
    {
        namespace d = companygen_detail;
        const auto language = [&](std::string_view code) -> d::language_model&
        {
            const auto [it, fresh] = m.language_index.try_emplace(std::string{code}, m.languages.size());
            if (fresh)
            {
                m.languages.emplace_back();
                m.languages.back().code = std::string{code};
            }
            return m.languages.at(it->second);
        };
        for (const auto& row : t.at(index_(table::words)).rows())
        {
            auto& lang = language(row["language"]);
            const auto kind = row["kind"];
            const std::string key{row["key"]};
            const std::string word{row["word"]};
            d::word_entry e{.word = word, .latin = std::string{row["latin"]}, .weight = row.get<double>("weight")};
            if (kind == "style")
            {
                if (key == "form_position") lang.form_position = word == "prefix" ? 1 : word == "both" ? 2 : 0;
                else if (key == "script")
                {
                    lang.script = word;
                    lang.latin = word == "Latn";
                }
                else if (key == "space") lang.spaced = word != "none";
                else if (key == "coined_join") lang.coined_join = word == "space" ? " " : "";
                else if (key == "quotes")
                {
                    std::size_t used = 1;
                    (void)d::decode_utf8(word, 0, used);
                    lang.quote_open = word.substr(0, used);
                    lang.quote_close = word.substr(used);
                }
            }
            else if (kind == "template")
            {
                const auto p = d::index_of(d::pattern_keys, key);
                if (!p) throw bad_("words.tsv: unknown pattern " + key);
                lang.templates.at(*p).push_back(parse_template_(word, e.weight));
            }
            else if (kind == "trade") lang.trade[key].add(std::move(e));
            else if (kind == "adjective") lang.adjective.add(std::move(e));
            else if (kind == "family")
            {
                if (key != "*" && key != "company") throw bad_("words.tsv: family key " + key + " (must be * or company)");
                if (key == "*") lang.family_sole.add(e);
                lang.family.add(std::move(e));
            }
            else if (kind == "joiner") lang.joiner.add(std::move(e));
            else if (kind == "coined_head") lang.coined_head.add(std::move(e));
            else if (kind == "coined_tail") lang.coined_tail.add(std::move(e));
            else if (kind == "surname") lang.surname.add(std::move(e));
            else if (kind == "region") lang.region[key].add(std::move(e));
            else if (kind == "place") lang.place[key].add(std::move(e));
            else throw bad_("words.tsv: unknown kind " + std::string{kind});
        }
        for (const auto& row : t.at(index_(table::patterns)).rows())
        {
            std::array<double, d::pattern_count> w{};
            for (std::size_t p = 0; p < d::pattern_count; ++p) w.at(p) = row.get<double>(d::pattern_keys.at(p));
            m.patterns[std::string{row["language"]} + "|" + std::string{row["group"]} + "|" + std::string{row["band"]}] = w;
        }
        for (const auto& row : t.at(index_(table::surnames)).rows())
        {
            const auto [it, fresh] = m.surname_index.try_emplace(std::string{row["culture"]}, m.surnames.size());
            if (fresh) m.surnames.emplace_back();
            m.surnames.at(it->second).add({.word = std::string{row["surname"]}, .latin = std::string{row["latin"]},
                                           .weight = row.get<double>("weight")});
        }
        std::unordered_map<std::string, std::size_t> place_index;
        for (const auto& row : t.at(index_(table::places)).rows())
        {
            const auto [it, fresh] = place_index.try_emplace(std::string{row["country_code"]}, m.places.size());
            if (fresh) m.places.emplace_back();
            m.places.at(it->second).add({.word = std::string{row["place"]}, .latin = {}, .weight = row.get<double>("weight")});
        }
        for (const auto& row : t.at(index_(table::domains)).rows())
        {
            const auto it = by_code.find(std::string{row["country_code"]});
            if (it == by_code.end()) throw bad_("domains.tsv: unknown country " + std::string{row["country_code"]});
            it->second.tlds.emplace_back(std::string{row["tld"]}, row.get<double>("share"));
        }
        for (const auto& row : t.at(index_(table::fold)).rows())
        {
            const auto from = row["from"];
            std::size_t used = 1;
            const char32_t cp = from.empty() ? 0 : d::decode_utf8(from, 0, used);
            if (from.empty() || used != from.size()) throw bad_("fold.tsv: '" + std::string{from} + "' is not one letter");
            m.fold[cp] = std::string{row["to"]};
        }
        const auto english = m.language_index.find("en");
        for (auto& [code, c] : by_code)
        {
            bool own = true;
            for (const auto item : d::split_view(c.languages, ';'))
            {
                if (item.empty()) continue;
                const auto colon = item.find(':');
                const std::string lang{item.substr(0, colon)};
                const double w = colon == std::string_view::npos ? 1.0 : d::to_double(item.substr(colon + 1));
                const auto it = m.language_index.find(lang);
                if (it == m.language_index.end())
                {
                    own = false;
                    continue;
                }
                if (w > 0.0) c.name_languages.emplace_back(it->second, w);
            }
            if (c.name_languages.empty())
            {
                if (english == m.language_index.end()) throw bad_("words.tsv: no language for " + code + "'s names");
                c.name_languages.emplace_back(english->second, 1.0);
                own = false;
            }
            c.name_evidence = own ? d::evidence_level::modelled : d::evidence_level::filled;
            if (const auto it = m.surname_index.find(c.culture); it != m.surname_index.end()) c.surnames = it->second;
            if (const auto it = place_index.find(code); it != place_index.end()) c.places = it->second;
        }
    }

    // ---- numbers: codes.tsv, anchors.tsv, schemes.tsv
    static void build_numbers_(const tables& t, companygen_detail::model& m, std::map<std::string, country_model>& by_code)
    {
        namespace d = companygen_detail;
        for (const auto& row : t.at(index_(table::codes)).rows())
        {
            auto& list = m.codes[std::string{row["list"]}];
            list.table.add(static_cast<std::uint32_t>(list.code.size()), row.get<double>("weight"));
            list.code.emplace_back(row["code"]);
            list.label.emplace_back(row["label"]);
            list.extra.emplace_back(row["extra"]);
        }
        for (const auto& row : t.at(index_(table::anchors)).rows())
        {
            auto& series = m.anchors[std::string{row["scheme"]} + "|" + std::string{row["series"]}];
            if (row["year"] == "highest")
            {
                series.highest = static_cast<std::uint64_t>(row.get<double>("q999"));
                continue;
            }
            std::array<double, 7> q{};
            constexpr std::array<std::string_view, 7> columns{"q001", "q10", "q25", "q50", "q75", "q90", "q999"};
            for (std::size_t i = 0; i < 7; ++i) q.at(i) = row.get<double>(columns.at(i));
            const int year = row.get<int>("year");
            const auto at = std::ranges::upper_bound(series.years, year) - series.years.begin();
            series.years.insert(series.years.begin() + at, year);
            series.points.insert(series.points.begin() + at, q);
        }
        constexpr std::array<std::string_view, 5> kinds{"register", "statistical", "tax", "vat", "lei"};
        for (const auto& row : t.at(index_(table::schemes)).rows())
        {
            d::scheme_model s;
            s.key = std::string{row["key"]};
            s.country = std::string{row["country_code"]};
            const auto kind = d::index_of(kinds, row["kind"]);
            if (!kind) throw bad_("schemes.tsv: " + s.key + ": unknown kind " + std::string{row["kind"]});
            s.kind_order = static_cast<int>(*kind);
            s.short_label = std::string{row["short"]};
            s.builder = std::string{row["builder"]};
            s.display = std::string{row["display"]};
            try
            {
                if (!row["pattern"].empty()) s.pattern = core::pattern::parse(row["pattern"]);
                s.check = d::parse_check(row["check"]);
                for (const auto item : d::split_view(row["params"], ';'))
                {
                    if (item.empty()) continue;
                    const auto eq = item.find('=');
                    if (eq == std::string_view::npos) throw std::invalid_argument("params need name=value");
                    s.params.emplace_back(std::string{item.substr(0, eq)}, std::string{item.substr(eq + 1)});
                }
            }
            catch (const std::invalid_argument& e)
            {
                throw bad_("schemes.tsv: " + s.key + ": " + e.what());
            }
            for (const char g : row["groups"])
            {
                const auto at = std::string_view{"SPLO"}.find(g);
                if (at == std::string_view::npos) throw bad_("schemes.tsv: " + s.key + ": groups must be letters of SPLO");
                s.groups |= 1U << at;
            }
            const auto safe = row["safe_method"];
            s.safe = safe == "wrong_check" ? d::safe_method::wrong_check
                     : safe == "reserved_range" ? d::safe_method::reserved_range
                     : safe == "above_issued" ? d::safe_method::above_issued
                                              : d::safe_method::none;
            const auto ev = row["evidence"];
            s.evidence = ev == "issuer" ? d::evidence_level::measured
                         : ev == "reference" ? d::evidence_level::modelled
                                             : d::evidence_level::filled;
            const auto date = [&](std::string_view column, std::optional<std::chrono::sys_days>& slot)
            {
                if (row[column].empty()) return;
                slot = d::parse_date(row[column]);
                if (!slot) throw bad_("schemes.tsv: " + s.key + ": " + std::string{column} + " must be a date");
            };
            date("from", s.from);
            date("until", s.until);
            if (!known_builder_(s.builder)) throw bad_("schemes.tsv: " + s.key + ": unknown builder " + s.builder);
            for (const auto list_param : {"court", "county", "region", "state"})
            {
                const auto v = s.param(list_param);
                if (!v.empty() && !m.codes.contains(std::string{v})) throw bad_("schemes.tsv: " + s.key + ": no code list " + std::string{v});
            }
            if (s.builder == "th_juristic" && !m.codes.contains(std::string{s.param("office")}))
            {
                throw bad_("schemes.tsv: " + s.key + ": no code list " + std::string{s.param("office")});
            }
            const std::size_t index = m.schemes.size();
            if (s.country == "*")
            {
                if (s.builder != "lei") throw bad_("schemes.tsv: " + s.key + ": only the LEI serves every country");
                m.lei = index;
            }
            else
            {
                const auto it = by_code.find(s.country);
                if (it == by_code.end()) throw bad_("schemes.tsv: unknown country " + s.country);
                it->second.schemes.push_back(index);
            }
            m.schemes.push_back(std::move(s));
        }
        for (auto& [code, c] : by_code)
        {
            std::ranges::sort(c.schemes, [&](std::size_t a, std::size_t b)
                              {
                                  const auto& x = m.schemes.at(a);
                                  const auto& y = m.schemes.at(b);
                                  return std::tie(x.kind_order, x.key) < std::tie(y.kind_order, y.key);
                              });
            for (const auto i : c.schemes)
            {
                const auto ev = m.schemes.at(i).evidence;
                if (!c.numbers_evidence) c.numbers_evidence = ev;
                else d::weaken(*c.numbers_evidence, ev);
            }
        }
    }

    static bool known_builder_(std::string_view b)
    {
        constexpr std::array<std::string_view, 37> names{
            "generic", "serial",  "lei",      "ar_cuit",  "at_fn",      "au_abn",     "br_cnpj",  "cn_uscc",
            "de_hrn",  "es_nif",  "fr_siret", "fr_tva",   "gb_crn",     "gb_vat",     "hu_adoszam", "id_npwp",
            "ie_vat",  "il_hp",   "in_cin",   "in_pan",   "in_gstin",   "it_piva",    "jp_cn",    "kr_brn",
            "mx_rfc",  "ng_rc",   "nl_btw",   "ro_onrc",  "ru_inn",     "ru_ogrn",    "se_orgnr", "sg_uen",
            "th_juristic", "us_ein", "vn_mst", "za_cipc", "fr_siren"};
        return std::ranges::find(names, b) != names.end();
    }

    static std::string two_digits_(std::size_t division)
    {
        return std::string{static_cast<char>('0' + division / 10), static_cast<char>('0' + division % 10)};
    }

    // Fit the factor on the young classes (ages 0 to 5) of the pooled age shape so that the country's
    // enterprises add up to its own share of firms aged up to five.
    static void fit_young_scale_(const companygen_detail::model& m, country_model& c)
    {
        namespace d = companygen_detail;
        if (!c.young_share) return;
        struct cell_weight
        {
            double weight;
            double young;
            bool scaled;
        };
        std::vector<cell_weight> cells;
        double total = 0.0;
        bool any_scaled = false;
        for (std::size_t s = 0; s < d::section_count; ++s)
        {
            for (std::size_t b = 0; b < d::band_count; ++b)
            {
                double weight = 0.0;
                for (std::size_t division = d::section_ranges.at(s).first;
                     std::cmp_less_equal(division, d::section_ranges.at(s).second); ++division)
                {
                    if (m.division_label.at(division).empty()) continue;
                    weight += c.ent.at(division) * m.cells.at(c.cell.at(division)).ent.at(b);
                }
                const auto& ref = c.age.at(s).at(b);
                double young = 0.0;
                for (std::size_t i = 0; i < 6; ++i) young += m.ages.at(ref.index).at(i);
                cells.push_back({weight, young, ref.scaled});
                total += weight;
                any_scaled = any_scaled || ref.scaled;
            }
        }
        if (!any_scaled || !(total > 0.0)) return;
        const auto share_at = [&](double scale)
        {
            double sum = 0.0;
            for (const auto& cw : cells)
            {
                const double y = cw.scaled ? scale * cw.young / (scale * cw.young + 1.0 - cw.young) : cw.young;
                sum += cw.weight * y;
            }
            return sum / total;
        };
        double lo = 1e-4;
        double hi = 1e4;
        const double target = std::min(*c.young_share, 0.98);
        if (share_at(lo) >= target) { c.young_scale = lo; return; }
        if (share_at(hi) <= target) { c.young_scale = hi; return; }
        for (int i = 0; i < 60; ++i)
        {
            const double mid = core::math::exp(0.5 * (core::math::log(lo) + core::math::log(hi)));
            if (share_at(mid) < target) lo = mid;
            else hi = mid;
        }
        c.young_scale = core::math::exp(0.5 * (core::math::log(lo) + core::math::log(hi)));
    }

    // -- Requests ------------------------------------------------------------

    // What a request fixes, checked against the data.
    struct plan
    {
        bool employment{false};
        std::optional<std::size_t> major;                   // ISCO-08 major group of the occupation
        std::size_t force_division{companygen_detail::no_index};
        std::size_t force_section{companygen_detail::no_index};
        std::size_t force_band{companygen_detail::no_index};
        bool incorporated_only{false};
        bool form_asked{false};                             // legal_form forced
        bool filtered{false};                               // form_asked or incorporated_only
        std::chrono::sys_days reference{};
        int reference_year{2025};
    };

    [[nodiscard]] plan plan_of_(const company_request& request) const
    {
        namespace d = companygen_detail;
        plan p;
        if (!request.reference_date.ok()) throw std::invalid_argument("company_request: invalid reference_date");
        p.reference_year = static_cast<int>(request.reference_date.year());
        if (p.reference_year < 1900 || p.reference_year > 2100)
        {
            throw std::invalid_argument("company_request: reference_date must be in 1900 to 2100");
        }
        p.reference = std::chrono::sys_days{request.reference_date};
        p.employment = request.weight == company_weight::employment;
        if (request.occupation_isco)
        {
            const auto& text = *request.occupation_isco;
            const bool digits = !text.empty() && text.size() <= 4 &&
                                std::ranges::all_of(text, [](char ch) { return ch >= '0' && ch <= '9'; });
            if (!digits) throw std::invalid_argument("company_request: occupation_isco '" + text + "' is not an ISCO-08 code");
            p.major = static_cast<std::size_t>(text[0] - '0');
            p.employment = true;
        }
        if (request.isic)
        {
            const auto& text = *request.isic;
            const auto division = d::parse_division(text);
            const auto section = text.size() == 1 ? d::section_of_letter(text[0] >= 'a' && text[0] <= 'z'
                                                                             ? static_cast<char>(text[0] - 'a' + 'A')
                                                                             : text[0])
                                                   : d::no_index;
            if (division && !_model.division_label.at(static_cast<std::size_t>(*division)).empty())
            {
                p.force_division = static_cast<std::size_t>(*division);
            }
            else if (section != d::no_index)
            {
                p.force_section = section;
            }
            else
            {
                throw std::invalid_argument("company_request: unknown isic '" + text + "'");
            }
        }
        if (request.size) p.force_band = static_cast<std::size_t>(*request.size);
        if (request.employees)
        {
            if (*request.employees == 0) throw std::invalid_argument("company_request: employees must be 1 or more");
            const std::size_t band = d::band_of(*request.employees);
            if (request.size && p.force_band != band)
            {
                throw std::invalid_argument("company_request: employees " + std::to_string(*request.employees) +
                                            " is outside size " + std::string{to_string(*request.size)});
            }
            p.force_band = band;
        }
        p.incorporated_only = request.incorporated_only;
        p.form_asked = request.legal_form.has_value();
        p.filtered = p.form_asked || p.incorporated_only;
        return p;
    }

    [[nodiscard]] const country_model& country_or_throw_(std::string_view code) const
    {
        require_data_();
        const auto it = _model.index.find(companygen_detail::upper(code));
        if (it == _model.index.end()) throw std::invalid_argument("unknown country '" + std::string{code} + "'");
        return _model.countries.at(it->second);
    }

    [[nodiscard]] const country_model& draw_country_(core::rng r, bool weighted, bool employment) const
    {
        const auto& weights = !weighted ? _model.uniform : employment ? _model.by_employment : _model.by_enterprises;
        return _model.countries.at(weights.pick(r));
    }

    // The form asked for, as an index into the country's forms.
    [[nodiscard]] static std::optional<std::size_t> forced_form_of_(const country_model& c, const company_request& request,
                                                                    const plan& p)
    {
        namespace d = companygen_detail;
        if (!request.legal_form) return std::nullopt;
        const auto& text = *request.legal_form;
        for (std::size_t i = 0; i < c.forms.size(); ++i)
        {
            const auto& f = c.forms.at(i);
            if (text.empty() || !(d::equal_nocase(f.code, text) || d::equal_nocase(f.abbreviation, text) ||
                                  d::equal_nocase(f.latin_abbreviation, text)))
            {
                continue;
            }
            if (f.from && *f.from > p.reference)
            {
                throw std::invalid_argument("company_request: legal_form '" + text + "' is not yet in force at the reference date");
            }
            if (p.incorporated_only && f.group < 2)
            {
                throw std::invalid_argument("company_request: legal_form '" + text + "' is not incorporated");
            }
            return i;
        }
        throw std::invalid_argument("company_request: " + c.code + " has no legal form '" + text + "'");
    }

    // The persons employed by occupation and section: the country's table, else the pooled one.
    [[nodiscard]] const companygen_detail::isco_row& occupation_row_(const country_model& c, std::size_t major) const
    {
        const auto& own = _model.occupations.at(c.occupation_table);
        if (own.present.at(major)) return own.rows.at(major);
        const auto& pooled = _model.occupations.front();
        if (pooled.present.at(major)) return pooled.rows.at(major);
        throw std::invalid_argument("company_request: no occupation data for ISCO major group " + std::to_string(major) +
                                    " in " + c.code);
    }

    // P(form | division, band) in `w`, over the country's forms: 0 for forms that cannot be had.
    void form_probs_(const country_model& c, const plan& p, std::size_t division, std::size_t section, std::size_t band,
                     std::vector<double>& w) const
    {
        namespace d = companygen_detail;
        const std::size_t n = c.forms.size();
        w.assign(n, 0.0);
        const bool large = band >= 2;
        std::array<double, d::group_count> share_sum{};
        // Level 0 applies every constraint; levels 1 and 2 relax them for a cell where no form is left.
        for (int level = 0; level < 3; ++level)
        {
            share_sum.fill(0.0);
            for (std::size_t i = 0; i < n; ++i)
            {
                const auto& f = c.forms.at(i);
                bool ok = true;
                if (level < 2 && f.from && *f.from > p.reference) ok = false;
                if (level < 1 && ok)
                {
                    if (f.only != 0 && ((f.only >> section) & 1U) == 0) ok = false;
                    if (((f.barred_sections >> section) & 1U) != 0 || f.barred_divisions.test(division)) ok = false;
                }
                w.at(i) = ok ? (large ? f.share_large : f.share_small) : 0.0;
                share_sum.at(f.group) += w.at(i);
            }
            if (share_sum[0] + share_sum[1] + share_sum[2] + share_sum[3] > 0.0) break;
        }
        const auto& odds = _model.odds_present.at(section).at(band) ? _model.odds.at(section).at(band)
                                                                    : _model.odds.at(d::section_count).at(band);
        std::array<double, d::group_count> group{};
        double total = 0.0;
        for (std::size_t g = 0; g < d::group_count; ++g)
        {
            if (share_sum.at(g) > 0.0) group.at(g) = c.base.at(large ? 1 : 0).at(section).at(g) * odds.at(g);
            total += group.at(g);
        }
        if (!(total > 0.0))
        {
            for (std::size_t g = 0; g < d::group_count; ++g)
            {
                group.at(g) = share_sum.at(g) > 0.0 ? 1.0 : 0.0;
                total += group.at(g);
            }
        }
        if (!(total > 0.0)) return;
        for (std::size_t i = 0; i < n; ++i)
        {
            const auto g = c.forms.at(i).group;
            if (w.at(i) > 0.0) w.at(i) = group.at(g) / total * w.at(i) / share_sum.at(g);
        }
    }

    // Whether form `i` satisfies the request's constraints on the form.
    [[nodiscard]] static bool passes_(const country_model& c, const plan& p, std::optional<std::size_t> forced, std::size_t i)
    {
        if (forced) return i == *forced;
        return !p.incorporated_only || c.forms.at(i).group >= 2;
    }

    // The probability that a company of (division, band) has a form the request allows.
    double form_mass_(const country_model& c, const plan& p, std::optional<std::size_t> forced, std::size_t division,
                      std::size_t section, std::size_t band, std::vector<double>& scratch) const
    {
        if (!p.filtered) return 1.0;
        form_probs_(c, p, division, section, band, scratch);
        double mass = 0.0;
        for (std::size_t i = 0; i < scratch.size(); ++i)
        {
            if (passes_(c, p, forced, i)) mass += scratch.at(i);
        }
        return mass;
    }

    // The weight of the bands of a division: the cell's shares, narrowed to the band asked for and to the
    // forms the request allows.
    double band_weights_(const country_model& c, const plan& p, std::optional<std::size_t> forced, std::size_t division,
                         std::size_t section, std::array<double, companygen_detail::band_count>& weights,
                         std::vector<double>& scratch) const
    {
        const auto& cell = _model.cells.at(c.cell.at(division));
        const auto& share = p.employment ? cell.emp : cell.ent;
        double total = 0.0;
        for (std::size_t b = 0; b < companygen_detail::band_count; ++b)
        {
            weights.at(b) = 0.0;
            if (p.force_band != companygen_detail::no_index && b != p.force_band) continue;
            if (!(share.at(b) > 0.0)) continue;
            weights.at(b) = share.at(b) * form_mass_(c, p, forced, division, section, b, scratch);
            total += weights.at(b);
        }
        return total;
    }

    [[nodiscard]] std::size_t pick_division_(const country_model& c, const plan& p, std::optional<std::size_t> forced,
                                             const core::rng& call, evidence_level& evidence) const
    {
        namespace d = companygen_detail;
        if (p.force_division != d::no_index) return p.force_division;
        d::weaken(evidence, c.industry_evidence);
        if (p.employment) d::weaken(evidence, c.employment_evidence);
        auto r = call.split("industry");
        const d::isco_row* occupation = p.major ? &occupation_row_(c, *p.major) : nullptr;
        const bool conditioned = p.filtered || p.force_band != d::no_index;
        std::array<double, d::max_divisions> w{};
        std::array<double, d::band_count> bands{};
        std::vector<double> scratch;
        double total = 0.0;
        // A section asked for that the country has no weight in is spread evenly over its divisions.
        for (int pass = 0; pass < 2 && !(total > 0.0); ++pass)
        {
            for (const auto division : _model.divisions)
            {
                const std::size_t section = d::section_of_division(division);
                if (p.force_section != d::no_index && section != p.force_section)
                {
                    w.at(division) = 0.0;
                    continue;
                }
                double weight = 1.0;
                if (pass == 0)
                {
                    if (occupation)
                    {
                        weight = c.section_emp.at(section) > 0.0
                                     ? occupation->at(section) * c.emp.at(division) / c.section_emp.at(section)
                                     : 0.0;
                    }
                    else
                    {
                        weight = p.employment ? c.emp.at(division) : c.ent.at(division);
                    }
                }
                else if (p.force_section == d::no_index)
                {
                    weight = 0.0;
                }
                if (weight > 0.0 && conditioned) weight *= band_weights_(c, p, forced, division, section, bands, scratch);
                w.at(division) = weight;
                total += weight;
            }
        }
        if (!(total > 0.0))
        {
            throw std::invalid_argument("company_request: no company in " + c.code + " has these fields together");
        }
        const double target = core::uniform01(r) * total;
        double acc = 0.0;
        std::size_t last = d::no_index;
        for (const auto division : _model.divisions)
        {
            if (!(w.at(division) > 0.0)) continue;
            acc += w.at(division);
            last = division;
            if (target < acc) return division;
        }
        return last;
    }

    [[nodiscard]] std::size_t pick_band_(const country_model& c, const plan& p, std::size_t division, std::size_t section,
                                         std::optional<std::size_t> forced, const core::rng& call,
                                         evidence_level& evidence) const
    {
        namespace d = companygen_detail;
        if (p.force_band != d::no_index) return p.force_band;
        d::weaken(evidence, c.size_evidence);
        if (p.employment) d::weaken(evidence, c.employment_evidence);
        auto r = call.split("size");
        std::array<double, d::band_count> weights{};
        std::vector<double> scratch;
        const double total = band_weights_(c, p, forced, division, section, weights, scratch);
        if (!(total > 0.0)) throw std::invalid_argument("company_request: no company in " + c.code + " has these fields together");
        const double target = core::uniform01(r) * total;
        double acc = 0.0;
        std::size_t last = 0;
        for (std::size_t b = 0; b < d::band_count; ++b)
        {
            if (!(weights.at(b) > 0.0)) continue;
            acc += weights.at(b);
            last = b;
            if (target < acc) return b;
        }
        return last;
    }

    [[nodiscard]] std::uint32_t draw_employees_(const country_model& c, std::size_t division, std::size_t band,
                                                const core::rng& call) const
    {
        namespace d = companygen_detail;
        auto r = call.split("employees");
        const auto& cell = _model.cells.at(c.cell.at(division));
        if (band == 3)
        {
            const auto& law = _model.pareto.at(c.large.at(division));
            const double u = core::uniform01(r);
            const double x = std::floor(d::pareto_draw(law, u));
            return static_cast<std::uint32_t>(std::clamp(x, law.lo, law.hi));
        }
        const auto& mix = cell.head.at(band);
        std::uint32_t which = mix.first;
        if (mix.t > 0.0)
        {
            const double u = core::uniform01(r);
            if (u < mix.t) which = mix.second;
        }
        const auto& cdf = _model.grid.at(band).at(which).cdf;
        const double target = core::uniform01(r) * cdf.back();
        const auto it = std::ranges::upper_bound(cdf, target);
        const auto offset = static_cast<std::size_t>(std::min<std::ptrdiff_t>(it - cdf.begin(), static_cast<std::ptrdiff_t>(cdf.size()) - 1));
        return static_cast<std::uint32_t>(d::band_lo.at(band)) + static_cast<std::uint32_t>(offset);
    }

    [[nodiscard]] std::size_t pick_form_(const country_model& c, const plan& p, std::size_t division, std::size_t section,
                                         std::size_t band, std::optional<std::size_t> forced, const core::rng& call,
                                         evidence_level& evidence) const
    {
        namespace d = companygen_detail;
        if (forced) return *forced;
        d::weaken(evidence, c.form_evidence);
        auto r = call.split("form");
        std::vector<double> w;
        form_probs_(c, p, division, section, band, w);
        double total = 0.0;
        for (std::size_t i = 0; i < w.size(); ++i)
        {
            if (!passes_(c, p, forced, i)) w.at(i) = 0.0;
            total += w.at(i);
        }
        if (!(total > 0.0)) throw std::invalid_argument("company_request: no company in " + c.code + " has these fields together");
        const double target = core::uniform01(r) * total;
        double acc = 0.0;
        std::size_t last = 0;
        for (std::size_t i = 0; i < w.size(); ++i)
        {
            if (!(w.at(i) > 0.0)) continue;
            acc += w.at(i);
            last = i;
            if (target < acc) return i;
        }
        return last;
    }

    // The founding date: an age from the cell's shape, then a day inside that year, never after the
    // reference date nor outside the form's dates.
    [[nodiscard]] std::chrono::year_month_day draw_founded_(const country_model& c, const plan& p, std::size_t section,
                                                            std::size_t band, const form_row& form,
                                                            const core::rng& call) const
    {
        namespace d = companygen_detail;
        namespace ch = std::chrono;
        auto r = call.split("founded");
        const auto& ref = c.age.at(section).at(band);
        const auto& shares = _model.ages.at(ref.index);
        std::array<double, d::max_age + 1> year_weight{};
        for (std::size_t cls = 0; cls < d::age_class_count; ++cls)
        {
            double share = shares.at(cls);
            if (ref.scaled && cls < 6) share *= c.young_scale;
            const auto [first, last] = d::age_span.at(cls);
            if (cls + 1 == d::age_class_count)
            {
                for (std::size_t i = 0; i < d::old_ages; ++i) year_weight.at(static_cast<std::size_t>(first) + i) = share * _model.old_decay.at(i);
            }
            else
            {
                const double each = share / static_cast<double>(last - first + 1);
                for (int a = first; a <= last; ++a) year_weight.at(static_cast<std::size_t>(a)) = each;
            }
        }
        struct span
        {
            std::int64_t first{0};
            std::int64_t last{-1};
        };
        std::array<span, d::max_age + 1> allowed{};
        double total = 0.0;
        const auto ref_days = p.reference.time_since_epoch().count();
        for (std::size_t a = 0; a <= d::max_age; ++a)
        {
            const int year = p.reference_year - static_cast<int>(a);
            const auto year_start = ch::sys_days{ch::year{year} / ch::January / 1}.time_since_epoch().count();
            const auto year_end = ch::sys_days{ch::year{year} / ch::December / 31}.time_since_epoch().count();
            const std::int64_t natural_end = std::min<std::int64_t>(year_end, ref_days);
            std::int64_t first = year_start;
            std::int64_t last = natural_end;
            if (form.from) first = std::max<std::int64_t>(first, form.from->time_since_epoch().count());
            if (form.until) last = std::min<std::int64_t>(last, form.until->time_since_epoch().count());
            if (first > last)
            {
                year_weight.at(a) = 0.0;
                continue;
            }
            allowed.at(a) = {first, last};
            year_weight.at(a) *= static_cast<double>(last - first + 1) / static_cast<double>(natural_end - year_start + 1);
            total += year_weight.at(a);
        }
        if (!(total > 0.0))
        {
            // Nothing in the age shape fits the form's dates: the form's own date.
            const auto days = form.until ? *form.until : form.from ? *form.from : p.reference;
            return ch::year_month_day{days};
        }
        const double target = core::uniform01(r) * total;
        double acc = 0.0;
        std::size_t age = 0;
        for (std::size_t a = 0; a <= d::max_age; ++a)
        {
            if (!(year_weight.at(a) > 0.0)) continue;
            acc += year_weight.at(a);
            age = a;
            if (target < acc) break;
        }
        const auto day = core::uniform_int<std::int64_t>(r, allowed.at(age).first, allowed.at(age).last);
        return ch::year_month_day{ch::sys_days{ch::days{static_cast<int>(day)}}};
    }

    // -- Names ----------------------------------------------------------------

    // A drawn name: the name with its legal form, the name without it, and its Latin form.
    struct drawn_name
    {
        std::string name;
        std::string short_name;
        std::string latin;        // the short name in Latin letters (text without a Latin form left out)
        std::string surname_latin; // the first founder's surname in Latin letters, if any
        std::string language;      // the language the name is written in
    };

    struct name_text
    {
        std::string text;
        std::string latin;
    };

    static name_text word_text_(const companygen_detail::word_entry& e)
    {
        return {.text = e.word, .latin = e.latin.empty() ? e.word : e.latin};
    }

    // Initials in the script's capitals (Latin for scripts without a list), with their Latin form.
    static name_text initials_(const companygen_detail::language_model& lang, core::rng& r)
    {
        static constexpr std::array<std::pair<std::string_view, std::string_view>, 27> cyrillic{
            {{"А", "A"}, {"Б", "B"}, {"В", "V"}, {"Г", "G"}, {"Д", "D"}, {"Е", "E"}, {"Ж", "Zh"}, {"З", "Z"}, {"И", "I"},
             {"К", "K"}, {"Л", "L"}, {"М", "M"}, {"Н", "N"}, {"О", "O"}, {"П", "P"}, {"Р", "R"}, {"С", "S"}, {"Т", "T"},
             {"У", "U"}, {"Ф", "F"}, {"Х", "Kh"}, {"Ц", "Ts"}, {"Ч", "Ch"}, {"Ш", "Sh"}, {"Э", "E"}, {"Ю", "Yu"}, {"Я", "Ya"}}};
        static constexpr std::array<std::pair<std::string_view, std::string_view>, 24> greek{
            {{"Α", "A"}, {"Β", "V"}, {"Γ", "G"}, {"Δ", "D"}, {"Ε", "E"}, {"Ζ", "Z"}, {"Η", "I"}, {"Θ", "Th"},
             {"Ι", "I"}, {"Κ", "K"}, {"Λ", "L"}, {"Μ", "M"}, {"Ν", "N"}, {"Ξ", "X"}, {"Ο", "O"}, {"Π", "P"},
             {"Ρ", "R"}, {"Σ", "S"}, {"Τ", "T"}, {"Υ", "Y"}, {"Φ", "F"}, {"Χ", "Ch"}, {"Ψ", "Ps"}, {"Ω", "O"}}};
        static constexpr std::string_view latin = "ABCDEFGHIJKLMNOPRSTUVW";
        const auto n = core::uniform_int<int>(r, 2, 3);
        name_text out;
        for (int i = 0; i < n; ++i)
        {
            if (lang.script == "Cyrl")
            {
                const auto& [c, l] = cyrillic.at(core::uniform_int<std::size_t>(r, 0, cyrillic.size() - 1));
                out.text += c;
                out.latin += l;
            }
            else if (lang.script == "Grek")
            {
                const auto& [c, l] = greek.at(core::uniform_int<std::size_t>(r, 0, greek.size() - 1));
                out.text += c;
                out.latin += l;
            }
            else
            {
                const char c = latin.at(core::uniform_int<std::size_t>(r, 0, latin.size() - 1));
                out.text += c;
                out.latin += c;
            }
        }
        return out;
    }

    // Collapses runs of spaces and trims.
    static std::string tidy_(std::string_view text)
    {
        std::string out;
        for (const char c : text)
        {
            if (c == ' ' && (out.empty() || out.back() == ' ')) continue;
            out += c;
        }
        while (!out.empty() && out.back() == ' ') out.pop_back();
        return out;
    }

    [[nodiscard]] drawn_name draw_name_(const country_model& c, const company_request& request, const form_row& f,
                                        std::size_t division, std::size_t section, std::size_t band,
                                        const core::rng& call) const
    {
        namespace d = companygen_detail;
        const auto r = call.split("name");

        // The language: by the country's shares.
        std::size_t li = c.name_languages.front().first;
        if (c.name_languages.size() > 1)
        {
            auto lr = r.split("language");
            double total = 0.0;
            for (const auto& [i, w] : c.name_languages) total += w;
            const double target = core::uniform01(lr) * total;
            double acc = 0.0;
            for (const auto& [i, w] : c.name_languages)
            {
                acc += w;
                li = i;
                if (target < acc) break;
            }
        }
        const auto& lang = _model.languages.at(li);

        // The sources of each part, and the parts this company can have.
        const auto fits = [&](const std::optional<std::string>& given)
        { return given && !given->empty() && d::latin_text(*given) == lang.latin; };
        const bool given_surname = fits(request.founder_surname);
        const bool given_place = fits(request.place);
        const d::word_list* surnames = nullptr;
        bool romanize = false; // a Latin-script name takes the Latin form of the culture's surnames
        if (!lang.surname.empty()) surnames = &lang.surname;
        else if (c.surnames != d::no_index)
        {
            const auto& list = _model.surnames.at(c.surnames);
            if (!list.empty() && (lang.latin || !list.latin)) surnames = &list;
            romanize = lang.latin && !list.latin;
        }
        const auto find_list = [](const std::unordered_map<std::string, d::word_list>& lists, const std::string& key) -> const d::word_list*
        {
            const auto it = lists.find(key);
            return it == lists.end() || it->second.empty() ? nullptr : &it->second;
        };
        const d::word_list* trade = find_list(lang.trade, two_digits_(division));
        if (!trade) trade = find_list(lang.trade, std::string(1, static_cast<char>('A' + section)));
        const d::word_list* region = find_list(lang.region, c.code);
        if (!region) region = find_list(lang.region, "*");
        const d::word_list* place = find_list(lang.place, c.code);
        if (!place && lang.latin && c.places != d::no_index && _model.places.at(c.places).latin && !_model.places.at(c.places).empty())
        {
            place = &_model.places.at(c.places);
        }
        if (!place) place = find_list(lang.place, "*");

        std::uint16_t have = d::p_initials;
        if (surnames || given_surname) have |= d::p_surname;
        if (surnames) have |= d::p_surname2;
        if (trade) have |= d::p_trade;
        if (!lang.adjective.empty()) have |= d::p_adjective | d::p_adjective2 | d::p_adjective3;
        if (region) have |= d::p_region;
        if (place || given_place) have |= d::p_place;
        // a sole trader takes no word that names a partner or co-owners ("& Co.", "Brothers")
        const bool sole = f.group == 0; // group_keys: 0 is sole_proprietorship
        const d::word_list& family = sole ? lang.family_sole : lang.family;
        if (!family.empty()) have |= d::p_family;
        if (!lang.joiner.empty()) have |= d::p_joiner;
        if (!lang.coined_head.empty()) have |= d::p_coined_head;
        if (!lang.coined_tail.empty()) have |= d::p_coined_tail;
        if (!lang.coined_head.empty() && !lang.coined_tail.empty()) have |= d::p_coined;
        const auto usable = [&](const d::name_template& tpl) { return (tpl.needs & ~have) == 0 && tpl.weight > 0.0; };

        // The pattern, by the shares of the language (or *), group and band, among those with a usable template.
        const std::string tail = "|" + std::string{d::group_keys.at(f.group)} + "|" + std::string{d::band_keys.at(band)};
        auto shares = _model.patterns.find(lang.code + tail);
        if (shares == _model.patterns.end()) shares = _model.patterns.find("*" + tail);
        std::array<double, d::pattern_count> w{};
        double total = 0.0;
        for (int pass = 0; pass < 2 && !(total > 0.0); ++pass)
        {
            for (std::size_t p = 0; p < d::pattern_count; ++p)
            {
                const bool any = std::ranges::any_of(lang.templates.at(p), usable);
                const double share = pass == 0 && shares != _model.patterns.end() ? shares->second.at(p) : 1.0;
                w.at(p) = any ? share : 0.0;
                total += w.at(p);
            }
        }
        const d::name_template fallback = parse_template_("{initials}", 1.0);
        const d::name_template* tpl = &fallback;
        if (total > 0.0)
        {
            auto pr = r.split("pattern");
            const double target = core::uniform01(pr) * total;
            double acc = 0.0;
            std::size_t pattern = 0;
            for (std::size_t p = 0; p < d::pattern_count; ++p)
            {
                if (!(w.at(p) > 0.0)) continue;
                acc += w.at(p);
                pattern = p;
                if (target < acc) break;
            }
            const auto& list = lang.templates.at(pattern);
            double tw = 0.0;
            for (const auto& x : list)
            {
                if (usable(x)) tw += x.weight;
            }
            auto tr = r.split("template");
            const double t2 = core::uniform01(tr) * tw;
            acc = 0.0;
            for (const auto& x : list)
            {
                if (!usable(x)) continue;
                acc += x.weight;
                tpl = &x;
                if (t2 < acc) break;
            }
        }

        // The parts, each from a stream of its own.
        const auto pick = [&](const d::word_list& list, std::string_view stream, const std::vector<std::string>& avoid = {})
        {
            auto pr = r.split(stream);
            const d::word_entry* e = &list.items.at(list.table.pick(pr));
            for (int i = 0; i < 8 && std::ranges::find(avoid, e->word) != avoid.end(); ++i)
            {
                e = &list.items.at(list.table.pick(pr));
            }
            return word_text_(*e);
        };
        drawn_name out;
        out.language = lang.code;
        std::string text;
        std::string latin;
        std::vector<std::string> surnames_used;
        std::vector<std::string> adjectives_used;
        for (const auto& piece : tpl->pieces)
        {
            name_text part;
            switch (piece.part)
            {
            case 0:
                part.text = piece.text;
                part.latin = d::latin_text(piece.text) ? piece.text : " ";
                break;
            case d::p_surname:
                part = given_surname ? name_text{*request.founder_surname, *request.founder_surname} : pick(*surnames, "surname");
                if (romanize && !given_surname) part.text = part.latin;
                surnames_used.push_back(part.text);
                if (out.surname_latin.empty()) out.surname_latin = part.latin;
                break;
            case d::p_surname2:
                part = pick(*surnames, "surname2", surnames_used);
                if (romanize) part.text = part.latin;
                surnames_used.push_back(part.text);
                break;
            case d::p_trade: part = pick(*trade, "trade"); break;
            case d::p_adjective:
            case d::p_adjective2:
            case d::p_adjective3:
                part = pick(lang.adjective, piece.part == d::p_adjective ? "adjective" : piece.part == d::p_adjective2 ? "adjective2" : "adjective3",
                            adjectives_used);
                adjectives_used.push_back(part.text);
                break;
            case d::p_region: part = pick(*region, "region"); break;
            case d::p_place: part = given_place ? name_text{*request.place, *request.place} : pick(*place, "place"); break;
            case d::p_family: part = pick(family, "family"); break;
            case d::p_joiner: part = pick(lang.joiner, "joiner"); break;
            case d::p_coined_head: part = pick(lang.coined_head, "coined_head"); break;
            case d::p_coined_tail: part = pick(lang.coined_tail, "coined_tail"); break;
            case d::p_coined:
            {
                const auto head = pick(lang.coined_head, "coined_head");
                const auto end = pick(lang.coined_tail, "coined_tail");
                part.text = head.text + lang.coined_join + end.text;
                part.latin = head.latin + lang.coined_join + end.latin;
                break;
            }
            case d::p_initials:
            {
                auto ir = r.split("initials");
                part = initials_(lang, ir);
                break;
            }
            default: break;
            }
            text += part.text;
            latin += part.latin;
        }
        out.short_name = tidy_(text);
        out.latin = tidy_(latin);

        // The legal form: its in_name text when its script fits, else the abbreviation, placed by the style.
        auto fr = r.split("form");
        std::string form_text;
        bool done = false;
        if (!f.in_name.empty())
        {
            const auto alts = d::split_view(f.in_name, '|');
            const auto alt = alts.at(alts.size() == 1 ? 0 : core::uniform_int<std::size_t>(fr, 0, alts.size() - 1));
            const auto at = alt.find("{name}");
            std::string bare{alt};
            if (at != std::string_view::npos) bare.erase(at, 6);
            if (alt == "-")
            {
                done = true;
                out.name = out.short_name;
            }
            else if (d::latin_text(bare) == lang.latin)
            {
                if (at != std::string_view::npos)
                {
                    out.name = std::string{alt.substr(0, at)} + out.short_name + std::string{alt.substr(at + 6)};
                    done = true;
                }
                else form_text = std::string{alt};
            }
            else if (lang.latin) form_text = f.latin_abbreviation;
        }
        else if (!f.abbreviation.empty() && d::latin_text(f.abbreviation) == lang.latin) form_text = f.abbreviation;
        else if (lang.latin) form_text = f.latin_abbreviation; // a name in another script takes no Latin form
        if (!done)
        {
            if (form_text.empty()) out.name = out.short_name;
            else
            {
                int position = lang.form_position;
                if (position == 2) position = core::uniform_int<int>(fr, 0, 1);
                const std::string sep = lang.spaced ? " " : "";
                if (position == 1)
                {
                    out.name = lang.quote_open.empty() ? form_text + sep + out.short_name
                                                       : form_text + " " + lang.quote_open + out.short_name + lang.quote_close;
                }
                else out.name = out.short_name + sep + form_text;
            }
        }
        return out;
    }

    // -- Domains --------------------------------------------------------------

    [[nodiscard]] std::string draw_domain_(const country_model& c, const drawn_name& name, bool safe, const core::rng& call) const
    {
        namespace d = companygen_detail;
        auto r = call.split("domain");
        // The Latin form folded to lowercase ASCII words (German umlauts as ae, oe, ue).
        const bool german = name.language == "de";
        std::vector<std::string> words(1);
        for (std::size_t i = 0; i < name.latin.size();)
        {
            std::size_t used = 1;
            const char32_t cp = d::decode_utf8(name.latin, i, used);
            i += used;
            if ((cp >= 'a' && cp <= 'z') || (cp >= '0' && cp <= '9')) words.back() += static_cast<char>(cp);
            else if (cp >= 'A' && cp <= 'Z') words.back() += static_cast<char>(cp - 'A' + 'a');
            else if (cp == 0x2019 || cp == 0x27) continue;
            else if (german && (cp == U'ä' || cp == U'ö' || cp == U'ü' || cp == U'Ä' || cp == U'Ö' || cp == U'Ü'))
            {
                words.back() += cp == U'ä' || cp == U'Ä' ? "ae" : cp == U'ö' || cp == U'Ö' ? "oe" : "ue";
            }
            else if (const auto it = _model.fold.find(cp); it != _model.fold.end())
            {
                for (const char ch : it->second)
                {
                    if (ch >= 'A' && ch <= 'Z') words.back() += static_cast<char>(ch - 'A' + 'a');
                    else if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')) words.back() += ch;
                }
            }
            else if (!words.back().empty()) words.emplace_back();
        }
        std::erase_if(words, [](const std::string& w) { return w.empty(); });
        const bool hyphens = core::uniform01(r) < 0.25;
        // as many words as fit in 24 characters, not ending on a short word ("de", "ve") when cut
        std::size_t kept = 0;
        std::size_t length = 0;
        for (const auto& w : words)
        {
            const std::size_t extra = (kept == 0 ? 0 : (hyphens ? 1 : 0)) + w.size();
            if (kept > 0 && length + extra > 24) break;
            length += extra;
            ++kept;
        }
        while (kept > 1 && kept < words.size() && words.at(kept - 1).size() <= 2) --kept;
        std::string label;
        for (std::size_t i = 0; i < kept; ++i)
        {
            if (i > 0 && hyphens) label += '-';
            label += words.at(i);
        }
        if (label.size() > 30) label.resize(30);
        if (label.empty())
        {
            const auto n = core::uniform_int<int>(r, 4, 6);
            for (int i = 0; i < n; ++i) label += static_cast<char>('a' + core::uniform_int<int>(r, 0, 25));
        }
        if (safe) return label + ".example";
        std::string tld = "com";
        double total = 0.0;
        for (const auto& [t, share] : c.tlds) total += share;
        if (total > 0.0)
        {
            const double target = core::uniform01(r) * total;
            double acc = 0.0;
            for (const auto& [t, share] : c.tlds)
            {
                acc += share;
                tld = t;
                if (target < acc) break;
            }
        }
        return label + "." + tld;
    }

    // -- Numbers --------------------------------------------------------------

    // What a builder makes: the number as printed and the positions of its check characters in the
    // compact form (its digits and letters).
    struct built_number
    {
        std::string value;
        std::vector<std::size_t> check_at;
        bool reserved{false};
        std::string compact; // set only where the compact form is not the value's digits and letters
    };

    struct number_context
    {
        const country_model& c;
        const form_row& f;
        std::size_t band;
        std::size_t division;
        std::size_t section;
        std::uint32_t employees;
        std::chrono::year_month_day founded;
        const plan& p;
        bool safe;
        const drawn_name& name;
        core::rng numbers;
        core::rng lei;
        std::unordered_map<std::string, std::size_t> chosen;  // code list -> the company's entry
        std::unordered_map<std::string, std::string> built;   // scheme -> its compact number
    };

    [[nodiscard]] int founded_year_(const number_context& x) const { return static_cast<int>(x.founded.year()); }

    [[nodiscard]] std::chrono::sys_days founded_days_(const number_context& x) const { return std::chrono::sys_days{x.founded}; }

    // The value a parameter maps the company's form to (its ISO code, its key, its group letter, else *).
    [[nodiscard]] static std::optional<std::string> mapped_(const companygen_detail::scheme_model& s, std::string_view param,
                                                            const number_context& x)
    {
        const auto text = s.param(param);
        if (text.empty()) return std::nullopt;
        const std::array<char, 1> g{"SPLO"[x.f.group]};
        return companygen_detail::map_lookup(text, {x.f.code, x.f.key, std::string_view{g.data(), 1}, "*"});
    }

    [[nodiscard]] const companygen_detail::code_list& list_(std::string_view name) const
    {
        const auto it = _model.codes.find(std::string{name});
        if (it == _model.codes.end() || it->second.code.empty())
        {
            throw std::runtime_error("company data: no code list " + std::string{name});
        }
        return it->second;
    }

    // The company's entry of a code list: one per company, shared by its numbers.
    std::size_t choose_(number_context& x, const std::string& name) const
    {
        if (const auto it = x.chosen.find(name); it != x.chosen.end()) return it->second;
        auto r = x.numbers.split("list").split(name);
        const std::size_t i = list_(name).table.pick(r);
        x.chosen.emplace(name, i);
        return i;
    }

    // The parts in order: the elements of a braced list are evaluated left to right, so draws made
    // inside them come in the same order on every compiler (the operands of + are unsequenced).
    static std::string join_(std::initializer_list<std::string> parts)
    {
        std::string out;
        for (const auto& p : parts) out += p;
        return out;
    }

    static std::string digits_(core::rng& r, std::size_t n)
    {
        std::string out;
        for (std::size_t i = 0; i < n; ++i) out += static_cast<char>('0' + core::uniform_int<int>(r, 0, 9));
        return out;
    }

    // A number of an anchored series issued in `year`: between the quantile points of that year (the
    // nearest year with anchors).
    static std::uint64_t anchor_draw_(const companygen_detail::anchor_series& s, int year, core::rng& r)
    {
        namespace d = companygen_detail;
        auto at = static_cast<std::size_t>(std::ranges::upper_bound(s.years, year) - s.years.begin());
        if (at > 0) --at;
        const auto& q = s.points.at(at);
        const double u = core::uniform01(r);
        double v = q.front();
        if (u >= d::anchor_probs.back()) v = q.back();
        else if (u > d::anchor_probs.front())
        {
            std::size_t k = 0;
            while (k + 2 < d::anchor_probs.size() && u >= d::anchor_probs.at(k + 1)) ++k;
            const double t = (u - d::anchor_probs.at(k)) / (d::anchor_probs.at(k + 1) - d::anchor_probs.at(k));
            v = q.at(k) + t * (q.at(k + 1) - q.at(k));
        }
        return static_cast<std::uint64_t>(std::max(1.0, std::floor(v + 0.5)));
    }

    // A serial number by the founding year: from start (number lo) to the reference date (hi), as
    // ((year - start) / (ref - start))^power; firms older than start share its first years.
    [[nodiscard]] std::uint64_t serial_by_year_(const companygen_detail::scheme_model& s, const number_context& x,
                                                std::uint64_t lo, std::uint64_t hi, core::rng& r) const
    {
        namespace d = companygen_detail;
        const int year = founded_year_(x);
        const auto start_text = s.param("start");
        double q = core::uniform01(r);
        if (!start_text.empty())
        {
            const auto start = static_cast<int>(d::to_number(start_text));
            const double power = s.param("power").empty() ? 1.0 : d::to_double(s.param("power"));
            const double span = std::max(1.0, static_cast<double>(x.p.reference_year - start + 1));
            const double t = year < start ? 0.02 * q : (static_cast<double>(year - start) + q) / span;
            q = t > 0.0 ? core::math::exp(power * core::math::log(std::min(1.0, t))) : 0.0;
        }
        const auto v = static_cast<double>(lo) + q * static_cast<double>(hi - lo);
        return std::clamp(static_cast<std::uint64_t>(v), lo, hi);
    }

    // A number in the safe range of a scheme ("safe=lo-hi"), or above the highest issued.
    [[nodiscard]] static std::uint64_t safe_draw_(const companygen_detail::scheme_model& s, std::uint64_t highest,
                                                  std::uint64_t top, core::rng& r)
    {
        const auto text = s.param("safe");
        if (!text.empty())
        {
            const auto [lo, hi] = companygen_detail::parse_range(text);
            return core::uniform_int<std::uint64_t>(r, lo, hi);
        }
        const std::uint64_t lo = highest + std::max<std::uint64_t>(1, (top - std::min(top, highest)) / 2);
        return core::uniform_int<std::uint64_t>(r, std::min(lo, top), top);
    }

    // The generic builder: the pattern (or the anchors) and the check characters, printed by the mask.
    [[nodiscard]] std::optional<built_number> build_generic_(const companygen_detail::scheme_model& s, number_context& x,
                                                             core::rng& r) const
    {
        namespace d = companygen_detail;
        const auto anchors = s.param("anchors");
        const auto mask_slots = static_cast<std::size_t>(std::ranges::count_if(s.display, [](char ch) { return ch == '#' || ch == '@'; }));
        for (int attempt = 0; attempt < 200; ++attempt)
        {
            std::string body;
            if (!anchors.empty())
            {
                const auto it = _model.anchors.find(std::string{anchors} + "|-");
                const std::size_t width = mask_slots - (s.check.type == d::check_rule::kind::none ? 0 : 1);
                if (it == _model.anchors.end() || it->second.years.empty()) body = digits_(r, width);
                else body = d::zero_pad(anchor_draw_(it->second, founded_year_(x), r), width);
            }
            else if (s.pattern) body = s.pattern->draw(r);
            const auto check = d::compute_check(s.check, body);
            if (!check) continue;
            if (x.safe && s.check.name == "tw_ubn" && body.size() > 6 && body[6] == '7') continue; // two checks pass
            const std::string slots = body + *check;
            built_number out{.value = d::apply_display(s.display, slots), .check_at = {}, .reserved = false, .compact = {}};
            const auto pos = d::slot_positions(s.display, slots.size());
            for (std::size_t k = body.size(); k < slots.size(); ++k) out.check_at.push_back(pos.at(k));
            return out;
        }
        throw std::runtime_error("company: could not draw a " + s.key + " number");
    }

    // India's PAN: the holder's type in the fourth letter, the first letter of its name in the fifth.
    [[nodiscard]] std::string pan_(number_context& x) const
    {
        namespace d = companygen_detail;
        if (const auto it = x.built.find("in-pan"); it != x.built.end()) return it->second;
        const auto at = std::ranges::find_if(x.c.schemes, [&](std::size_t i) { return _model.schemes.at(i).key == "in-pan"; });
        auto r = x.numbers.split("in-pan");
        std::string type = "C";
        if (at != x.c.schemes.end())
        {
            if (const auto m = mapped_(_model.schemes.at(*at), "type", x)) type = *m;
        }
        const auto letter = [&] { return static_cast<char>('A' + core::uniform_int<int>(r, 0, 25)); };
        std::string out;
        for (int i = 0; i < 3; ++i) out += letter();
        out += type.empty() ? 'C' : type.front();
        const std::string& from = x.f.group == 0 && !x.name.surname_latin.empty() ? x.name.surname_latin : x.name.latin;
        char initial = '\0';
        for (const char ch : from)
        {
            if (ch >= 'A' && ch <= 'Z') initial = ch;
            else if (ch >= 'a' && ch <= 'z') initial = static_cast<char>(ch - 'a' + 'A');
            if (initial != '\0') break;
        }
        out += initial != '\0' ? initial : letter();
        out += d::zero_pad(core::uniform_int<std::uint64_t>(r, 1, 9999), 4);
        out += letter();
        x.built.emplace("in-pan", out);
        return out;
    }

    // The base number a dependent number wraps (SIREN, ACN, RSIN), drawn by its own scheme when the
    // company has it, else as that scheme would.
    [[nodiscard]] std::optional<std::string> base_(number_context& x, std::string_view key) const
    {
        if (const auto it = x.built.find(std::string{key}); it != x.built.end()) return it->second;
        return std::nullopt;
    }

    // The check characters each named builder's numbers carry, as python-stdnum checks them.
    static char ar_cuit_check_(std::string_view ten)
    {
        constexpr std::array<unsigned, 10> w{5, 4, 3, 2, 7, 6, 5, 4, 3, 2};
        unsigned s = 0;
        for (std::size_t i = 0; i < 10; ++i) s += static_cast<unsigned>(ten[i] - '0') * w.at(i);
        return std::string_view{"012345678990"}[11 - s % 11];
    }

    static std::string au_abn_check_(std::string_view nine)
    {
        constexpr std::array<unsigned, 9> w{3, 5, 7, 9, 11, 13, 15, 17, 19};
        unsigned t = 0;
        for (std::size_t i = 0; i < 9; ++i) t += static_cast<unsigned>(nine[i] - '0') * w.at(i);
        return std::to_string(11 + (89 - (t + 1) % 89) % 89);
    }

    static std::string br_cnpj_check_(std::string_view twelve)
    {
        std::vector<unsigned> v;
        for (const char ch : twelve.substr(0, 12)) v.push_back(static_cast<unsigned>(static_cast<unsigned char>(ch)) - 48U);
        const auto digit = [&](std::span<const unsigned> w)
        {
            unsigned s = 0;
            for (std::size_t i = 0; i < w.size(); ++i) s += w[i] * v.at(i);
            return (11 - s % 11) % 11 % 10;
        };
        constexpr std::array<unsigned, 12> w1{5, 4, 3, 2, 9, 8, 7, 6, 5, 4, 3, 2};
        constexpr std::array<unsigned, 13> w2{6, 5, 4, 3, 2, 9, 8, 7, 6, 5, 4, 3, 2};
        const unsigned d1 = digit(w1);
        v.push_back(d1);
        const unsigned d2 = digit(w2);
        return std::string{static_cast<char>('0' + d1), static_cast<char>('0' + d2)};
    }

    static constexpr std::string_view uscc_alphabet_ = "0123456789ABCDEFGHJKLMNPQRTUWXY";

    static char cn_uscc_check_(std::string_view seventeen)
    {
        constexpr std::array<unsigned, 17> w{1, 3, 9, 27, 19, 26, 16, 17, 20, 29, 25, 13, 8, 24, 10, 30, 28};
        unsigned s = 0;
        for (std::size_t i = 0; i < 17; ++i)
        {
            const auto at = uscc_alphabet_.find(seventeen[i]);
            if (at == std::string_view::npos) return '?';
            s += static_cast<unsigned>(at) * w.at(i);
        }
        return uscc_alphabet_[(31 - s % 31) % 31];
    }

    // GB 11714: the check of an organisation code (8 characters).
    static char org_code_check_(std::string_view eight)
    {
        constexpr std::array<unsigned, 8> w{3, 7, 9, 10, 5, 8, 4, 2};
        unsigned s = 0;
        for (std::size_t i = 0; i < 8; ++i) s += core::alnum_value(eight[i]) * w.at(i);
        const unsigned c = 11 - s % 11;
        return c == 10 ? 'X' : c == 11 ? '0' : static_cast<char>('0' + c);
    }

    static std::string es_cif_checks_(std::string_view seven)
    {
        const char d = core::luhn_digit(seven);
        return std::string{d, std::string_view{"JABCDEFGHI"}[static_cast<std::size_t>(d - '0')]};
    }

    static unsigned gb_vat_sum_(std::string_view nine)
    {
        constexpr std::array<unsigned, 9> w{8, 7, 6, 5, 4, 3, 2, 10, 1};
        unsigned s = 0;
        for (std::size_t i = 0; i < 9; ++i) s += static_cast<unsigned>(nine[i] - '0') * w.at(i);
        return s % 97;
    }

    static constexpr std::string_view ie_alphabet_ = "WABCDEFGHIJKLMNOPQRSTUV";

    static char ie_vat_check_(std::string_view seven, std::string_view second)
    {
        unsigned s = 0;
        for (std::size_t i = 0; i < 7; ++i) s += static_cast<unsigned>(8 - i) * static_cast<unsigned>(seven[i] - '0');
        const auto at = second.empty() ? 0 : ie_alphabet_.find(second.front());
        if (at == std::string_view::npos) return '?';
        s += 9 * static_cast<unsigned>(at);
        return ie_alphabet_[s % 23];
    }

    static char jp_cn_check_(std::string_view twelve)
    {
        unsigned s = 0;
        for (std::size_t i = 0; i < 12; ++i) s += static_cast<unsigned>(twelve[11 - i] - '0') * (i % 2 == 0 ? 1U : 2U);
        return static_cast<char>('0' + 9 - s % 9);
    }

    static char kr_brn_check_(std::string_view nine)
    {
        constexpr std::array<unsigned, 9> w{1, 3, 7, 1, 3, 7, 1, 3, 5};
        unsigned s = 0;
        for (std::size_t i = 0; i < 9; ++i) s += static_cast<unsigned>(nine[i] - '0') * w.at(i);
        s += (static_cast<unsigned>(nine[8] - '0') * 5) / 10;
        return static_cast<char>('0' + (10 - s % 10) % 10);
    }

    static constexpr std::string_view rfc_alphabet_ = "0123456789ABCDEFGHIJKLMN&OPQRSTUVWXYZ ";

    static char mx_rfc_check_(std::string_view eleven)
    {
        const std::string twelve = " " + std::string{eleven};
        unsigned s = 0;
        for (std::size_t i = 0; i < 12; ++i)
        {
            const auto at = rfc_alphabet_.find(twelve[i]);
            if (at == std::string_view::npos) return '?';
            s += static_cast<unsigned>(at) * static_cast<unsigned>(13 - i);
        }
        return rfc_alphabet_[(11 - s % 11) % 11];
    }

    static char ro_onrc_check_(std::string_view thirteen)
    {
        unsigned s = static_cast<unsigned>(static_cast<unsigned char>(thirteen[0])) % 10;
        for (std::size_t i = 1; i < 13; ++i) s += static_cast<unsigned>(thirteen[i] - '0');
        return static_cast<char>('0' + s % 10);
    }

    static std::optional<char> ru_ogrn_check_(std::string_view head)
    {
        const auto v = companygen_detail::to_number(head);
        const auto c = head.size() == 12 ? v % 11 % 10 : v % 13;
        if (c > 9) return std::nullopt;
        return static_cast<char>('0' + c);
    }

    static char sg_check_(std::string_view body, std::string_view kind)
    {
        if (kind == "business")
        {
            constexpr std::array<unsigned, 8> w{10, 4, 9, 3, 8, 2, 7, 1};
            unsigned s = 0;
            for (std::size_t i = 0; i < 8; ++i) s += static_cast<unsigned>(body[i] - '0') * w.at(i);
            return std::string_view{"XMKECAWLJDB"}[s % 11];
        }
        if (kind == "local")
        {
            constexpr std::array<unsigned, 9> w{10, 8, 6, 4, 9, 7, 5, 3, 1};
            unsigned s = 0;
            for (std::size_t i = 0; i < 9; ++i) s += static_cast<unsigned>(body[i] - '0') * w.at(i);
            return std::string_view{"ZKCMDNERGWH"}[s % 11];
        }
        constexpr std::string_view alphabet = "ABCDEFGHJKLMNPQRSTUVWX0123456789";
        constexpr std::array<unsigned, 9> w{4, 3, 5, 3, 10, 2, 2, 5, 7};
        unsigned s = 0;
        for (std::size_t i = 0; i < 9; ++i)
        {
            const auto at = alphabet.find(body[i]);
            if (at == std::string_view::npos) return '?';
            s += static_cast<unsigned>(at) * w.at(i);
        }
        return alphabet[(s + 11 - 5) % 11];
    }

    static std::optional<char> vn_mst_check_(std::string_view nine)
    {
        constexpr std::array<unsigned, 9> w{31, 29, 23, 19, 17, 13, 7, 5, 3};
        unsigned s = 0;
        for (std::size_t i = 0; i < 9; ++i) s += static_cast<unsigned>(nine[i] - '0') * w.at(i);
        if (s % 11 == 0) return std::nullopt;
        return static_cast<char>('0' + 10 - s % 11);
    }

    static bool hu_anum_valid_(std::string_view eight)
    {
        constexpr std::array<unsigned, 8> w{9, 7, 3, 1, 9, 7, 3, 1};
        unsigned s = 0;
        for (std::size_t i = 0; i < 8; ++i) s += static_cast<unsigned>(eight[i] - '0') * w.at(i);
        return s % 10 == 0;
    }

    static bool bsn_valid_(std::string_view nine)
    {
        if (nine.size() != 9 || !companygen_detail::all_digits(nine)) return false;
        int s = 0;
        for (std::size_t i = 0; i < 8; ++i) s += static_cast<int>(9 - i) * (nine[i] - '0');
        s -= nine[8] - '0';
        return ((s % 11) + 11) % 11 == 0;
    }

    // Whether a compact number passes its scheme's check, as python-stdnum checks it (structure only
    // where the scheme has no check).
    [[nodiscard]] bool number_valid_(const companygen_detail::scheme_model& s, std::string_view n) const
    {
        namespace d = companygen_detail;
        const auto& b = s.builder;
        const auto digits = [&](std::size_t from, std::size_t len)
        { return n.size() >= from + len && d::all_digits(n.substr(from, len)); };
        if (b == "generic" || b == "ru_inn" || b == "th_juristic" || b == "it_piva" || b == "il_hp" || b == "se_orgnr")
        {
            // the slots of the mask: the compact form without the mask's literal letters and digits
            std::string slots{n};
            const auto mask_slots = static_cast<std::size_t>(std::ranges::count_if(s.display, [](char ch) { return ch == '#' || ch == '@'; }));
            const auto full = d::alnum_of(s.display).size() + mask_slots;
            if (!s.display.empty() && n.size() == full && n.size() != mask_slots)
            {
                slots.clear();
                for (std::size_t i = 0, k = 0; i < s.display.size(); ++i)
                {
                    const char ch = s.display[i];
                    if (ch == '#' || ch == '@') slots += n[k++];
                    else if (d::is_alnum(ch))
                    {
                        if (n[k] != ch) return false;
                        ++k;
                    }
                }
            }
            if (b == "it_piva")
            {
                return slots.size() == 11 && d::all_digits(slots) && slots.substr(0, 7) != "0000000" && slots.substr(7, 3) >= "001" &&
                       slots.substr(7, 3) <= "100" && core::luhn_valid(slots);
            }
            if (b == "il_hp") return slots.size() == 9 && d::all_digits(slots) && slots[0] == '5' && core::luhn_valid(slots);
            if (b == "se_orgnr") return slots.size() == 10 && d::all_digits(slots) && core::luhn_valid(slots);
            if (s.check.type == d::check_rule::kind::none || s.check.type == d::check_rule::kind::unpublished)
            {
                return !slots.empty() && (!s.pattern || s.pattern->matches(slots));
            }
            const std::size_t width = s.check.type == d::check_rule::kind::mod97_10 || s.check.name == "be_kbo" ? 2 : 1;
            if (slots.size() <= width) return false;
            const auto body = std::string_view{slots}.substr(0, slots.size() - width);
            if (s.check.type != d::check_rule::kind::named || s.check.name != "be_kbo")
            {
                if (!d::all_digits(body)) return false;
            }
            const auto check = d::compute_check(s.check, body);
            const auto given = std::string_view{slots}.substr(slots.size() - width);
            if (check && *check == given) return true;
            if (s.check.name == "tw_ubn" && body.size() > 6 && body[6] == '7' && check)
            {
                return given.front() == static_cast<char>('0' + (check->front() - '0' + 9) % 10);
            }
            return false;
        }
        if (b == "serial") return !n.empty() && d::all_digits(n);
        if (b == "lei") return n.size() == 20 && core::detail::all_digits_or_capitals(n) && core::mod97_10_valid(n);
        if (b == "ar_cuit") return n.size() == 11 && d::all_digits(n) && ar_cuit_check_(n.substr(0, 10)) == n[10];
        if (b == "at_fn")
        {
            const auto body = n.starts_with("FN") ? n.substr(2) : n;
            return body.size() >= 2 && d::all_digits(body.substr(0, body.size() - 1)) && body.back() >= 'a' && body.back() <= 'z';
        }
        if (b == "au_abn") return n.size() == 11 && d::all_digits(n) && au_abn_check_(n.substr(2)) == n.substr(0, 2);
        if (b == "br_cnpj")
        {
            return n.size() == 14 && core::detail::all_digits_or_capitals(n.substr(0, 12)) && digits(12, 2) &&
                   !n.starts_with("000000000000") && br_cnpj_check_(n) == n.substr(12);
        }
        if (b == "cn_uscc") return n.size() == 18 && digits(0, 8) && cn_uscc_check_(n) == n[17];
        if (b == "de_hrn")
        {
            for (const auto section : {"HRA ", "HRB ", "GnR ", "VR ", "PR "})
            {
                if (n.find(section) != std::string_view::npos) return true;
            }
            return false;
        }
        if (b == "es_nif")
        {
            return n.size() == 9 && std::string_view{"ABCDEFGHJNPQRSUVW"}.find(n[0]) != std::string_view::npos && digits(1, 7) &&
                   es_cif_checks_(n.substr(1, 7)).find(n[8]) != std::string::npos;
        }
        if (b == "fr_siret") return n.size() == 14 && d::all_digits(n) && core::luhn_valid(n) && core::luhn_valid(n.substr(0, 9));
        if (b == "fr_tva")
        {
            const auto body = n.starts_with("FR") ? n.substr(2) : n;
            if (body.size() != 11 || !d::all_digits(body) || !core::luhn_valid(body.substr(2))) return false;
            return d::to_number(body.substr(0, 2)) == (d::to_number(body.substr(2)) * 100 + 12) % 97;
        }
        if (b == "gb_crn")
        {
            return n.size() == 8 && (d::all_digits(n) || (n[0] >= 'A' && n[0] <= 'Z' && n[1] >= 'A' && n[1] <= 'Z' && digits(2, 6)));
        }
        if (b == "gb_vat")
        {
            const auto body = n.starts_with("GB") ? n.substr(2) : n;
            if ((body.size() != 9 && body.size() != 12) || !d::all_digits(body)) return false;
            const auto sum = gb_vat_sum_(body);
            if (d::to_number(body.substr(0, 3)) >= 100) return sum == 0 || sum == 42 || sum == 55;
            return sum == 0;
        }
        if (b == "hu_adoszam") return n.size() == 11 && d::all_digits(n) && hu_anum_valid_(n.substr(0, 8));
        if (b == "id_npwp")
        {
            if (!d::all_digits(n)) return false;
            if (n.size() == 15) return core::luhn_valid(n.substr(0, 9));
            return n.size() == 16 && n[0] == '0' && core::luhn_valid(n.substr(0, 10));
        }
        if (b == "ie_vat")
        {
            const auto body = n.starts_with("IE") ? n.substr(2) : n;
            if ((body.size() != 8 && body.size() != 9) || !d::all_digits(body.substr(0, 7))) return false;
            return ie_vat_check_(body.substr(0, 7), body.substr(8)) == body[7];
        }
        if (b == "in_cin")
        {
            return n.size() == 21 && (n[0] == 'L' || n[0] == 'U') && digits(1, 5) && digits(8, 4) && digits(15, 6);
        }
        if (b == "in_pan")
        {
            return n.size() == 10 && std::ranges::all_of(n.substr(0, 5), [](char ch) { return ch >= 'A' && ch <= 'Z'; }) && digits(5, 4) &&
                   n[9] >= 'A' && n[9] <= 'Z';
        }
        if (b == "in_gstin")
        {
            return n.size() == 15 && core::detail::all_digits_or_capitals(n) && digits(0, 2) && n[12] != '0' && n[13] == 'Z' &&
                   d::luhn36_checksum(n) == 0;
        }
        if (b == "jp_cn") return n.size() == 13 && d::all_digits(n) && jp_cn_check_(n.substr(1)) == n[0];
        if (b == "kr_brn") return n.size() == 10 && d::all_digits(n) && kr_brn_check_(n.substr(0, 9)) == n[9];
        if (b == "mx_rfc") return n.size() == 12 && digits(3, 6) && mx_rfc_check_(n.substr(0, 11)) == n[11];
        if (b == "ng_rc") return n.size() > 2 && d::all_digits(n.substr(2));
        if (b == "nl_btw")
        {
            const auto body = n.starts_with("NL") ? n.substr(2) : n;
            if (body.size() != 12 || !d::all_digits(body.substr(0, 9)) || body[9] != 'B' || !d::all_digits(body.substr(10))) return false;
            if (d::to_number(body.substr(0, 9)) == 0 || d::to_number(body.substr(10)) == 0) return false;
            return bsn_valid_(body.substr(0, 9)) || core::mod97_10_valid("NL" + std::string{body});
        }
        if (b == "ro_onrc")
        {
            if (n.size() == 14 && d::all_digits(n.substr(1))) return ro_onrc_check_(n) == n[13];
            return n.size() >= 6 && (n[0] == 'J' || n[0] == 'F' || n[0] == 'C');
        }
        if (b == "ru_ogrn")
        {
            if (!d::all_digits(n) || (n.size() != 13 && n.size() != 15)) return false;
            if (n.size() == 13 && n[0] == '0') return false;
            if (n.size() == 15 && n[0] != '3' && n[0] != '4') return false;
            const auto c = ru_ogrn_check_(n.substr(0, n.size() - 1));
            return c && *c == n.back();
        }
        if (b == "sg_uen")
        {
            if (n.size() == 9 && digits(0, 8)) return sg_check_(n, "business") == n[8];
            if (n.size() == 10 && digits(0, 9)) return sg_check_(n, "local") == n[9];
            return n.size() == 10 && (n[0] == 'R' || n[0] == 'S' || n[0] == 'T') && digits(1, 2) && digits(5, 4) &&
                   sg_check_(n, "other") == n[9];
        }
        if (b == "us_ein")
        {
            if (n.size() != 9 || !d::all_digits(n)) return false;
            const auto prefix = n.substr(0, 2);
            for (const auto list : {"internet", "campus"})
            {
                for (const auto p : d::split_view(s.param(list), ' '))
                {
                    if (p == prefix) return true;
                }
            }
            return false;
        }
        if (b == "vn_mst")
        {
            if (n.size() != 10 || !d::all_digits(n)) return false;
            const auto c = vn_mst_check_(n.substr(0, 9));
            return c && *c == n[9];
        }
        if (b == "za_cipc") return n.size() == 12 && d::all_digits(n);
        return false;
    }

    std::optional<built_number> build_named_(const companygen_detail::scheme_model& s, number_context& x, core::rng& r) const
    {
        namespace d = companygen_detail;
        namespace ch = std::chrono;
        const auto& b = s.builder;
        const int year = founded_year_(x);
        const auto shown = [&](std::string slots, std::vector<std::size_t> check_slots) -> built_number
        {
            const auto pos = d::slot_positions(s.display, slots.size());
            built_number out{.value = d::apply_display(s.display, slots), .check_at = {}, .reserved = false, .compact = {}};
            for (const auto k : check_slots) out.check_at.push_back(pos.at(k));
            return out;
        };
        const auto registered = [&]
        {
            const auto share = s.param("registered");
            if (share.empty()) return true;
            const auto v = d::map_lookup(share, {d::band_keys.at(x.band)});
            return !v || core::uniform01(r) < d::to_double(*v);
        };

        if (b == "serial")
        {
            const auto [lo, hi] = d::parse_range(s.param("range"));
            built_number out;
            std::uint64_t v = 0;
            if (x.safe)
            {
                v = safe_draw_(s, hi, hi * 10, r);
                out.reserved = true;
            }
            else v = serial_by_year_(s, x, lo, hi, r);
            const auto slots = static_cast<std::size_t>(std::ranges::count(s.display, '#'));
            out.value = slots > 0 ? d::zero_pad(v, slots) : std::to_string(v);
            return out;
        }
        if (b == "lei")
        {
            built_number out;
            std::string prefix = "ZZZZ";
            if (x.safe && s.safe == d::safe_method::reserved_range)
            {
                prefix = std::string{s.param("safe_prefix")};
                out.reserved = true;
            }
            else
            {
                const std::string base{s.param("lou")};
                const auto own = _model.codes.find(base + ":" + x.c.code);
                const auto& list = own != _model.codes.end() && !own->second.code.empty() ? own->second : list_(base + ":*");
                prefix = list.code.at(list.table.pick(r));
            }
            std::string body = prefix;
            constexpr std::string_view alnum = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
            for (int i = 0; i < 14; ++i) body += alnum.at(core::uniform_int<std::size_t>(r, 0, alnum.size() - 1));
            const auto c = core::mod97_10_digits(body);
            out.value = body + std::string{c[0], c[1]};
            out.check_at = {18, 19};
            return out;
        }
        if (b == "ar_cuit")
        {
            const std::string body = digits_(r, 8);
            std::string prefix = "30";
            const std::string first = prefix + body;
            if (ar_cuit_check_(first) == '9')
            {
                // 11 - r = 10 with 30: AFIP gives the number 33 instead
                constexpr std::array<unsigned, 10> w{5, 4, 3, 2, 7, 6, 5, 4, 3, 2};
                unsigned sum = 0;
                for (std::size_t i = 0; i < 10; ++i) sum += static_cast<unsigned>(first[i] - '0') * w.at(i);
                if (11 - sum % 11 == 10) prefix = "33";
            }
            const std::string ten = prefix + body;
            return shown(ten + ar_cuit_check_(ten), {10});
        }
        if (b == "at_fn")
        {
            constexpr std::string_view letters = "abdfghikmpstvwxyz";
            built_number out;
            out.value = join_({"FN ", std::to_string(core::uniform_int<std::uint64_t>(r, 1, 620000)),
                               std::string(1, letters.at(core::uniform_int<std::size_t>(r, 0, letters.size() - 1)))});
            return out;
        }
        if (b == "au_abn")
        {
            std::string nine;
            if (x.f.group >= 2)
            {
                if (const auto acn = base_(x, "au-acn")) nine = *acn;
            }
            if (nine.size() != 9) nine = digits_(r, 9);
            const std::string prefix = au_abn_check_(nine);
            return shown(prefix + nine, {0, 1});
        }
        if (b == "br_cnpj")
        {
            const auto from = d::parse_date(s.param("alphanumeric_from"));
            const bool alnum = from && founded_days_(x) >= *from;
            constexpr std::string_view chars = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
            std::string root;
            while (root.empty() || root == "00000000")
            {
                root.clear();
                for (int i = 0; i < 8; ++i)
                {
                    root += alnum ? chars.at(core::uniform_int<std::size_t>(r, 0, chars.size() - 1))
                                  : static_cast<char>('0' + core::uniform_int<int>(r, 0, 9));
                }
            }
            const std::string twelve = root + "0001";
            return shown(twelve + br_cnpj_check_(twelve), {12, 13});
        }
        if (b == "cn_uscc")
        {
            const auto type = mapped_(s, "type", x);
            if (!type || *type == "-") return std::nullopt;
            const std::string list{s.param("county")};
            const auto& county = list_(list).code.at(choose_(x, list));
            std::string org;
            if (founded_days_(x) >= ch::sys_days{ch::year{2015} / ch::October / 1})
            {
                org = "MA";
                for (int i = 0; i < 6; ++i) org += uscc_alphabet_.at(core::uniform_int<std::size_t>(r, 0, uscc_alphabet_.size() - 1));
            }
            else org = digits_(r, 8);
            org += org_code_check_(org);
            const std::string seventeen = *type + county + org;
            built_number out;
            out.value = seventeen + cn_uscc_check_(seventeen);
            out.check_at = {17};
            return out;
        }
        if (b == "de_hrn")
        {
            const auto section = mapped_(s, "section", x);
            if (!section || section->empty() || *section == "-") return std::nullopt;
            const std::string list{s.param("court")};
            const auto& courts = list_(list);
            const auto i = choose_(x, list);
            const auto& extra = courts.extra.at(i);
            std::uint64_t lo = 1;
            std::uint64_t hi = 0;
            if (extra.find('-') != std::string::npos) std::tie(lo, hi) = d::parse_range(extra);
            else hi = extra.empty() ? 20000 : d::to_number(extra);
            const double span = static_cast<double>(hi - lo);
            const double share = *section == "HRB" ? 1.0 : *section == "HRA" ? 0.4 : *section == "VR" ? 0.15 : 0.02;
            const auto top = lo + static_cast<std::uint64_t>(span * share);
            // register numbers grow with the year of entry, most entered or renumbered since 1990
            const double u = core::uniform01(r);
            const double q = year < 1990 ? 0.1 * u : 0.1 + 0.9 * (static_cast<double>(year - 1990) + u) /
                                                              static_cast<double>(std::max(1, x.p.reference_year - 1990 + 1));
            const auto n = std::clamp(lo + static_cast<std::uint64_t>(q * static_cast<double>(top - lo)), lo, std::max(lo, top));
            const auto suffix = d::map_lookup(s.param("suffix"), {courts.code.at(i)});
            const std::string number = *section + " " + std::to_string(n) + (suffix ? *suffix : "");
            built_number out;
            out.value = courts.label.at(i) + " " + number;
            out.compact = courts.code.at(i) + " " + number;
            return out;
        }
        if (b == "es_nif")
        {
            const auto letter = mapped_(s, "letter", x);
            if (!letter || letter->size() != 1) return std::nullopt;
            const std::string body = s.pattern ? s.pattern->draw(r) : digits_(r, 7);
            const auto checks = es_cif_checks_(body);
            const bool letter_check = std::string_view{"NPQRSW"}.find(letter->front()) != std::string_view::npos;
            return shown(*letter + body + (letter_check ? checks[1] : checks[0]), {8});
        }
        if (b == "fr_siret" || b == "fr_tva")
        {
            auto siren = base_(x, "fr-siren");
            if (!siren || siren->size() != 9)
            {
                const auto eight = digits_(r, 8);
                siren = eight + core::luhn_digit(eight);
            }
            if (b == "fr_siret")
            {
                const std::string head = *siren + "0001";
                return shown(head + core::luhn_digit(head), {13});
            }
            const auto key = (d::to_number(*siren) * 100 + 12) % 97;
            return shown(d::zero_pad(key, 2) + *siren, {0, 1});
        }
        if (b == "gb_crn")
        {
            const auto series = mapped_(s, "series", x);
            if (!series || *series == "-") return std::nullopt;
            const double u = core::uniform01(r);
            const double scotland = d::to_double(s.param("scotland"));
            const double ni = d::to_double(s.param("northern_ireland"));
            const std::size_t region = u < scotland ? 1 : u < scotland + ni ? 2 : 0;
            std::string prefix;
            if (series->empty()) prefix = region == 1 ? "SC" : region == 2 ? "NI" : "";
            else
            {
                const auto alts = d::split_view(*series, '|');
                prefix = std::string{alts.at(std::min(region, alts.size() - 1))};
            }
            const std::size_t width = 8 - prefix.size();
            std::uint64_t top = 1;
            for (std::size_t i = 0; i < width; ++i) top *= 10;
            --top;
            const auto it = _model.anchors.find("gb-crn|" + (prefix.empty() ? std::string{"-"} : prefix));
            const d::anchor_series* a = it == _model.anchors.end() ? nullptr : &it->second;
            built_number out;
            std::uint64_t v = 0;
            if (x.safe)
            {
                v = safe_draw_(s, a && a->highest ? *a->highest : top * 9 / 10, top, r);
                out.reserved = true;
            }
            else if (a && !a->years.empty()) v = std::min(top, anchor_draw_(*a, year, r));
            else v = core::uniform_int<std::uint64_t>(r, 1, a && a->highest ? std::min(top, *a->highest) : (top + 1) / 10);
            out.value = prefix + d::zero_pad(v, width);
            return out;
        }
        if (b == "gb_vat")
        {
            if (!registered()) return std::nullopt;
            const auto from = d::parse_date(s.param("series_9755_from"));
            const bool v9755 = from && founded_days_(x) >= *from;
            std::string body = digits_(r, 7);
            while (body.front() == '0') body = digits_(r, 7);
            const unsigned sum = gb_vat_sum_(body + "00");
            const unsigned cc = ((v9755 ? 42U : 0U) + 97U - sum) % 97U;
            return shown(body + d::zero_pad(cc, 2), {7, 8});
        }
        if (b == "hu_adoszam")
        {
            const std::string seven = digits_(r, 7);
            const auto check = d::compute_check(s.check, seven);
            const std::string list{s.param("county")};
            const auto& county = list_(list).code.at(choose_(x, list));
            return shown(seven + *check + std::string{s.param("vat_code")} + county, {7});
        }
        if (b == "id_npwp")
        {
            const auto type = mapped_(s, "type", x);
            if (!type) return std::nullopt;
            const std::string eight = join_({d::draw_choice(*type, r), digits_(r, 6)});
            const std::string fifteen = eight + core::luhn_digit(eight) + d::draw_choice(s.param("kpp"), r) + "000";
            const auto from = d::parse_date(s.param("new_from"));
            built_number out;
            if (from && x.p.reference >= *from)
            {
                out.value = "0" + fifteen;
                out.check_at = {9};
            }
            else
            {
                out.value = d::apply_display("##.###.###.#-###.###", fifteen);
                out.check_at = {8};
            }
            return out;
        }
        if (b == "ie_vat")
        {
            const std::string seven = digits_(r, 7);
            const auto from = d::parse_date(s.param("second_letter_from"));
            std::string second;
            if (from && founded_days_(x) >= *from) second = d::draw_choice(s.param("second"), r);
            built_number out;
            out.value = "IE" + seven + ie_vat_check_(seven, second) + second;
            out.check_at = {9};
            return out;
        }
        if (b == "il_hp")
        {
            const auto prefix = mapped_(s, "prefix", x);
            if (!prefix || *prefix == "-") return std::nullopt;
            const std::string eight = join_({d::draw_choice(*prefix, r), digits_(r, 6)});
            return shown(eight + core::luhn_digit(eight), {8});
        }
        if (b == "in_cin")
        {
            const auto ownership = mapped_(s, "ownership", x);
            const auto listing = mapped_(s, "listing", x);
            if (!ownership || *ownership == "-") return std::nullopt;
            const std::string list{s.param("state")};
            const auto& state = list_(list).extra.at(choose_(x, list));
            const std::string nic = join_({two_digits_(x.division), std::to_string(core::uniform_int<int>(r, 1, 9)),
                                           digits_(r, 1), std::to_string(core::uniform_int<int>(r, 1, 9))});
            const std::string listed = listing ? d::draw_choice(*listing, r) : std::string{"U"};
            built_number out;
            out.value = listed + nic + state + std::to_string(year) + *ownership +
                        d::zero_pad(core::uniform_int<std::uint64_t>(r, 1, 450000), 6);
            return out;
        }
        if (b == "in_pan")
        {
            const auto type = mapped_(s, "type", x);
            if (!type || *type == "-") return std::nullopt;
            built_number out;
            out.value = pan_(x);
            return out;
        }
        if (b == "in_gstin")
        {
            if (!registered()) return std::nullopt;
            const std::string list{s.param("state")};
            const auto& state = list_(list).code.at(choose_(x, list));
            const std::string fourteen = state + pan_(x) + "1Z";
            built_number out;
            out.value = fourteen + d::luhn36_char(fourteen);
            out.check_at = {14};
            return out;
        }
        if (b == "it_piva")
        {
            std::string seven = digits_(r, 7);
            while (seven == "0000000") seven = digits_(r, 7);
            const std::string ten = seven + d::draw_choice(s.param("office"), r);
            return shown(ten + core::luhn_digit(ten), {10});
        }
        if (b == "jp_cn")
        {
            const auto cls = mapped_(s, "class", x);
            if (!cls || *cls == "-") return std::nullopt;
            const std::string twelve = join_({d::draw_choice(s.param("office"), r), *cls, digits_(r, 6)});
            return shown(jp_cn_check_(twelve) + twelve, {0});
        }
        if (b == "kr_brn")
        {
            const auto type = mapped_(s, "type", x);
            if (!type || *type == "-") return std::nullopt;
            const std::string nine = join_({d::draw_choice(s.param("office"), r), d::draw_choice(*type, r),
                                            d::zero_pad(core::uniform_int<std::uint64_t>(r, 1, 9999), 4)});
            return shown(nine + kr_brn_check_(nine), {9});
        }
        if (b == "mx_rfc")
        {
            // the first letters of the name's words, the stopwords skipped
            static constexpr std::array<std::string_view, 22> stop{
                "DE", "DEL", "LA", "LAS", "LOS", "EL", "Y", "E", "EN", "CON", "POR", "PARA", "THE", "AND", "OF",
                "SA", "CV", "SC", "COMPANIA", "CIA", "SOCIEDAD", "A"};
            std::vector<std::string> words(1);
            for (std::size_t i = 0; i < x.name.latin.size();)
            {
                std::size_t used = 1;
                const char32_t cp = d::decode_utf8(x.name.latin, i, used);
                i += used;
                std::string letters;
                if (cp >= 'a' && cp <= 'z') letters = std::string(1, static_cast<char>(cp - 'a' + 'A'));
                else if (cp >= 'A' && cp <= 'Z') letters = std::string(1, static_cast<char>(cp));
                else if (const auto it = _model.fold.find(cp); it != _model.fold.end())
                {
                    for (const char c2 : it->second)
                    {
                        if (c2 >= 'a' && c2 <= 'z') letters += static_cast<char>(c2 - 'a' + 'A');
                        else if (c2 >= 'A' && c2 <= 'Z') letters += c2;
                    }
                }
                if (!letters.empty()) words.back() += letters;
                else if (cp != '.' && !words.back().empty()) words.emplace_back();
            }
            std::erase_if(words, [](const std::string& w) { return w.empty() || std::ranges::find(stop, w) != stop.end(); });
            std::string head;
            if (words.size() >= 3) head = std::string{words[0][0], words[1][0], words[2][0]};
            else if (words.size() == 2) head = std::string(1, words[0][0]) + words[1].substr(0, 2);
            else if (words.size() == 1) head = words[0].substr(0, 3);
            while (head.size() < 3) head += 'X';
            const auto ymd = x.founded;
            const std::string date = d::zero_pad(static_cast<std::uint64_t>(static_cast<int>(ymd.year()) % 100), 2) +
                                     d::zero_pad(static_cast<unsigned>(ymd.month()), 2) + d::zero_pad(static_cast<unsigned>(ymd.day()), 2);
            constexpr std::string_view first = "123456789ABCDEFGHIJKLMNOPQRSTUV";
            constexpr std::string_view second = "123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
            const std::string eleven = join_({head, date, std::string(1, first.at(core::uniform_int<std::size_t>(r, 0, first.size() - 1))),
                                              std::string(1, second.at(core::uniform_int<std::size_t>(r, 0, second.size() - 1)))});
            built_number out;
            out.value = eleven + mx_rfc_check_(eleven);
            out.check_at = {11};
            return out;
        }
        if (b == "ng_rc")
        {
            const auto prefix = mapped_(s, "prefix", x);
            if (!prefix || *prefix == "-") return std::nullopt;
            const auto highest = d::map_lookup(s.param("highest"), {*prefix});
            const std::uint64_t hi = highest ? d::to_number(*highest) : 1000000;
            built_number out;
            std::uint64_t v = 0;
            if (x.safe)
            {
                v = safe_draw_(s, hi, 9999999, r);
                out.reserved = true;
            }
            else v = serial_by_year_(s, x, 1, hi, r);
            out.value = *prefix + " " + std::to_string(v);
            return out;
        }
        if (b == "nl_btw")
        {
            if (x.f.group == 0)
            {
                // in safe mode not a number the 11-proof accepts, which passes whatever its suffix
                std::string nine = digits_(r, 9);
                while (d::to_number(nine) == 0 || (x.safe && bsn_valid_(nine))) nine = digits_(r, 9);
                const auto c = core::mod97_10_digits("NL" + nine + "B");
                return shown(nine + std::string{c[0], c[1]}, {9, 10});
            }
            auto rsin = base_(x, "nl-rsin");
            if (!rsin || rsin->size() != 9)
            {
                const auto at = std::ranges::find_if(x.c.schemes, [&](std::size_t i) { return _model.schemes.at(i).key == "nl-rsin"; });
                if (at == x.c.schemes.end()) return std::nullopt;
                auto rr = x.numbers.split("nl-rsin");
                const auto made = build_generic_(_model.schemes.at(*at), x, rr);
                if (!made) return std::nullopt;
                rsin = d::alnum_of(made->value);
            }
            return shown(*rsin + "01", {8});
        }
        if (b == "ro_onrc")
        {
            const auto prefix = mapped_(s, "prefix", x);
            if (!prefix || *prefix == "-") return std::nullopt;
            const std::string list{s.param("county")};
            const auto& county = list_(list).code.at(choose_(x, list));
            const auto serial = core::uniform_int<std::uint64_t>(r, 1, county == "40" ? 60000 : 9000);
            const int entered = std::max(year, 1990);
            const auto from = d::parse_date(s.param("new_format_from"));
            built_number out;
            if (from && x.p.reference >= *from)
            {
                const std::string thirteen = *prefix + std::to_string(entered) + d::zero_pad(serial, 6) +
                                             (founded_days_(x) >= *from ? std::string{"00"} : county);
                out.value = thirteen + ro_onrc_check_(thirteen);
                out.check_at = {13};
                return out;
            }
            if (x.safe) return std::nullopt; // the old format has no check
            out.value = *prefix + county + "/" + std::to_string(serial) + "/" + std::to_string(entered);
            return out;
        }
        if (b == "ru_inn")
        {
            const std::string list{s.param("region")};
            const auto& region = list_(list).code.at(choose_(x, list));
            for (int attempt = 0; attempt < 50; ++attempt)
            {
                const std::string nine = join_({region, d::draw_choice(s.param("inspection"), r), digits_(r, 5)});
                const auto check = d::compute_check(s.check, nine);
                if (check) return shown(nine + *check, {9});
            }
            return std::nullopt;
        }
        if (b == "ru_ogrn")
        {
            const std::string list{s.param("region")};
            const auto& region = list_(list).code.at(choose_(x, list));
            const auto start = d::parse_date(s.param("start"));
            int entered = year;
            if (start && founded_days_(x) < *start) entered = core::uniform_int<int>(r, 2002, 2003);
            const std::string yy = d::zero_pad(static_cast<std::uint64_t>(entered % 100), 2);
            for (int attempt = 0; attempt < 50; ++attempt)
            {
                std::string head;
                if (x.f.group == 0) head = "3" + yy + region + digits_(r, 9);
                else
                {
                    head = join_({std::string(1, core::uniform01(r) < 0.6 ? '1' : '5'), yy, region,
                                  d::zero_pad(core::uniform_int<std::uint64_t>(r, 1, 99), 2), digits_(r, 5)});
                }
                const auto c = ru_ogrn_check_(head);
                if (c) return shown(head + *c, {head.size()});
            }
            return std::nullopt;
        }
        if (b == "se_orgnr")
        {
            const auto group = mapped_(s, "group", x);
            if (!group || *group == "-") return std::nullopt;
            const std::string nine = join_({d::draw_choice(*group, r), digits_(r, 1),
                                            std::to_string(core::uniform_int<int>(r, 20, 99)), digits_(r, 5)});
            return shown(nine + core::luhn_digit(nine), {9});
        }
        if (b == "sg_uen")
        {
            const auto format = mapped_(s, "format", x);
            if (!format || *format == "-") return std::nullopt;
            std::string body;
            if (*format == "business") body = digits_(r, 8);
            else if (*format == "local") body = std::to_string(year) + digits_(r, 5);
            else
            {
                const char era = year >= 2000 ? 'T' : year >= 1900 ? 'S' : 'R';
                body = join_({std::string(1, era), d::zero_pad(static_cast<std::uint64_t>(year % 100), 2),
                              d::draw_choice(s.param("other_type"), r), digits_(r, 4)});
            }
            built_number out;
            out.value = body + sg_check_(body, *format == "business" || *format == "local" ? std::string_view{*format} : "other");
            out.check_at = {body.size()};
            return out;
        }
        if (b == "th_juristic")
        {
            const auto type = mapped_(s, "type", x);
            if (!type || *type == "-") return std::nullopt;
            const std::string list{s.param("office")};
            const auto& office = list_(list).code.at(choose_(x, list));
            const std::string twelve = "0" + office + *type + d::zero_pad(static_cast<std::uint64_t>((year + 543) % 1000), 3) +
                                       d::zero_pad(core::uniform_int<std::uint64_t>(r, 1, office == "10" ? 40000 : 3000), 5);
            const auto check = d::compute_check(s.check, twelve);
            return shown(twelve + *check, {12});
        }
        if (b == "us_ein")
        {
            if (x.f.group == 0 && x.employees <= 1) return std::nullopt; // the owner's SSN
            built_number out;
            std::string_view list = "campus";
            if (x.safe)
            {
                list = "reserved";
                out.reserved = true;
            }
            else
            {
                const auto from = d::parse_date(s.param("internet_from"));
                if (from && founded_days_(x) >= *from && core::uniform01(r) < d::to_double(s.param("internet_share"))) list = "internet";
            }
            const auto prefixes = d::split_view(s.param(list), ' ');
            const auto prefix = prefixes.at(core::uniform_int<std::size_t>(r, 0, prefixes.size() - 1));
            const auto shown_number = shown(std::string{prefix} + digits_(r, 7), {});
            out.value = shown_number.value;
            return out;
        }
        if (b == "vn_mst")
        {
            for (int attempt = 0; attempt < 50; ++attempt)
            {
                const std::string nine = join_({d::draw_choice(s.param("province"), r), digits_(r, 7)});
                if (const auto c = vn_mst_check_(nine)) return shown(nine + *c, {9});
            }
            return std::nullopt;
        }
        if (b == "za_cipc")
        {
            const auto type = mapped_(s, "type", x);
            if (!type || *type == "-") return std::nullopt;
            built_number out;
            std::uint64_t v = 0;
            if (x.safe)
            {
                v = safe_draw_(s, 0, 999999, r);
                out.reserved = true;
            }
            else
            {
                const auto [lo, hi] = d::parse_range(s.param("range"));
                v = core::uniform_int<std::uint64_t>(r, lo, hi);
            }
            out.value = std::to_string(year) + "/" + d::zero_pad(v, 6) + "/" + *type;
            return out;
        }
        throw std::runtime_error("company: no builder " + b);
    }

    // The company's numbers, register first, then statistical, tax and VAT numbers, then the LEI.
    [[nodiscard]] std::vector<registration> draw_numbers_(number_context& x) const
    {
        namespace d = companygen_detail;
        std::vector<registration> out;
        const auto one = [&](const d::scheme_model& s, core::rng r)
        {
            if (((s.groups >> x.f.group) & 1U) == 0) return;
            if ((s.from && x.p.reference < *s.from) || (s.until && x.p.reference > *s.until)) return;
            const bool needed = s.key == "in-pan"; // the GSTIN wraps the PAN
            if (x.safe && s.safe == d::safe_method::none && !needed) return;
            auto made = s.builder == "generic" ? build_generic_(s, x, r) : build_named_(s, x, r);
            if (!made) return;
            if (x.safe && s.safe == d::safe_method::wrong_check)
            {
                // change a check character until the number fails its check
                auto sr = r.split("safe");
                for (int attempt = 0; attempt < 64; ++attempt)
                {
                    const auto compact = d::alnum_of(made->value);
                    if (attempt > 0 && !number_valid_(s, compact)) break;
                    if (made->check_at.empty()) break;
                    const auto at = made->check_at.at(core::uniform_int<std::size_t>(sr, 0, made->check_at.size() - 1));
                    std::size_t k = 0;
                    for (auto& c : made->value)
                    {
                        if (!d::is_alnum(c)) continue;
                        if (k++ == at)
                        {
                            c = d::other_char(c, sr);
                            break;
                        }
                    }
                }
                made->reserved = true;
            }
            const std::string compact = made->compact.empty() ? d::alnum_of(made->value) : made->compact;
            x.built[s.key] = compact;
            if (x.safe && s.safe == d::safe_method::none) return; // built only for the numbers that wrap it
            out.push_back({.scheme = s.key, .label = s.short_label, .value = made->value, .compact = compact, .reserved = made->reserved});
        };
        for (const auto i : x.c.schemes)
        {
            const auto& s = _model.schemes.at(i);
            one(s, x.numbers.split(s.key));
        }
        // decision 9: an LEI for companies with 250 persons or more or in finance, never for sole traders
        if (_model.lei != d::no_index && (x.employees >= 250 || x.section == 10))
        {
            one(_model.schemes.at(_model.lei), x.lei);
        }
        return out;
    }

    // -- State ------------------------------------------------------------

    tables _tables{make_tables_()};
    companygen_detail::model _model;
    core::rng _engine{core::rng::from_entropy()};

    struct auto_probe_tag {};

    explicit cpg(auto_probe_tag /*tag*/)
    {
        if (!load(dataset::lite)) (void)load(dataset::full);
    }

    // -- Helpers ----------------------------------------------------------

    static std::span<const core::embedded_file> embedded_files_()
    {
#ifdef DASMIG_COMPANY_EMBED
        return embedded::companygen::files;
#else
        return {};
#endif
    }

    std::uint64_t draw_seed_() { return _engine(); }

    void require_data_() const
    {
        if (!has_data()) throw std::runtime_error("No company data loaded. Call load() first.");
    }
};

} // namespace dasmig

#endif // DASMIG_COMPANYGEN_HPP
