/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_MESSAGE_HPP_INCLUDED
#define ZLINK_CPP_MESSAGE_HPP_INCLUDED

#include "common.hpp"

namespace zlink
{

class message_t
{
  public:
    message_t () : _valid (false)
    {
        if (zlink_msg_init (&_msg) == 0)
            _valid = true;
    }

    explicit message_t (size_t size_) : _valid (false)
    {
        if (zlink_msg_init_size (&_msg, size_) == 0)
            _valid = true;
    }

    ~message_t () { close (); }

    message_t (message_t &&other) noexcept : _valid (false)
    {
        if (!other._valid)
            return;

        if (zlink_msg_init (&_msg) != 0)
            return;

        if (zlink_msg_move (&_msg, &other._msg) == 0) {
            _valid = true;
            other._valid = false;
            return;
        }

        zlink_msg_close (&_msg);
    }

    message_t &operator= (message_t &&other) noexcept
    {
        if (this == &other)
            return *this;

        close ();
        if (!other._valid)
            return *this;

        if (zlink_msg_init (&_msg) != 0)
            return *this;

        if (zlink_msg_move (&_msg, &other._msg) == 0) {
            _valid = true;
            other._valid = false;
            return *this;
        }

        zlink_msg_close (&_msg);
        return *this;
    }

    message_t (const message_t &) = delete;
    message_t &operator= (const message_t &) = delete;

    bool valid () const noexcept { return _valid; }

    int init ()
    {
        if (_valid)
            return 0;
        if (zlink_msg_init (&_msg) != 0)
            return -1;
        _valid = true;
        return 0;
    }

    int init_size (size_t size_)
    {
        close ();
        if (zlink_msg_init_size (&_msg, size_) != 0)
            return -1;
        _valid = true;
        return 0;
    }

    int init_data (void *data_,
                   size_t size_,
                   zlink_free_fn *ffn_ = NULL,
                   void *hint_ = NULL)
    {
        close ();
        if (zlink_msg_init_data (&_msg, data_, size_, ffn_, hint_) != 0)
            return -1;
        _valid = true;
        return 0;
    }

    void *data () noexcept { return _valid ? zlink_msg_data (&_msg) : NULL; }

    const void *data () const noexcept
    {
        return _valid ? zlink_msg_data (const_cast<zlink_msg_t *> (&_msg)) : NULL;
    }

    size_t size () const noexcept
    {
        return _valid ? zlink_msg_size (&_msg) : 0;
    }

    bool more () const noexcept { return _valid && zlink_msg_more (&_msg) != 0; }

    int get (int property_) const
    {
        return _valid ? zlink_msg_get (&_msg, property_) : -1;
    }

    int set (int property_, int optval_)
    {
        return _valid ? zlink_msg_set (&_msg, property_, optval_) : -1;
    }

    const char *gets (const char *property_) const
    {
        return (_valid && property_) ? zlink_msg_gets (&_msg, property_) : NULL;
    }

    int close () noexcept
    {
        if (!_valid)
            return 0;
        const int rc = zlink_msg_close (&_msg);
        _valid = false;
        return rc;
    }

    zlink_msg_t *handle () noexcept { return &_msg; }
    const zlink_msg_t *handle () const noexcept { return &_msg; }

    int move_to (zlink_msg_t *dest_)
    {
        if (!dest_ || !_valid)
            return -1;
        if (zlink_msg_init (dest_) != 0)
            return -1;

        const int rc = zlink_msg_move (dest_, &_msg);
        if (rc == 0)
            _valid = false;
        return rc;
    }

    int copy_to (zlink_msg_t *dest_) const
    {
        if (!dest_ || !_valid)
            return -1;
        if (zlink_msg_init (dest_) != 0)
            return -1;

        return zlink_msg_copy (dest_, const_cast<zlink_msg_t *> (&_msg));
    }

  private:
    zlink_msg_t _msg;
    bool _valid;
};

} // namespace zlink

#endif
