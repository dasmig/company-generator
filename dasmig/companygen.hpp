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
    anchors      ///< anchors.tsv: issue anchors of sequential numbers.
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
    std::optional<std::string> founder_surname{}; ///< From name-generator (used from Milestone 5).
    std::optional<std::string> place{};           ///< From city-generator (used from Milestone 5).
    std::chrono::year_month_day reference_date{default_reference_date}; ///< Bounds the founding date and picks the number rules in force.
    bool safe{false};                             ///< Numbers and domains nobody holds (used from Milestone 5).
    bool weighted{true};                          ///< False: countries uniform, the rest by the data.
};

/// @brief One number a company holds.
struct registration
{
    std::string scheme;  ///< The scheme id: `"fr-siren"`, `"de-ust-idnr"`, `"lei"`.
    std::string label;   ///< `"SIREN"`, `"USt-IdNr."`, `"LEI"`.
    std::string value;   ///< As printed: `"552 100 554"`, `"DE123456789"`.
    std::string compact; ///< Digits and letters only.
    bool reserved{false}; ///< Drawn so that no company holds it (safe mode).
};

/// @brief Where the data behind each field of a country comes from.
///
/// Each is `"measured"`, `"modelled"` or `"filled"`; empty for a field not
/// yet drawn by this version.
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
    std::string name;                          ///< `"Bäckerei Hoffmann GmbH"` (from Milestone 5).
    std::string short_name;                    ///< The name without the legal form (from Milestone 5).
    std::string isic;                          ///< The ISIC Rev. 4 division: `"10"`.
    std::string isic_label;                    ///< Its title: `"manufacture of food products"`.
    char isic_section{'A'};                    ///< Its section: `'C'`.
    size_band size{size_band::micro};          ///< The band of employees.
    std::uint32_t employees{1};                ///< Persons employed, the owner counted.
    std::string legal_form;                    ///< The abbreviation (the name when the form has none): `"GmbH"`.
    std::string legal_form_code;               ///< ISO 20275 code: `"2HBR"`; empty for curated forms.
    std::string legal_form_group;              ///< `"sole_proprietorship"`, `"partnership"`, `"limited_liability"` or `"other"`.
    std::chrono::year_month_day founded{};     ///< The founding date.
    std::vector<registration> numbers;         ///< The register's, then tax and VAT, then the LEI (from Milestone 5).
    std::string domain;                        ///< `"baeckerei-hoffmann.de"` (from Milestone 5).
    std::string evidence;                      ///< The weakest of the fields drawn: `"measured"`, `"modelled"` or `"filled"`.

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
    std::string code;
    std::string abbreviation;
    std::string latin_abbreviation;
    std::string name;
    std::string latin_name;
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
    std::array<double, band_count> no_domain{};
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
};

using isco_row = std::array<double, section_count>;
struct occupation_table
{
    std::array<isco_row, isco_groups> rows{};
    std::array<bool, isco_groups> present{};
};

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
};

} // namespace companygen_detail
/// @endcond

/// @brief Company Generator: companies by country.
///
/// The call seed splits into the streams `"country"`, `"industry"`, `"size"`,
/// `"employees"`, `"form"` and `"founded"` (and, from Milestone 5, `"name"`,
/// `"numbers"`, `"domain"` and `"lei"`). A given field skips its draw and
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
    /// @return Per field: measured, modelled or filled (name and numbers are
    ///         empty until Milestone 5).
    /// @throws std::invalid_argument For an unknown country.
    [[nodiscard]] company_evidence evidence(std::string_view country) const
    {
        namespace d = companygen_detail;
        const auto& c = country_or_throw_(country);
        return {.industry = d::evidence_name(c.industry_evidence),
                .size = d::evidence_name(c.size_evidence),
                .legal_form = d::evidence_name(c.form_evidence),
                .age = d::evidence_name(c.age_evidence),
                .name = {},
                .numbers = {},
                .domain = d::evidence_name(c.domain_evidence)};
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
    static constexpr std::size_t table_count_ = 16;
    static constexpr std::array<table, table_count_> all_tables_{
        table::countries, table::isic,        table::industries, table::sizes,   table::ages,    table::groups,
        table::form_odds, table::legal_forms, table::occupations, table::words,  table::patterns, table::surnames,
        table::places,    table::domains,     table::schemes,    table::anchors};

    using documents = std::array<std::optional<core::tsv_document>, table_count_>;
    using tables = std::array<core::layered_table, table_count_>;

    static constexpr std::size_t index_(table t) { return static_cast<std::size_t>(t); }

    static constexpr std::string_view file_name_(table t)
    {
        constexpr std::array<std::string_view, table_count_> names{
            "countries.tsv",   "isic.tsv",    "industries.tsv", "sizes.tsv",  "ages.tsv",     "groups.tsv",
            "form_odds.tsv",   "legal_forms.tsv", "occupations.tsv", "words.tsv", "patterns.tsv", "surnames.tsv",
            "places.tsv",      "domains.tsv", "schemes.tsv",    "anchors.tsv"};
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
                                 "young_share", "domain_evidence", "no_domain_micro", "no_domain_small",
                                 "no_domain_medium", "no_domain_large"},
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
                    .optional = {}};
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
                    .required = {"country_code", "kind", "label", "native_label", "builder", "display", "pattern",
                                 "check", "groups", "safe_method", "optional", "from", "until", "params", "evidence"},
                    .optional = {"source"}};
        case table::anchors:
            return {.key = {"scheme", "series", "year"},
                    .required = {"q001", "q10", "q25", "q50", "q75", "q90", "q999", "count"},
                    .optional = {"source"}};
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
                core::layered_table{schema_(table::schemes)},     core::layered_table{schema_(table::anchors)}};
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
            for (const auto c : {"no_domain_micro", "no_domain_small", "no_domain_medium", "no_domain_large"})
            {
                if (number_(row, c) > 1.0) throw row.error(c, "must be 0 to 1");
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
            break;
        }
    }

    // A legal_forms row as a form_row (row errors located at the row).
    static form_row parse_form_(const core::tsv_row& row)
    {
        namespace d = companygen_detail;
        form_row f;
        f.code = std::string{row["code"]};
        f.abbreviation = std::string{row["abbreviation"]};
        f.latin_abbreviation = std::string{row["latin_abbreviation"]};
        f.name = std::string{row["name"]};
        f.latin_name = std::string{row["latin_name"]};
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
            const std::array<std::string_view, d::band_count> no_domain{"no_domain_micro", "no_domain_small",
                                                                        "no_domain_medium", "no_domain_large"};
            for (std::size_t b = 0; b < d::band_count; ++b) c.no_domain.at(b) = row.get<double>(no_domain.at(b));
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
