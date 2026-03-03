/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_CONTEXT_HPP_INCLUDED
#define ZLINK_CPP_CONTEXT_HPP_INCLUDED

#include "types.hpp"

namespace zlink
{

class context_t
{
  public:
    context_t () : _ctx (zlink_ctx_new ()) {}

    explicit context_t (int io_threads) : _ctx (zlink_ctx_new ())
    {
        if (_ctx)
            zlink_ctx_set (_ctx, ZLINK_IO_THREADS, io_threads);
    }

    ~context_t ()
    {
        if (_ctx)
            zlink_ctx_term (_ctx);
        _ctx = NULL;
    }

    context_t (context_t &&other) noexcept : _ctx (other._ctx)
    {
        other._ctx = NULL;
    }

    context_t &operator= (context_t &&other) noexcept
    {
        if (this == &other)
            return *this;

        if (_ctx)
            zlink_ctx_term (_ctx);
        _ctx = other._ctx;
        other._ctx = NULL;
        return *this;
    }

    context_t (const context_t &) = delete;
    context_t &operator= (const context_t &) = delete;

    void *handle () noexcept { return _ctx; }
    const void *handle () const noexcept { return _ctx; }

    int shutdown ()
    {
        return _ctx ? zlink_ctx_shutdown (_ctx) : -1;
    }

    int set (context_option option_, int value_)
    {
        return _ctx ? zlink_ctx_set (_ctx, static_cast<int> (option_), value_)
                    : -1;
    }

    int get (context_option option_, int *value_) const
    {
        if (!_ctx || !value_)
            return -1;
        const int rc = zlink_ctx_get (_ctx, static_cast<int> (option_));
        if (rc < 0)
            return -1;
        *value_ = rc;
        return 0;
    }

  private:
    void *_ctx;
};

} // namespace zlink

#endif
