/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"
#include <stdlib.h>

#if !defined ZLINK_HAVE_WINDOWS
#include <unistd.h>
#endif

#include "utils/random.hpp"
#include "utils/stdint.hpp"
#include "utils/clock.hpp"
#include "utils/mutex.hpp"
#include "utils/macros.hpp"

void zlink::seed_random ()
{
#if defined ZLINK_HAVE_WINDOWS
    const int pid = static_cast<int> (GetCurrentProcessId ());
#else
    int pid = static_cast<int> (getpid ());
#endif
    srand (static_cast<unsigned int> (clock_t::now_us () + pid));
}

uint32_t zlink::generate_random ()
{
    const uint32_t low = static_cast<uint32_t> (rand ());
    uint32_t high = static_cast<uint32_t> (rand ());
    high <<= (sizeof (int) * 8 - 1);
    return high | low;
}

void zlink::random_open ()
{
}

void zlink::random_close ()
{
}