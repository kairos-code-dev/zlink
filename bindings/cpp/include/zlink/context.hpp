/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_CONTEXT_HPP_INCLUDED
#define ZLINK_CPP_CONTEXT_HPP_INCLUDED

#include "types.hpp"
#include <cerrno>

namespace zlink
{

/**
 * @brief RAII wrapper for a zlink context.
 */
class context_t
{
  public:
    /**
     * @brief Create a context with default options.
     */
    context_t () : _ctx (zlink_ctx_new ()) {}

    /**
     * @brief Create a context and set IO thread count.
     * @param io_threads Number of IO threads.
     */
    explicit context_t (int io_threads) : _ctx (zlink_ctx_new ())
    {
        if (_ctx)
            zlink_ctx_set (_ctx, ZLINK_IO_THREADS, io_threads);
    }

    /**
     * @brief Terminate and release the context.
     */
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

    /**
     * @brief Access the raw context handle.
     * @return Mutable native handle.
     */
    void *handle () noexcept { return _ctx; }
    /**
     * @brief Access the raw context handle.
     * @return Const native handle.
     */
    const void *handle () const noexcept { return _ctx; }

    /**
     * @brief Request asynchronous context shutdown.
     * @return 0 on success, -1 on failure.
     */
    ZLINK_CPP_NODISCARD int shutdown ()
    {
        return _ctx ? zlink_ctx_shutdown (_ctx) : -1;
    }

    /**
     * @brief Set a context option.
     * @param option_ Option key.
     * @param value_ Option value.
     * @return 0 on success, -1 on failure.
     */
    ZLINK_CPP_NODISCARD int set (context_option option_, int value_)
    {
        return _ctx ? zlink_ctx_set (_ctx, static_cast<int> (option_), value_)
                    : -1;
    }

    /**
     * @brief Get a context option.
     * @param option_ Option key.
     * @param value_ Output pointer for the value.
     * @return 0 on success, -1 on failure.
     */
    ZLINK_CPP_NODISCARD int get (context_option option_, int *value_) const
    {
        if (!_ctx || !value_)
            return -1;
        errno = 0;
        const int rc = zlink_ctx_get (_ctx, static_cast<int> (option_));
        if (rc == -1 && errno != 0)
            return -1;
        *value_ = rc;
        return 0;
    }

    /**
     * @brief Get a context option.
     * @param option_ Option key.
     * @param value_ Output reference for the value.
     * @return 0 on success, -1 on failure.
     */
    ZLINK_CPP_NODISCARD int get (context_option option_, int &value_) const
    {
        return get (option_, &value_);
    }

  private:
    void *_ctx;
};

} // namespace zlink

#endif
