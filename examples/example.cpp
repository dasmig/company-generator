#include "../dasmig/companygen.hpp"
#include <cstdint>
#include <iostream>

int main()
{
    // The singleton loads the lite tier (compiled in, or from resources/).
    auto& gen = dasmig::cpg::instance();
    if (!gen.has_data())
    {
        std::cerr << "Could not load the company data.\n";
        return 1;
    }
    std::cout << "Serving " << gen.countries().size() << " countries.\n\n";

    std::cout << "Random, by the number of enterprises:\n";
    for (int i = 0; i < 5; ++i)
    {
        std::cout << "  " << gen.get_company(dasmig::company_request{}) << '\n';
    }

    // A worker's employer: each company counts for its staff.
    dasmig::company_request employer;
    employer.country = "DE";
    employer.weight = dasmig::company_weight::employment;
    std::cout << "\nAn employer in Germany (weighted by employment):\n";
    for (int i = 0; i < 3; ++i)
    {
        std::cout << "  " << gen.get_company(employer) << '\n';
    }

    // Fields can be forced; the rest is drawn to fit them.
    dasmig::company_request builder;
    builder.country = "FR";
    builder.isic = "F"; // construction
    builder.size = dasmig::size_band::small;
    builder.incorporated_only = true;
    std::cout << "\nA small incorporated construction company in France:\n";
    for (int i = 0; i < 3; ++i)
    {
        const auto company = gen.get_company(builder);
        std::cout << "  " << company << " [" << company.legal_form_group << ", evidence: " << company.evidence << "]\n";
    }

    std::cout << "\nSeed 42, twice:\n";
    const auto a = gen.get_company(dasmig::company_request{}, std::uint64_t{42});
    const auto b = gen.get_company(dasmig::company_request{}, std::uint64_t{42});
    std::cout << "  " << a.to_json() << '\n' << "  " << b.to_json() << '\n';

    std::cout << "\nGerman legal forms:\n";
    for (const auto& form : gen.legal_forms("DE"))
    {
        if (form.group == "limited_liability") std::cout << "  " << form.abbreviation << " (" << form.name << ")\n";
    }
    return 0;
}
