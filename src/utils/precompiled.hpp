/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_PRECOMPILED_HPP_INCLUDED__
#define __ZLINK_PRECOMPILED_HPP_INCLUDED__

//  On AIX platform, poll.h has to be included first to get consistent
//  definition of pollfd structure (AIX uses 'reqevents' and 'retnevents'
//  instead of 'events' and 'revents' and defines macros to map from POSIX-y
//  names to AIX-specific names).
//  zlink.h must be included *after* poll.h for AIX to build properly.
//  precompiled.hpp includes include/zlink.h
#if defined ZLINK_POLL_BASED_ON_POLL && defined ZLINK_HAVE_AIX
#include <poll.h>
#endif

#include "platform.hpp"

#define __STDC_LIMIT_MACROS

// This must be included before any windows headers are compiled.
#if defined ZLINK_HAVE_WINDOWS
#include "utils/windows.hpp"
#endif

#if defined ZLINK_HAVE_OPENBSD
#define ucred sockpeercred
#endif

// 0MQ definitions and exported functions
#include "../include/zlink.h"

// TODO: expand pch implementation to non-windows builds.
#ifdef _MSC_VER

// standard C headers
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <io.h>
#include <ipexport.h>
#include <iphlpapi.h>
#include <limits.h>
#include <mstcpip.h>
#include <mswsock.h>
#include <process.h>
#include <rpc.h>
#include <signal.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <winsock2.h>
#include <ws2tcpip.h>

// standard C++ headers
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <limits>
#include <map>
#include <new>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#if _MSC_VER >= 1800
#include <inttypes.h>
#endif

#if _MSC_VER >= 1700
#include <atomic>
#endif

#if defined _WIN32_WCE
#include <cmnintrin.h>
#else
#include <intrin.h>
#endif

#include "core/options.hpp"

#endif // _MSC_VER

#endif //ifndef __ZLINK_PRECOMPILED_HPP_INCLUDED__
