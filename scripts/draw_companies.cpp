// SPDX-License-Identifier: MIT
// Draws companies for scripts/validate_company.py and writes counts, not companies.
//
//   draw_companies <resources dir> <per country> <seed> <enterprises|employment> <out dir> [country...]
//
// Writes three TSV files to <out dir>:
//   cells.tsv  country, division, band, group, companies, employees
//   forms.tsv  country, legal form (abbreviation, else name), group, companies
//   ages.tsv   country, companies aged 0 to 5 years, companies
#include "dasmig/companygen.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <tuple>

int main(int argc, char** argv)
{
    if (argc < 6)
    {
        std::cerr << "usage: draw_companies <resources dir> <per country> <seed> <enterprises|employment> <out dir> [country...]\n";
        return 2;
    }
    try
    {
        dasmig::cpg g;
        g.load(std::filesystem::path{argv[1]});
        if (!g.has_data())
        {
            std::cerr << "no data in " << argv[1] << "\n";
            return 2;
        }
        const std::uint64_t per_country = std::strtoull(argv[2], nullptr, 10);
        const std::uint64_t seed = std::strtoull(argv[3], nullptr, 10);
        dasmig::company_request request;
        request.weight = std::string{argv[4]} == "employment" ? dasmig::company_weight::employment
                                                                : dasmig::company_weight::enterprises;
        const std::filesystem::path out{argv[5]};
        std::filesystem::create_directories(out);
        std::vector<std::string> countries;
        for (int i = 6; i < argc; ++i) countries.emplace_back(argv[i]);
        if (countries.empty()) countries = g.countries();

        std::ofstream cells_file{out / "cells.tsv"};
        std::ofstream forms_file{out / "forms.tsv"};
        std::ofstream ages_file{out / "ages.tsv"};
        if (!cells_file || !forms_file || !ages_file)
        {
            std::cerr << "cannot write to " << out << "\n";
            return 2;
        }
        constexpr char tab = '\t';
        constexpr char eol = '\n';
        const int reference_year = static_cast<int>(request.reference_date.year());
        for (const auto& country : countries)
        {
            request.country = country;
            std::map<std::tuple<std::string, std::string, std::string>, std::pair<std::uint64_t, std::uint64_t>> cells;
            std::map<std::pair<std::string, std::string>, std::uint64_t> forms;
            std::uint64_t young = 0;
            for (std::uint64_t i = 0; i < per_country; ++i)
            {
                const auto c = g.get_company(request, dasmig::core::item_seed(seed, i));
                auto& cell = cells[{c.isic, std::string{dasmig::to_string(c.size)}, c.legal_form_group}];
                ++cell.first;
                cell.second += c.employees;
                ++forms[{c.legal_form, c.legal_form_group}];
                if (reference_year - static_cast<int>(c.founded.year()) <= 5) ++young;
            }
            for (const auto& [key, v] : cells)
            {
                cells_file << country << tab << std::get<0>(key) << tab << std::get<1>(key) << tab << std::get<2>(key)
                           << tab << v.first << tab << v.second << eol;
            }
            for (const auto& [key, n] : forms)
            {
                forms_file << country << tab << key.first << tab << key.second << tab << n << eol;
            }
            ages_file << country << tab << young << tab << per_country << eol;
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
