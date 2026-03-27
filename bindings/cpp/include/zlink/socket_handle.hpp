/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_SOCKET_HANDLE_HPP_INCLUDED
#define ZLINK_CPP_SOCKET_HANDLE_HPP_INCLUDED

#include "socket.hpp"

namespace zlink
{

class socket_handle_t
{
  public:
    socket_handle_t () noexcept {}

    explicit socket_handle_t (void *socket_, bool own_ = true) noexcept
        : _socket (own_ ? socket_t::adopt (socket_) : socket_t::wrap (socket_))
    {
    }

    ~socket_handle_t () {}

    socket_handle_t (socket_handle_t &&other) noexcept
        : _socket (std::move (other._socket))
    {
    }

    socket_handle_t &operator= (socket_handle_t &&other) noexcept
    {
        if (this == &other)
            return *this;
        _socket = std::move (other._socket);
        return *this;
    }

    socket_handle_t (const socket_handle_t &) = delete;
    socket_handle_t &operator= (const socket_handle_t &) = delete;

    bool valid () const noexcept { return _socket.valid (); }
    void *handle () noexcept { return _socket.handle (); }
    const void *handle () const noexcept { return _socket.handle (); }

    ZLINK_CPP_NODISCARD int close () noexcept { return _socket.close (); }

  protected:
    explicit socket_handle_t (socket_t &&socket_) noexcept
        : _socket (std::move (socket_))
    {
    }

    socket_t &socket_base () noexcept { return _socket; }
    const socket_t &socket_base () const noexcept { return _socket; }

  private:
    socket_t _socket;
};

} // namespace zlink

#endif
