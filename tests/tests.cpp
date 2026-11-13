#include "catch_amalgamated.hpp"
#include "../dasmig/companygen.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <map>
#include <set>
#include <sstream>
#include <string>

namespace
{

using namespace std::chrono_literals;
using dasmig::company;
using dasmig::company_request;
using dasmig::company_table;
using dasmig::cpg;
using dasmig::size_band;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

cpg make_loaded_generator()
{
    cpg gen;
    if (!gen.load(dasmig::dataset::lite))
    {
        for (const auto* candidate : {"resources/lite", "../resources/lite"})
        {
            if (std::filesystem::is_directory(candidate))
            {
                gen.load(std::filesystem::path{candidate});
                break;
            }
        }
    }
    REQUIRE(gen.has_data());
    return gen;
}

company_request in(const std::string& country)
{
    company_request r;
    r.country = country;
    return r;
}

std::uint64_t fnv(std::uint64_t h, const std::string& text)
{
    return dasmig::core::hash_combine(h, dasmig::core::fnv1a64(text));
}

bool in_band(size_band band, std::uint32_t employees)
{
    switch (band)
    {
    case size_band::micro: return employees >= 1 && employees <= 9;
    case size_band::small: return employees >= 10 && employees <= 49;
    case size_band::medium: return employees >= 50 && employees <= 249;
    case size_band::large: return employees >= 250;
    }
    return false;
}

} // namespace

// ---------------------------------------------------------------------------
// Loading
// ---------------------------------------------------------------------------

TEST_CASE("load(dataset::lite) loads every table", "[loading]")
{
    const auto gen = make_loaded_generator();
    REQUIRE(gen.countries().size() == 199);
    REQUIRE(gen.divisions().size() == 85);
    REQUIRE(gen.data_hash() != 0);
    for (const auto table : {company_table::countries, company_table::isic, company_table::industries,
                             company_table::sizes, company_table::ages, company_table::groups, company_table::form_odds,
                             company_table::legal_forms, company_table::occupations, company_table::words,
                             company_table::patterns, company_table::surnames, company_table::places,
                             company_table::domains, company_table::schemes, company_table::anchors})
    {
        REQUIRE(gen.layers(table).size() == 1);
    }
}

TEST_CASE("load - non-existent path is a no-op", "[loading]")
{
    cpg gen;
    gen.load(std::filesystem::path{"no/such/directory"});
    REQUIRE_FALSE(gen.has_data());
    REQUIRE(gen.data_hash() == 0);
}

#ifndef DASMIG_COMPANY_EMBED // compiled-in data is never missing
TEST_CASE("load(dataset) returns false for a missing tier", "[loading]")
{
    const auto old_cwd = std::filesystem::current_path();
    const auto tmp = std::filesystem::temp_directory_path() / "cpg_empty_dir";
    std::filesystem::create_directories(tmp);
    std::filesystem::current_path(tmp);
    cpg gen;
    const bool loaded = gen.load(dasmig::dataset::full);
    std::filesystem::current_path(old_cwd);
    std::filesystem::remove_all(tmp);
    REQUIRE_FALSE(loaded);
    REQUIRE_FALSE(gen.has_data());
}
#endif

TEST_CASE("the singleton loads data on first use", "[loading]")
{
    REQUIRE(cpg::instance().has_data());
}

TEST_CASE("get_company throws when no data is loaded", "[generation]")
{
    cpg gen;
    REQUIRE_THROWS_AS(gen.get_company(company_request{}), std::runtime_error);
    REQUIRE_THROWS_AS(gen.get_company(company_request{}, std::uint64_t{1}), std::runtime_error);
    REQUIRE_THROWS_AS(gen.legal_forms("DE"), std::runtime_error);
}

// ---------------------------------------------------------------------------
// Determinism
// ---------------------------------------------------------------------------

TEST_CASE("get_company with a seed is deterministic", "[generation][seed]")
{
    const auto gen = make_loaded_generator();
    for (std::uint64_t s = 0; s < 200; ++s)
    {
        REQUIRE(gen.get_company(company_request{}, s).to_json() == gen.get_company(company_request{}, s).to_json());
    }
}

TEST_CASE("a result replays from its seed", "[generation][seed]")
{
    auto gen = make_loaded_generator();
    const auto v = gen.get_company(company_request{});
    REQUIRE(gen.get_company(company_request{}, v.seed()).to_json() == v.to_json());
}

TEST_CASE("explicit per-call seeds replay across generator instances", "[seed]")
{
    const auto first = make_loaded_generator();
    const auto second = make_loaded_generator();
    REQUIRE(first.get_company(company_request{}, 42).to_json() == second.get_company(company_request{}, 42).to_json());
}

TEST_CASE("forcing the country or the industry to what was drawn changes nothing else", "[generation][seed]")
{
    const auto gen = make_loaded_generator();
    for (std::uint64_t s = 1; s <= 300; ++s)
    {
        const auto base = gen.get_company(company_request{}, s);
        auto with_country = company_request{};
        with_country.country = base.country_code;
        REQUIRE(gen.get_company(with_country, s).to_json() == base.to_json());
        with_country.isic = base.isic;
        REQUIRE(gen.get_company(with_country, s).to_json() == base.to_json());
    }
}

// ---------------------------------------------------------------------------
// Requests
// ---------------------------------------------------------------------------

TEST_CASE("a forced country is honoured in any case", "[request]")
{
    const auto gen = make_loaded_generator();
    REQUIRE(gen.get_company(in("de"), 1).country_code == "DE");
    REQUIRE(gen.get_company(in("Br"), 1).country_code == "BR");
    REQUIRE_THROWS_WITH(gen.get_company(in("ZZ"), 1), "unknown country 'ZZ'");
}

TEST_CASE("a forced industry is honoured", "[request]")
{
    const auto gen = make_loaded_generator();
    auto r = in("DE");
    r.isic = "43";
    for (std::uint64_t s = 0; s < 50; ++s)
    {
        const auto c = gen.get_company(r, s);
        REQUIRE(c.isic == "43");
        REQUIRE(c.isic_section == 'F');
        REQUIRE(c.isic_label == "specialized construction activities");
    }
    r.isic = "f";
    for (std::uint64_t s = 0; s < 200; ++s)
    {
        const auto c = gen.get_company(r, s);
        REQUIRE(c.isic_section == 'F');
        REQUIRE((c.isic == "41" || c.isic == "42" || c.isic == "43"));
    }
    for (const auto* bad : {"T", "44", "430", "", "4"})
    {
        r.isic = bad;
        REQUIRE_THROWS_AS(gen.get_company(r, 1), std::invalid_argument);
    }
}

TEST_CASE("a forced size or head count is honoured", "[request]")
{
    const auto gen = make_loaded_generator();
    for (const auto band : {size_band::micro, size_band::small, size_band::medium, size_band::large})
    {
        auto r = in("FR");
        r.size = band;
        for (std::uint64_t s = 0; s < 100; ++s)
        {
            const auto c = gen.get_company(r, s);
            REQUIRE(c.size == band);
            REQUIRE(in_band(c.size, c.employees));
        }
    }
    auto r = in("FR");
    r.employees = 12;
    const auto c = gen.get_company(r, 3);
    REQUIRE(c.employees == 12);
    REQUIRE(c.size == size_band::small);
    r.employees = 4'000;
    REQUIRE(gen.get_company(r, 3).size == size_band::large);
    r.size = size_band::micro;
    REQUIRE_THROWS_AS(gen.get_company(r, 3), std::invalid_argument);
    r.size = size_band::large;
    REQUIRE(gen.get_company(r, 3).employees == 4'000);
    r.employees = 0;
    REQUIRE_THROWS_AS(gen.get_company(r, 3), std::invalid_argument);
}

TEST_CASE("a forced legal form is honoured", "[request]")
{
    const auto gen = make_loaded_generator();
    auto r = in("DE");
    r.legal_form = "GmbH";
    for (std::uint64_t s = 0; s < 100; ++s)
    {
        const auto c = gen.get_company(r, s);
        REQUIRE(c.legal_form == "GmbH");
        REQUIRE(c.legal_form_code == "2HBR");
        REQUIRE(c.legal_form_group == "limited_liability");
    }
    r.legal_form = "2hbr"; // the ISO 20275 code, any case
    REQUIRE(gen.get_company(r, 1).legal_form == "GmbH");
    r.legal_form = "LLC";
    REQUIRE_THROWS_AS(gen.get_company(r, 1), std::invalid_argument);
    r.legal_form = "";
    REQUIRE_THROWS_AS(gen.get_company(r, 1), std::invalid_argument);
}

TEST_CASE("a legal form must be in force and fit the other fields", "[request][errors]")
{
    const auto gen = make_loaded_generator();
    auto r = in("DE");
    r.legal_form = "UG (haftungsbeschränkt)"; // the Unternehmergesellschaft, from 2008-11-01
    r.reference_date = 2000y / 1 / 1;
    REQUIRE_THROWS_WITH(gen.get_company(r, 1), Catch::Matchers::ContainsSubstring("not yet in force"));
    r.reference_date = 2025y / 7 / 1;
    for (std::uint64_t s = 0; s < 100; ++s)
    {
        REQUIRE(gen.get_company(r, s).founded >= 2008y / 11 / 1);
    }

    r = in("DE");
    r.legal_form = "e. K."; // a sole trader, barred from banking and insurance
    r.incorporated_only = true;
    REQUIRE_THROWS_WITH(gen.get_company(r, 1), Catch::Matchers::ContainsSubstring("not incorporated"));
    r.incorporated_only = false;
    r.isic = "64";
    REQUIRE_THROWS_WITH(gen.get_company(r, 1), Catch::Matchers::ContainsSubstring("these fields together"));
    r.isic = "K"; // only division 66 is open to it
    REQUIRE(gen.get_company(r, 1).isic == "66");
    r.isic = "47";
    REQUIRE(gen.get_company(r, 1).legal_form == "e. K.");
}

TEST_CASE("incorporated_only leaves out sole traders and partnerships", "[request]")
{
    const auto gen = make_loaded_generator();
    auto r = company_request{};
    r.incorporated_only = true;
    for (std::uint64_t s = 0; s < 3'000; ++s)
    {
        const auto c = gen.get_company(r, s);
        REQUIRE(c.legal_form_group != "sole_proprietorship");
        REQUIRE(c.legal_form_group != "partnership");
    }
}

TEST_CASE("the reference date bounds the founding date", "[request]")
{
    const auto gen = make_loaded_generator();
    auto r = company_request{};
    r.reference_date = 1990y / 3 / 15;
    for (std::uint64_t s = 0; s < 1'000; ++s)
    {
        REQUIRE(gen.get_company(r, s).founded <= 1990y / 3 / 15);
    }
    r.reference_date = std::chrono::year_month_day{2025y, std::chrono::month{13}, 1d};
    REQUIRE_THROWS_AS(gen.get_company(r, 1), std::invalid_argument);
    r.reference_date = 1800y / 1 / 1;
    REQUIRE_THROWS_AS(gen.get_company(r, 1), std::invalid_argument);
    r.reference_date = 2025y / 7 / 1;
    r.occupation_isco = "7x";
    REQUIRE_THROWS_AS(gen.get_company(r, 1), std::invalid_argument);
}

TEST_CASE("weights: employment draws bigger firms and uniform countries cover them all", "[request][stats]")
{
    const auto gen = make_loaded_generator();
    auto by_enterprises = in("DE");
    auto by_employment = in("DE");
    by_employment.weight = dasmig::company_weight::employment;
    double sum_enterprises = 0.0;
    double sum_employment = 0.0;
    constexpr int draws = 5'000;
    for (std::uint64_t s = 0; s < draws; ++s)
    {
        sum_enterprises += gen.get_company(by_enterprises, s).employees;
        sum_employment += gen.get_company(by_employment, s).employees;
    }
    REQUIRE(sum_employment > 10.0 * sum_enterprises);

    auto uniform = company_request{};
    uniform.weighted = false;
    std::set<std::string> countries;
    for (std::uint64_t s = 0; s < 5'000; ++s) countries.insert(gen.get_company(uniform, s).country_code);
    REQUIRE(countries.size() >= 190);
}

TEST_CASE("an occupation draws the industry of its workers", "[request][stats]")
{
    const auto gen = make_loaded_generator();
    auto farmers = in("FR");
    farmers.occupation_isco = "6111"; // skilled agricultural workers (major group 6)
    auto clerks = in("FR");
    clerks.occupation_isco = "4";     // clerical support workers
    int in_agriculture = 0;
    int clerks_in_agriculture = 0;
    constexpr int draws = 2'000;
    for (std::uint64_t s = 0; s < draws; ++s)
    {
        in_agriculture += gen.get_company(farmers, s).isic_section == 'A' ? 1 : 0;
        clerks_in_agriculture += gen.get_company(clerks, s).isic_section == 'A' ? 1 : 0;
    }
    REQUIRE(in_agriculture > draws / 2);
    REQUIRE(clerks_in_agriculture < draws / 20);
}

// ---------------------------------------------------------------------------
// Coherence
// ---------------------------------------------------------------------------

TEST_CASE("every company is coherent in every country", "[coherence]")
{
    const auto gen = make_loaded_generator();
    const auto divisions = gen.divisions();
    const std::chrono::year_month_day reference = company_request::default_reference_date;
    for (const auto& country : gen.countries())
    {
        const auto forms = gen.legal_forms(country);
        for (std::uint64_t s = 0; s < 300; ++s)
        {
            const auto c = gen.get_company(in(country), s);
            REQUIRE(c.country_code == country);
            REQUIRE(std::ranges::find(divisions, c.isic) != divisions.end());
            REQUIRE(c.isic_section >= 'A');
            REQUIRE(c.isic_section <= 'S');
            REQUIRE(!c.isic_label.empty());
            REQUIRE(in_band(c.size, c.employees));
            REQUIRE(c.founded.ok());
            REQUIRE(c.founded <= reference);
            REQUIRE(c.founded.year() >= 1900y);

            const auto form = std::ranges::find_if(forms, [&](const dasmig::legal_form_info& f)
                                                   { return f.code == c.legal_form_code &&
                                                            (f.abbreviation.empty() ? f.name : f.abbreviation) == c.legal_form &&
                                                            f.group == c.legal_form_group; });
            REQUIRE(form != forms.end());
            if (form->from) REQUIRE(c.founded >= *form->from);
            if (form->until) REQUIRE(c.founded <= *form->until);
            REQUIRE((c.evidence == "measured" || c.evidence == "modelled" || c.evidence == "filled"));
        }
    }
}

TEST_CASE("legal forms and evidence can be queried", "[query]")
{
    const auto gen = make_loaded_generator();
    const auto forms = gen.legal_forms("de");
    const auto gmbh = std::ranges::find_if(forms, [](const dasmig::legal_form_info& f) { return f.code == "2HBR"; });
    REQUIRE(gmbh != forms.end());
    REQUIRE(gmbh->abbreviation == "GmbH");
    REQUIRE(gmbh->group == "limited_liability");
    REQUIRE_FALSE(gmbh->from);
    const auto ug = std::ranges::find_if(forms, [](const dasmig::legal_form_info& f) { return f.code == "63KS"; });
    REQUIRE(ug != forms.end());
    REQUIRE(ug->from == 2008y / 11 / 1);
    REQUIRE_THROWS_AS(gen.legal_forms("ZZ"), std::invalid_argument);

    const auto de = gen.evidence("DE");
    REQUIRE(de.industry == "measured");
    REQUIRE(de.size == "measured");
    REQUIRE(de.legal_form == "measured");
    REQUIRE(de.age == "measured");
    REQUIRE(gen.evidence("AD").size == "filled");
    REQUIRE(de.name == "modelled");
    REQUIRE(de.numbers == "modelled");
    REQUIRE(gen.evidence("FR").numbers == "modelled");
    REQUIRE(gen.evidence("AD").numbers == "none");
    REQUIRE_THROWS_AS(gen.evidence("ZZ"), std::invalid_argument);
    REQUIRE(gen.get_company(in("AD"), 1).evidence == "filled");
}

// ---------------------------------------------------------------------------
// Names, domains and numbers
// ---------------------------------------------------------------------------

TEST_CASE("every scheme's worked examples pass and a changed check fails", "[numbers]")
{
    const auto gen = make_loaded_generator();
    // designs/company-generator-numbers.tsv; python-stdnum's MX doctest TES 030201 001 is left out:
    // its last three characters are not a homoclave and check
    const std::vector<std::pair<std::string, std::string>> examples{
        {"ar-cuit", "30-50001091-2"},
        {"ar-cuit", "20-26756539-3"},
        {"at-fn", "FN 122119m"},
        {"at-uid", "ATU13585627"},
        {"au-abn", "51 824 753 556"},
        {"au-abn", "83 914 571 673"},
        {"au-acn", "004 085 616"},
        {"au-acn", "010 499 966"},
        {"be-kbo", "0403.019.261"},
        {"be-kbo", "0428.759.497"},
        {"br-cnpj", "16.727.230/0001-97"},
        {"br-cnpj", "12.ABC.345/01DE-35"},
        {"ca-bn", "123026635"},
        {"ca-bn", "123026635RC0001"},
        {"ch-uid", "CHE-100.155.212"},
        {"cl-rut", "76.086.428-5"},
        {"cn-uscc", "91110000600037341L"},
        {"co-nit", "900.373.115-3"},
        {"cz-ico", "25123891"},
        {"de-hrn", "Amtsgericht Aachen HRA 11223"},
        {"de-hrn", "Amtsgericht München HRB 6684"},
        {"de-ustid", "DE136695976"},
        {"dk-cvr", "13585628"},
        {"eg-tn", "100-531-385"},
        {"es-nif-entity", "B58378431"},
        {"es-nif-entity", "B64717838"},
        {"fi-ytunnus", "2077474-0"},
        {"fr-siren", "552 008 443"},
        {"fr-siren", "404 833 048"},
        {"fr-siret", "732 829 320 00074"},
        {"fr-tva", "FR 40 303265045"},
        {"fr-tva", "FR 23 334175221"},
        {"gb-crn", "00445790"},
        {"gb-crn", "SC045551"},
        {"gb-crn", "OC300001"},
        {"gb-vat", "GB 980 7806 84"},
        {"gr-afm", "094259216"},
        {"hu-adoszam", "12892312-2-41"},
        {"id-npwp", "01.312.166.0-091.000"},
        {"ie-cro", "12345"},
        {"ie-vat", "IE 6433435F"},
        {"ie-vat", "IE 6433435OA"},
        {"il-hp", "516179157"},
        {"in-cin", "U74999MH2018PTC312456"},
        {"in-cin", "L17110MH1973PLC019786"},
        {"in-gstin", "27AAPFU0939F1ZV"},
        {"in-pan", "AAACR5055K"},
        {"it-piva", "00743110157"},
        {"jp-cn", "5-8356-7825-6246"},
        {"ke-pin", "P051365947M"},
        {"kr-brn", "116-82-00276"},
        {"kr-brn", "134-86-72683"},
        {"mx-rfc-moral", "MAB 930714 8T4"},
        {"ng-rc", "RC 1234567"},
        {"nl-btw", "NL004495445B01"},
        {"nl-btw", "NL002455799B11"},
        {"nl-kvk", "69599084"},
        {"nl-rsin", "004495445"},
        {"no-orgnr", "988 077 917"},
        {"nz-nzbn", "9429041535011"},
        {"pe-ruc", "20512333797"},
        {"pl-krs", "0000028860"},
        {"pl-nip", "856-734-62-15"},
        {"pl-regon", "192598184"},
        {"pl-regon", "123456785"},
        {"pt-nipc", "501964843"},
        {"ro-cui", "18547290"},
        {"ro-onrc", "J52/750/2012"},
        {"ro-onrc", "J2012000750528"},
        {"ru-inn", "7707083893"},
        {"ru-ogrn", "1022200525819"},
        {"se-orgnr", "556036-0793"},
        {"se-orgnr", "123456-7897"},
        {"sg-uen", "197401143C"},
        {"sg-uen", "00192200M"},
        {"th-juristic", "0-10-5-556-00001-7"},
        {"tr-vkn", "4540536920"},
        {"tw-ubn", "00501503"},
        {"ua-edrpou", "32855961"},
        {"us-ein", "91-1144442"},
        {"us-ein", "04-2103594"},
        {"vn-mst", "0100233488"},
        {"za-cipc", "2000/000001/07"},
        {"lei", "213800KUD8LAJWSQ9D15"},
    };
    // the schemes without a check: structure only
    const std::set<std::string> unchecked{"at-fn", "de-hrn", "eg-tn", "gb-crn", "ie-cro", "in-cin", "in-pan",
                                          "ke-pin", "ng-rc", "nl-kvk", "pl-krs", "us-ein", "za-cipc"};
    // more than one check character passes (GB VAT's two rules, the UBN's seventh digit 7, the CIF's
    // digit or letter), or the changed digit is not the check (an old-format ONRC number)
    const std::set<std::string> loose{"gb-vat", "tw-ubn", "es-nif-entity", "ro-onrc"};
    for (const auto& [scheme, value] : examples)
    {
        INFO(scheme << " " << value);
        REQUIRE(gen.is_valid(scheme, value));
        if (unchecked.contains(scheme) || loose.contains(scheme)) continue;
        // change the check: the last digit; the first of a JP corporate number, the eighth of an HU tax number, the ninth of an ID NPWP or NL btw-id
        const std::map<std::string, std::size_t> check_digit{{"jp-cn", 0}, {"hu-adoszam", 7}, {"id-npwp", 8}, {"nl-btw", 8}};
        std::string changed = value;
        std::size_t at = changed.find_last_of("0123456789");
        if (const auto it = check_digit.find(scheme); it != check_digit.end())
        {
            std::size_t digits = 0;
            for (at = 0; at < changed.size(); ++at)
            {
                if (changed[at] >= '0' && changed[at] <= '9' && digits++ == it->second) break;
            }
        }
        changed[at] = changed[at] == '9' ? '0' : static_cast<char>(changed[at] + 1);
        REQUIRE_FALSE(gen.is_valid(scheme, changed));
    }
    REQUIRE_THROWS_AS((void)gen.is_valid("xx-none", "1"), std::invalid_argument);
}

TEST_CASE("names, domains and numbers are coherent in every country", "[coherence][names]")
{
    const auto gen = make_loaded_generator();
    for (const auto& country : gen.countries())
    {
        std::set<std::string> names;
        for (std::uint64_t s = 0; s < 200; ++s)
        {
            const auto c = gen.get_company(in(country), s);
            INFO(country << " " << s << " " << c.to_json());
            REQUIRE(!c.name.empty());
            REQUIRE(!c.short_name.empty());
            REQUIRE(c.name.find(c.short_name) != std::string::npos);
            REQUIRE(dasmig::core::is_utf8(c.name));
            REQUIRE(c.name.find("  ") == std::string::npos);
            REQUIRE(c.name.front() != ' ');
            REQUIRE(c.name.back() != ' ');
            names.insert(c.short_name);

            const auto dot = c.domain.find('.');
            REQUIRE(dot != std::string::npos);
            REQUIRE(dot > 0);
            REQUIRE(c.domain.size() - dot > 2);
            REQUIRE(std::ranges::all_of(c.domain, [](char ch)
                                        { return (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '.'; }));
            REQUIRE_FALSE(c.domain.ends_with(".example"));

            std::set<std::string> seen;
            for (const auto& n : c.numbers)
            {
                INFO(n.scheme << " " << n.value);
                REQUIRE(gen.is_valid(n.scheme, n.value));
                REQUIRE_FALSE(n.reserved);
                REQUIRE(!n.label.empty());
                REQUIRE(!n.compact.empty());
                REQUIRE(seen.insert(n.scheme).second);
                if (n.scheme == "lei")
                {
                    // decision 9: 250 persons or more, or finance; never a sole trader
                    REQUIRE((c.employees >= 250 || c.isic_section == 'K'));
                    REQUIRE(c.legal_form_group != "sole_proprietorship");
                    REQUIRE(&n == &c.numbers.back());
                }
            }
        }
        REQUIRE(names.size() > 20); // names vary
    }
}

TEST_CASE("numbers encode the company: SIREN in SIRET and TVA, ACN in ABN, PAN in GSTIN, RFC's date", "[numbers]")
{
    const auto gen = make_loaded_generator();
    const auto value = [](const company& c, std::string_view scheme)
    {
        const auto it = std::ranges::find_if(c.numbers, [&](const dasmig::registration& n) { return n.scheme == scheme; });
        return it == c.numbers.end() ? std::string{} : it->compact;
    };
    for (std::uint64_t s = 0; s < 300; ++s)
    {
        auto request = in("FR");
        const auto fr = gen.get_company(request, s);
        REQUIRE(value(fr, "fr-siret").substr(0, 9) == value(fr, "fr-siren"));
        REQUIRE(value(fr, "fr-tva").substr(4) == value(fr, "fr-siren"));

        request = in("AU");
        request.incorporated_only = true;
        const auto au = gen.get_company(request, s);
        if (!value(au, "au-acn").empty()) REQUIRE(value(au, "au-abn").substr(2) == value(au, "au-acn"));

        request = in("IN");
        request.size = size_band::large;
        const auto india = gen.get_company(request, s);
        if (!value(india, "in-gstin").empty()) REQUIRE(value(india, "in-gstin").substr(2, 10) == value(india, "in-pan"));

        request = in("MX");
        request.incorporated_only = true;
        const auto mx = gen.get_company(request, s);
        const auto rfc = value(mx, "mx-rfc-moral");
        if (!rfc.empty())
        {
            const auto date = mx.founded_text();
            REQUIRE(rfc.substr(3, 6) == date.substr(2, 2) + date.substr(5, 2) + date.substr(8, 2));
        }

        request = in("SE");
        request.incorporated_only = true;
        const auto se = gen.get_company(request, s);
        if (se.legal_form == "AB") REQUIRE(value(se, "se-orgnr").front() == '5');
    }
}

TEST_CASE("safe mode: domains end in .example and numbers nobody holds", "[safe]")
{
    const auto gen = make_loaded_generator();
    // the schemes whose safe number still passes a check: a reserved prefix, or a block above the highest
    const std::set<std::string> no_check{"gb-crn", "ie-cro", "ng-rc", "pl-krs", "za-cipc", "lei"};
    for (const auto* country : {"US", "DE", "FR", "GB", "JP", "CN", "IN", "BR", "RU", "NL", "AT", "IT", "ZA", "PL", "EG", "KE"})
    {
        for (std::uint64_t s = 0; s < 200; ++s)
        {
            auto request = in(country);
            request.safe = true;
            if (s % 2 == 0) request.size = size_band::large;
            const auto c = gen.get_company(request, s);
            INFO(c.to_json());
            REQUIRE(c.domain.ends_with(".example"));
            for (const auto& n : c.numbers)
            {
                REQUIRE(n.reserved);
                // decision 10: the schemes without a safe method are left out
                REQUIRE(n.scheme != "de-hrn");
                REQUIRE(n.scheme != "at-fn");
                REQUIRE(n.scheme != "in-cin");
                REQUIRE(n.scheme != "in-pan");
                REQUIRE(n.scheme != "nl-kvk");
                REQUIRE(n.scheme != "eg-tn");
                REQUIRE(n.scheme != "ke-pin");
                if (n.scheme == "lei") REQUIRE(n.value.starts_with("ZZZZ"));
                if (!no_check.contains(n.scheme)) REQUIRE_FALSE(gen.is_valid(n.scheme, n.value));
            }
            // safe mode changes the numbers and the domain only
            request.safe = false;
            const auto real = gen.get_company(request, s);
            REQUIRE(real.name == c.name);
            REQUIRE(real.founded == c.founded);
            REQUIRE(real.legal_form_code == c.legal_form_code);
        }
    }
}

TEST_CASE("a given founder surname or place is used where it fits the name's script", "[request][names]")
{
    const auto gen = make_loaded_generator();
    std::size_t with_surname = 0;
    std::size_t with_place = 0;
    for (std::uint64_t s = 0; s < 400; ++s)
    {
        auto request = in("DE");
        const auto base = gen.get_company(request, s);
        request.founder_surname = "Quastenberg";
        request.place = "Zwergenhausen";
        const auto c = gen.get_company(request, s);
        // the other fields keep their draws
        REQUIRE(c.isic == base.isic);
        REQUIRE(c.employees == base.employees);
        REQUIRE(c.legal_form_code == base.legal_form_code);
        REQUIRE(c.founded == base.founded);
        if (c.name.find("Quastenberg") != std::string::npos) ++with_surname;
        if (c.name.find("Zwergenhausen") != std::string::npos) ++with_place;
    }
    REQUIRE(with_surname > 40);
    REQUIRE(with_place > 10);

    // a Latin surname is not put in a Japanese-script name
    for (std::uint64_t s = 0; s < 200; ++s)
    {
        auto request = in("JP");
        request.founder_surname = "Quastenberg";
        const auto c = gen.get_company(request, s);
        if (c.name.find("株式会社") != std::string::npos) REQUIRE(c.name.find("Quastenberg") == std::string::npos);
    }
}

TEST_CASE("a sole trader's name names no partner", "[names]")
{
    const auto gen = make_loaded_generator();
    // words.tsv's family words under key "company"
    const std::vector<std::string> partner{"& Co", "& Cie", "& C.", "y Cía.", "y Asociados", "i Spółka", "i Wspólnicy",
                                           "a spol.", "és Társa", "& Cia", "и Партнёры", "Brothers", "Hermanos", "Frères",
                                           "Fratelli", "Irmãos", "Gebrüder"};
    std::size_t sole = 0;
    std::size_t partner_names = 0;
    for (const auto* country : {"DE", "FR", "GB", "ES", "IT", "PL", "CZ", "HU", "PT", "NL", "SE", "DK", "RU"})
    {
        for (std::uint64_t s = 0; s < 3000; ++s)
        {
            auto request = in(country);
            request.size = size_band::small;
            const auto c = gen.get_company(request, s);
            const bool has = std::ranges::any_of(partner, [&](const std::string& w)
                                                 { return c.short_name.find(w) != std::string::npos; });
            if (c.legal_form_group == "sole_proprietorship")
            {
                ++sole;
                INFO(c.name);
                REQUIRE_FALSE(has);
            }
            else if (has) ++partner_names;
        }
    }
    REQUIRE(sole > 500);
    REQUIRE(partner_names > 0); // other companies still draw them
}

// ---------------------------------------------------------------------------
// Conversion and JSON
// ---------------------------------------------------------------------------

TEST_CASE("string conversion and streaming give the summary", "[conversion]")
{
    const auto gen = make_loaded_generator();
    const auto v = gen.get_company(company_request{}, std::uint64_t{5});
    const std::string s = v;
    std::ostringstream os;
    os << v;
    REQUIRE(s == v.to_string());
    REQUIRE(os.str() == s);
    REQUIRE(s.starts_with(v.country_code + ", "));
    REQUIRE(s.find("founded " + v.founded_text()) != std::string::npos);
    REQUIRE(s.find("(" + v.isic + ")") != std::string::npos);
}

TEST_CASE("to_json and signature", "[json]")
{
    const auto gen = make_loaded_generator();
    const auto v = gen.get_company(company_request{}, std::uint64_t{99});
    REQUIRE(v.signature().seed == 99);
    REQUIRE(v.seed() == 99);
    REQUIRE(v.signature().data == gen.data_hash());
    const auto json = v.to_json();
    REQUIRE(json.starts_with(R"({"country_code":")"));
    REQUIRE(json.find(R"("signature":{"seed":"0000000000000063")") != std::string::npos);
    // Built outside REQUIRE: MSVC's preprocessor misreads a raw string next to "\"" in a macro argument (C2017).
    const auto founded = R"("founded":")" + v.founded_text() + R"(")";
    REQUIRE(json.find(founded) != std::string::npos);

    dasmig::core::json_writer w;
    w.begin_array();
    v.to_json(w);
    w.end_array();
    REQUIRE(w.str() == "[" + json + "]");
}

// ---------------------------------------------------------------------------
// Known answers (rule 2): the same seed and data give these exact results on
// every compiler and platform. A change here is a change of output and needs
// a major version.
// ---------------------------------------------------------------------------

TEST_CASE("known answers for fixed seeds", "[kat]")
{
    cpg gen;
    gen.load(std::filesystem::path{"resources/lite"});
    if (!gen.has_data()) gen.load(std::filesystem::path{"../resources/lite"});
    REQUIRE(gen.has_data());
    REQUIRE(gen.data_hash() == 0x5fa2cadd5c7a71afULL);

    REQUIRE(gen.get_company(company_request{}, std::uint64_t{1}).to_json() ==
            R"({"country_code":"CN","name":"新通客运有限公司","short_name":"新通客运","isic":"49","isic_label":"land transport and transport via pipelines","isic_section":"H","size":"micro","employees":1,"legal_form":"有限公司","legal_form_code":"","legal_form_group":"limited_liability","founded":"2021-10-17","numbers":[{"scheme":"cn-uscc","label":"USCC","value":"91360981MA7WW3546H","compact":"91360981MA7WW3546H","reserved":false}],"domain":"xintongkeyun.com","evidence":"filled","signature":{"seed":"0000000000000001","data":"5fa2cadd5c7a71af"}})");
    REQUIRE(gen.get_company(in("DE"), std::uint64_t{2}).to_json() ==
            R"({"country_code":"DE","name":"Finanzberatung München GmbH","short_name":"Finanzberatung München","isic":"66","isic_label":"activities auxiliary to financial service and insurance activities","isic_section":"K","size":"micro","employees":5,"legal_form":"GmbH","legal_form_code":"2HBR","legal_form_group":"limited_liability","founded":"2000-06-05","numbers":[{"scheme":"de-hrn","label":"HR","value":"Amtsgericht Oldenburg (Oldenburg) HRB 207288","compact":"Oldenburg HRB 207288","reserved":false},{"scheme":"de-ustid","label":"USt-IdNr.","value":"DE106065628","compact":"DE106065628","reserved":false},{"scheme":"lei","label":"LEI","value":"9845ZXDCJYIG1I65SF47","compact":"9845ZXDCJYIG1I65SF47","reserved":false}],"domain":"finanzberatungmuenchen.de","evidence":"measured","signature":{"seed":"0000000000000002","data":"5fa2cadd5c7a71af"}})");
    REQUIRE(gen.get_company(in("BR"), std::uint64_t{3}).to_json() ==
            R"({"country_code":"BR","name":"Genica Empreendimentos Imobiliários Ltda.","short_name":"Genica Empreendimentos Imobiliários","isic":"68","isic_label":"real estate activities","isic_section":"L","size":"micro","employees":1,"legal_form":"Ltda.","legal_form_code":"QHW1","legal_form_group":"limited_liability","founded":"2005-09-29","numbers":[{"scheme":"br-cnpj","label":"CNPJ","value":"02.413.885/0001-86","compact":"02413885000186","reserved":false}],"domain":"genica-empreendimentos.com.br","evidence":"filled","signature":{"seed":"0000000000000003","data":"5fa2cadd5c7a71af"}})");

    std::uint64_t h = 0xcbf29ce484222325ULL;
    for (std::uint64_t s = 0; s < 10000; ++s) h = fnv(h, gen.get_company(company_request{}, s).to_json());
    REQUIRE(h == 0xea67689b5f3914bbULL);
}

// ---------------------------------------------------------------------------
// Extending data (rule 8)
// ---------------------------------------------------------------------------

TEST_CASE("extend overrides, adds and removes legal forms", "[extend]")
{
    auto gen = make_loaded_generator();
    const auto base_hash = gen.data_hash();
    const auto before = gen.get_company(in("DE"), std::uint64_t{7});

    // A form of the user's own that every large German company takes.
    const auto& info = gen.extend_from_string(
        company_table::legal_forms,
        "country_code\tkey\tcode\tgroup\tabbreviation\tlatin_abbreviation\tname\tlatin_name\tfrom\tuntil\tonly\tnot\t"
        "share_small\tshare_large\tevidence\n"
        "DE\ttest\tTEST\tlimited_liability\tTst\tTst\tTest AG\tTest AG\t\t\t\t\t1000000\t1000000\tcurated\n",
        "mod.tsv");
    REQUIRE(info.added == 1);
    REQUIRE(gen.data_hash() != base_hash);
    REQUIRE(gen.layers(company_table::legal_forms).size() == 2);
    REQUIRE(gen.layers(company_table::sizes).size() == 1);
    auto r = in("DE");
    r.legal_form = "tst";
    const auto forced = gen.get_company(r, std::uint64_t{7});
    REQUIRE(forced.legal_form == "Tst");
    REQUIRE(forced.legal_form_code == "TEST");
    REQUIRE(forced.signature().data == gen.data_hash());
    const auto forms = gen.legal_forms("DE");
    REQUIRE(std::ranges::any_of(forms, [](const dasmig::legal_form_info& f) { return f.code == "TEST"; }));

    // Removing it again.
    gen.extend_from_string(company_table::legal_forms, "country_code\tkey\tdisabled\nDE\ttest\t1\n", "remove.tsv");
    REQUIRE_THROWS_AS(gen.get_company(r, std::uint64_t{7}), std::invalid_argument);

    // Reloading the base drops every layer.
    REQUIRE(gen.load(dasmig::dataset::lite));
    REQUIRE(gen.data_hash() == base_hash);
    REQUIRE(gen.get_company(in("DE"), std::uint64_t{7}).to_json() == before.to_json());
}

TEST_CASE("a layer can change how often a country is drawn", "[extend]")
{
    auto gen = make_loaded_generator();
    const auto row = [](const std::string& country, const std::string& persons)
    {
        return "country_code\tenterprises\tpersons_employed\tlanguages\tculture\tindustry_evidence\temployment_evidence\t"
               "size_evidence\tform_evidence\tage_evidence\tyoung_share\tdomain_evidence\n" +
               country + "\t1000000000000\t" + persons + "\ten:1\tenglish\tmeasured\tmeasured\tmeasured\tmeasured\t"
               "measured\t\tmeasured\n";
    };
    gen.extend_from_string(company_table::countries, row("MC", "100"), "monaco.tsv");
    int monaco = 0;
    for (std::uint64_t s = 0; s < 400; ++s) monaco += gen.get_company(company_request{}, s).country_code == "MC" ? 1 : 0;
    REQUIRE(monaco > 300); // its billion enterprises outweigh the rest of the world
}

TEST_CASE("a bad layer is rejected and leaves the data unchanged", "[extend][errors]")
{
    auto gen = make_loaded_generator();
    const auto hash = gen.data_hash();
    const std::string header =
        "country_code\tkey\tcode\tgroup\tabbreviation\tlatin_abbreviation\tname\tlatin_name\tfrom\tuntil\tonly\tnot\t"
        "share_small\tshare_large\tevidence\n";

    REQUIRE_THROWS_AS(gen.extend_from_string(company_table::legal_forms,
                                             header + "DE\tx\tX\tcompany\tX\tX\tX\tX\t\t\t\t\t1\t1\tcurated\n", "group.tsv"),
                      dasmig::core::tsv_error);
    REQUIRE_THROWS_AS(gen.extend_from_string(company_table::legal_forms,
                                             header + "DE\tx\tX\tother\tX\tX\tX\tX\t2008-13-01\t\t\t\t1\t1\tcurated\n",
                                             "date.tsv"),
                      dasmig::core::tsv_error);
    REQUIRE_THROWS_AS(gen.extend_from_string(company_table::legal_forms,
                                             header + "DE\tx\tX\tother\tX\tX\tX\tX\t\t\tZ\t\t1\t1\tcurated\n", "only.tsv"),
                      dasmig::core::tsv_error);
    REQUIRE_THROWS_AS(gen.extend_from_string(company_table::legal_forms,
                                             header + "DE\tx\tX\tother\tX\tX\tX\tX\t\t\t\t\t-1\t1\tcurated\n", "share.tsv"),
                      dasmig::core::tsv_error);
    REQUIRE_THROWS_AS(gen.extend_from_string(company_table::legal_forms, "country_code\tkey\nDE\tx\n", "partial.tsv"),
                      dasmig::core::tsv_error); // required columns missing
    // Rows that are valid alone but do not fit the other tables.
    REQUIRE_THROWS_AS(gen.extend_from_string(company_table::legal_forms,
                                             header + "ZZ\tx\tX\tother\tX\tX\tX\tX\t\t\t\t\t1\t1\tcurated\n", "country.tsv"),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(gen.extend_from_string(company_table::industries, "country_code\tdivision\tenterprises\temployment\nDE\t44\t1\t1\n",
                                             "division.tsv"),
                      dasmig::core::tsv_error);

    REQUIRE(gen.data_hash() == hash);
    REQUIRE(gen.layers(company_table::legal_forms).size() == 1);
    REQUIRE_THROWS_AS(gen.extend(std::filesystem::path{"somewhere/widgets.tsv"}), std::invalid_argument);
}

#ifdef DASMIG_COMPANY_EMBED
TEST_CASE("compiled-in data needs no files and matches them", "[embed]")
{
    const auto old_cwd = std::filesystem::current_path();
    const auto tmp = std::filesystem::temp_directory_path() / "cpg_embed_dir";
    std::filesystem::create_directories(tmp);
    std::filesystem::current_path(tmp);
    cpg gen;
    const bool loaded = gen.load(dasmig::dataset::lite);
    std::filesystem::current_path(old_cwd);
    std::filesystem::remove_all(tmp);

    REQUIRE(loaded);
    REQUIRE(gen.layers(company_table::countries).front().source == "embedded:lite/countries.tsv");
    REQUIRE(gen.data_hash() == 0x5fa2cadd5c7a71afULL); // same as the files
}
#endif

// ---------------------------------------------------------------------------
// Batch generation
// ---------------------------------------------------------------------------

TEST_CASE("company batch: items equal the single calls for any thread count", "[batch]")
{
    auto gen = make_loaded_generator();
    const auto request = company_request{};
    const auto batch = gen.get_company_batch(request, {.count = 200, .threads = 1}, 77);
    REQUIRE(batch.size() == 200);
    for (std::uint64_t i = 0; i < batch.size(); ++i)
    {
        REQUIRE(batch[i].to_json() == gen.get_company(request, dasmig::core::item_seed(77, i)).to_json());
    }

    // A range is computed directly, on any number of threads.
    const auto tail = gen.get_company_batch(request, {.count = 100, .first = 100, .threads = 4}, 77);
    REQUIRE(tail.size() == 100);
    for (std::size_t i = 0; i < tail.size(); ++i)
    {
        REQUIRE(tail[i].to_json() == batch[100 + i].to_json());
    }
    for (const std::size_t threads : {2U, 7U, 0U})
    {
        const auto other = gen.get_company_batch(request, {.count = 200, .threads = threads}, 77);
        REQUIRE(other.size() == batch.size());
        for (std::size_t i = 0; i < other.size(); ++i)
        {
            REQUIRE(other[i].to_json() == batch[i].to_json());
        }
    }

    // Without a seed the batch still has the requested size.
    REQUIRE(gen.get_company_batch(request, {.count = 5}).size() == 5);
}

TEST_CASE("company batch: streaming gives the same items in order", "[batch]")
{
    auto gen = make_loaded_generator();
    const auto request = company_request{};
    const auto batch = gen.get_company_batch(request, {.count = 300, .first = 10, .threads = 3}, 5);
    std::uint64_t next = 10;
    std::size_t seen = 0;
    gen.for_each_company(request, {.count = 300, .first = 10, .threads = 3}, 5,
                         [&](std::uint64_t index, const company& item)
                         {
                             REQUIRE(index == next);
                             REQUIRE(item.to_json() == batch[seen].to_json());
                             ++next;
                             ++seen;
                         });
    REQUIRE(seen == batch.size());
}

TEST_CASE("company batch: a bad request throws before any item", "[batch][errors]")
{
    auto gen = make_loaded_generator();
    REQUIRE_THROWS_AS(gen.get_company_batch(in("ZZ"), {.count = 10, .threads = 2}, 1), std::invalid_argument);
}

TEST_CASE("company batch: known answer of 10,000 items", "[batch][kat]")
{
    auto gen = make_loaded_generator();
    const auto request = company_request{};
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    gen.for_each_company(request, {.count = 10000, .threads = 4}, 1,
                         [&](std::uint64_t, const company& item) { hash = fnv(hash, item.to_json()); });
    REQUIRE(hash == 0x56a60cb5fe97a869ULL);
}
