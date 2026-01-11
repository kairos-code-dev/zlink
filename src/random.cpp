/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"
#include <stdlib.h>

#if !defined ZMQ_HAVE_WINDOWS
#include <unistd.h>
#endif

#include "random.hpp"
#include "stdint.hpp"
#include "clock.hpp"
#include "mutex.hpp"
#include "macros.hpp"

void zmq::seed_random ()
{
#if defined ZMQ_HAVE_WINDOWS
    const int pid = static_cast<int> (GetCurrentProcessId ());
#else
    int pid = static_cast<int> (getpid ());
#endif
    srand (static_cast<unsigned int> (clock_t::now_us () + pid));
}

uint32_t zmq::generate_random ()
{
    const uint32_t low = static_cast<uint32_t> (rand ());
    uint32_t high = static_cast<uint32_t> (rand ());
    high <<= (sizeof (int) * 8 - 1);
    return high | low;
}

void zmq::random_open ()
{
}

void zmq::random_close ()
{
}