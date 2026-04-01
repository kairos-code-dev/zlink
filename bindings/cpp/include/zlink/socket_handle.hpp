/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_SOCKET_HANDLE_HPP_INCLUDED
#define ZLINK_CPP_SOCKET_HANDLE_HPP_INCLUDED

#include "common.hpp"

namespace zlink
{

class socket_handle_t
{
  public:
    socket_handle_t () noexcept : _socket (NULL), _own (false) {}

    explicit socket_handle_t (void *socket_, bool own_ = true) noexcept
        : _socket (socket_), _own (own_)
    {
    }

    ~socket_handle_t () { (void) close (); }

    socket_handle_t (socket_handle_t &&other) noexcept
        : _socket (other._socket), _own (other._own)
    {
        other._socket = NULL;
        other._own = false;
    }

    socket_handle_t &operator= (socket_handle_t &&other) noexcept
    {
        if (this == &other)
            return *this;

        (void) close ();
        _socket = other._socket;
        _own = other._own;
        other._socket = NULL;
        other._own = false;
        return *this;
    }

    socket_handle_t (const socket_handle_t &) = delete;
    socket_handle_t &operator= (const socket_handle_t &) = delete;

    bool valid () const noexcept { return _socket != NULL; }
    void *handle () noexcept { return _socket; }
    const void *handle () const noexcept { return _socket; }

    ZLINK_CPP_NODISCARD int close () noexcept
    {
        if (!_socket) {
            _own = false;
            return 0;
        }

        if (!_own) {
            _socket = NULL;
            return 0;
        }

        void *socket = _socket;
        const int rc = zlink_close (socket);
        if (rc == 0) {
            _socket = NULL;
            _own = false;
        }
        return rc;
    }

  protected:
    void reset_handle (void *socket_, bool own_) noexcept
    {
        _socket = socket_;
        _own = own_;
    }

  private:
    void *_socket;
    bool _own;
};

} // namespace zlink

#endif
