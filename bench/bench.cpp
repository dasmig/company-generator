// Benchmarks for company-generator; see bench.hpp for the options.

#include "../dasmig/companygen.hpp"
#include "bench.hpp"

int main(int argc, char** argv)
{
    dasmig::bench::runner run{"company-generator", argc, argv};

    run.load("load", [&] {
        dasmig::cpg gen;
        return gen.load(run.tier());
    });

    dasmig::cpg gen;
    if (!gen.load(run.tier())) return run.missing_tier();

    run.calls("get_company", [&](std::uint64_t seed) { return gen.get_company({}, seed); });
    run.calls("get_company to_json", [&](std::uint64_t seed) {
        return gen.get_company({}, seed).to_json();
    });

    run.batches("get_company_batch", [&](std::size_t count, std::size_t threads, std::uint64_t seed) {
        return gen.get_company_batch({}, {.count = count, .threads = threads}, seed);
    });

    return run.finish();
}
