/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_MONITOR_HPP_INCLUDED
#define ZLINK_CPP_MONITOR_HPP_INCLUDED

#include "socket.hpp"
#include "types.hpp"

namespace zlink
{

class monitor_socket_t
{
  public:
    explicit monitor_socket_t (socket_t &&sock_) : _sock (std::move (sock_)) {}

    int recv (zlink_monitor_event_t &event_, recv_flag flags_ = recv_flag::none)
    {
        return zlink_monitor_recv (_sock.handle (), &event_,
                                   static_cast<int> (flags_));
    }

    socket_t &socket () noexcept { return _sock; }

  private:
    socket_t _sock;
};

} // namespace zlink

#endif
