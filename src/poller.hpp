/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZMQ_POLLER_HPP_INCLUDED__
#define __ZMQ_POLLER_HPP_INCLUDED__

//  Phase 5: Legacy I/O removal - Only ASIO poller is supported
#if !defined ZMQ_IOTHREAD_POLLER_USE_ASIO
#error ZMQ_IOTHREAD_POLLER_USE_ASIO must be defined - only ASIO poller is supported
#endif

#include "asio/asio_poller.hpp"

#if (defined ZMQ_POLL_BASED_ON_SELECT + defined ZMQ_POLL_BASED_ON_POLL) > 1
#error More than one of the ZMQ_POLL_BASED_ON_* macros defined
#elif (defined ZMQ_POLL_BASED_ON_SELECT + defined ZMQ_POLL_BASED_ON_POLL) == 0
#error None of the ZMQ_POLL_BASED_ON_* macros defined
#endif

#endif
