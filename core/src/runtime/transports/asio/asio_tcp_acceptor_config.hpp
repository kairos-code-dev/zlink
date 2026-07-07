/* SPDX-License-Identifier: MPL-2.0 */

#ifndef ZLINK_ASIO_TCP_ACCEPTOR_CONFIG_HPP_INCLUDED
#define ZLINK_ASIO_TCP_ACCEPTOR_CONFIG_HPP_INCLUDED

#include "core/options.hpp"
#include "utils/config.hpp"

#include <boost/asio/detail/socket_option.hpp>
#include <boost/asio/ip/tcp.hpp>

#include <cerrno>

#ifndef ZLINK_HAVE_WINDOWS
#include <sys/socket.h>
#else
#include <winsock2.h>
#endif

namespace zlink
{
template <typename acceptor_t, typename trace_fn_t>
inline int configure_asio_tcp_acceptor (acceptor_t &acceptor_,
                                        const boost::asio::ip::tcp &protocol_,
                                        int address_family_,
                                        const boost::asio::ip::tcp::endpoint &bind_endpoint_,
                                        const options_t &options_,
                                        bool enable_reuse_port_,
                                        trace_fn_t trace_fn_)
{
    boost::system::error_code ec;

    acceptor_.open (protocol_, ec);
    if (ec) {
        trace_fn_ ("open acceptor", ec, true);
        errno = EADDRINUSE;
        return -1;
    }

#ifdef ZLINK_HAVE_WINDOWS
    acceptor_.set_option (
      boost::asio::detail::socket_option::boolean<SOL_SOCKET, SO_EXCLUSIVEADDRUSE> (true), ec);
#else
    acceptor_.set_option (boost::asio::socket_base::reuse_address (true), ec);
#endif
    if (ec) {
        trace_fn_ ("set reuse_address", ec, true);
        acceptor_.close ();
        errno = EADDRINUSE;
        return -1;
    }

#ifdef SO_REUSEPORT
    if (enable_reuse_port_) {
        typedef boost::asio::detail::socket_option::boolean<SOL_SOCKET, SO_REUSEPORT>
          reuse_port_t;
        acceptor_.set_option (reuse_port_t (true), ec);
        if (ec) {
            trace_fn_ ("set reuse_port", ec, false);
            ec.clear ();
        }
    }
#else
    (void) enable_reuse_port_;
#endif

    if (address_family_ == AF_INET6) {
        acceptor_.set_option (boost::asio::ip::v6_only (!options_.ipv6), ec);
        if (ec) {
            trace_fn_ ("set v6only", ec, false);
            ec.clear ();
        }
    }

    acceptor_.bind (bind_endpoint_, ec);
    if (ec) {
        trace_fn_ ("bind", ec, true);
        acceptor_.close ();
        errno = EADDRINUSE;
        return -1;
    }

    acceptor_.listen (options_.backlog, ec);
    if (ec) {
        trace_fn_ ("listen", ec, true);
        acceptor_.close ();
        errno = EADDRINUSE;
        return -1;
    }

    return 0;
}
}

#endif
