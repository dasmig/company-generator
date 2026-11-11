// SPDX-License-Identifier: MIT
// Draws registration numbers for scripts/check_numbers.py: at least <per scheme> numbers of every
// scheme (the LEI pooled over the countries), with safe mode off and on.
//
//   draw_numbers <resources dir> <per scheme> <seed>
//
// Writes to stdout one number a line: scheme, value, compact, safe (0 or 1), reserved (0 or 1),
// country, legal-form code, founding date, name.
#include "dasmig/companygen.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>

int main(int argc, char** argv)
{
    if (argc < 4)
    {
        std::cerr << "usage: draw_numbers <resources dir> <per scheme> <seed>\n";
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
        const std::uint64_t target = std::strtoull(argv[2], nullptr, 10);
        const std::uint64_t seed = std::strtoull(argv[3], nullptr, 10);
        std::ios::sync_with_stdio(false);
        std::map<std::string, std::uint64_t> lei_seen;
        std::uint64_t lei_total[2] = {0, 0};
        std::uint64_t item = 0;
        for (const auto& country : g.countries())
        {
            for (int safe = 0; safe < 2; ++safe)
            {
                std::map<std::string, std::uint64_t> seen;
                // a first pass finds the country's schemes
                for (std::uint64_t i = 0; i < 2000; ++i)
                {
                    dasmig::company_request request;
                    request.country = country;
                    request.size = i % 2 == 0 ? dasmig::size_band::large : dasmig::size_band::small;
                    for (const auto& n : g.get_company(request, dasmig::core::item_seed(seed, item++)).numbers)
                    {
                        if (n.scheme != "lei") seen.emplace(n.scheme, 0);
                    }
                }
                if (seen.empty()) continue;
                const std::uint64_t cap = target * 60;
                for (std::uint64_t i = 0; i < cap; ++i)
                {
                    bool done = true;
                    for (const auto& [k, v] : seen) done = done && v >= target;
                    if (done) break;
                    dasmig::company_request request;
                    request.country = country;
                    request.safe = safe == 1;
                    if (i % 3 == 1) request.incorporated_only = true;
                    if (i % 3 == 2) request.size = dasmig::size_band::large;
                    const auto c = g.get_company(request, dasmig::core::item_seed(seed, item++));
                    for (const auto& n : c.numbers)
                    {
                        if (n.scheme == "lei")
                        {
                            if (lei_total[safe] >= target || lei_seen[country + std::to_string(safe)] >= target / 20 + 1) continue;
                            ++lei_total[safe];
                            ++lei_seen[country + std::to_string(safe)];
                        }
                        else
                        {
                            auto& count = seen[n.scheme];
                            if (count >= target) continue;
                            ++count;
                        }
                        std::cout << n.scheme << '\t' << n.value << '\t' << n.compact << '\t' << safe << '\t'
                                  << (n.reserved ? 1 : 0) << '\t' << country << '\t' << c.legal_form_code << '\t'
                                  << c.founded_text() << '\t' << c.name << '\n';
                    }
                }
            }
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
