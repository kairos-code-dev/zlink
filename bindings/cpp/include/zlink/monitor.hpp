/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_MONITOR_HPP_INCLUDED
#define ZLINK_CPP_MONITOR_HPP_INCLUDED

#include "socket.hpp"
#include "types.hpp"

namespace zlink
{

/**
 * @brief Wrapper around a monitor PAIR socket.
 */
class monitor_socket_t
{
  public:
    /**
     * @brief Construct from an owned monitor socket.
     * @param sock_ Monitor socket wrapper.
     */
    explicit monitor_socket_t (socket_t &&sock_) : _sock (std::move (sock_)) {}

    /**
     * @brief Receive one monitor event frame.
     * @param event_ Output event structure.
     * @param flags_ Receive flags.
     * @return 0 on success, -1 on failure.
     */
    int recv (zlink_monitor_event_t &event_, recv_flag flags_ = recv_flag::none)
    {
        return zlink_monitor_recv (_sock.handle (), &event_,
                                   static_cast<int> (flags_));
    }

    /**
     * @brief Access underlying socket wrapper.
     * @return Socket reference.
     */
    socket_t &socket () noexcept { return _sock; }

  private:
    socket_t _sock;
};

} // namespace zlink

#endif
