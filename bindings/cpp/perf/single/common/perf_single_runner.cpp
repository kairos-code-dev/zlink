#include "perf_single_runner.hpp"

#include <cstdlib>
#include <iostream>

#ifndef RUN_PATTERN_FN
#error "RUN_PATTERN_FN must be defined"
#endif

void RUN_PATTERN_FN (const std::string &transport,
                     size_t size,
                     const std::string &lib_name);

namespace {

void run_dispatch (const std::string &transport,
                   size_t size,
                   const std::string &lib_name)
{
    RUN_PATTERN_FN (transport, size, lib_name);
}

} // namespace

namespace perf {
namespace single {

int run_standard_bench_main (int argc, char **argv, run_fn_t fn)
{
    if (!fn)
        return 1;

    std::string lib_name = "current";
    std::string transport;
    size_t size = 0;

    // policy runner format: <bin> <transport> <size>
    // compatibility format: <bin> <lib> <transport> <size>
    if (argc >= 4) {
        lib_name = argv[1];
        transport = argv[2];
        size = static_cast<size_t> (std::strtoull (argv[3], NULL, 10));
    } else if (argc >= 3) {
        transport = argv[1];
        size = static_cast<size_t> (std::strtoull (argv[2], NULL, 10));
    } else {
        std::cerr << "usage: <transport> <size>" << std::endl;
        return 1;
    }

    if (size == 0)
        return 1;

    fn (transport, size, lib_name);
    return 0;
}

} // namespace single
} // namespace perf

int main (int argc, char **argv)
{
    return perf::single::run_standard_bench_main (argc, argv, run_dispatch);
}
