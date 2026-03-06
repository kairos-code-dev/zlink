#include "perf_entry.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

#ifndef RUN_MULTI_SERVER_FN
#error "RUN_MULTI_SERVER_FN must be defined"
#endif

void RUN_MULTI_SERVER_FN (const std::string &transport, size_t size);

int main (int argc, char **argv)
{
    if (argc < 3) {
        std::cerr << "usage: <transport> <size>" << std::endl;
        return 1;
    }

    const std::string transport = argv[1];
    const size_t size = static_cast<size_t> (std::strtoull (argv[2], NULL, 10));
    if (size == 0)
        return 1;

    RUN_MULTI_SERVER_FN (transport, size);
    return 0;
}
