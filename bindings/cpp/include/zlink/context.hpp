/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_CONTEXT_HPP_INCLUDED
#define ZLINK_CPP_CONTEXT_HPP_INCLUDED

#include "types.hpp"
#include <cerrno>

namespace zlink
{

class context_t;

class context_options_t
{
  public:
    explicit context_options_t (context_t &ctx_) : _ctx (ctx_) {}

    ZLINK_CPP_NODISCARD int ioThreads () const;
    ZLINK_CPP_NODISCARD int ioThreads (int value_);
    ZLINK_CPP_NODISCARD int maxSockets () const;
    ZLINK_CPP_NODISCARD int maxSockets (int value_);
    ZLINK_CPP_NODISCARD int maxMsgSize () const;
    ZLINK_CPP_NODISCARD int maxMsgSize (int value_);
    ZLINK_CPP_NODISCARD int threadPriority () const;
    ZLINK_CPP_NODISCARD int threadPriority (int value_);
    ZLINK_CPP_NODISCARD int threadSchedulingPolicy () const;
    ZLINK_CPP_NODISCARD int threadSchedulingPolicy (int value_);
    ZLINK_CPP_NODISCARD bool blocky () const;
    ZLINK_CPP_NODISCARD int blocky (bool enabled_);
    ZLINK_CPP_NODISCARD int socketLimit () const;
    ZLINK_CPP_NODISCARD int msgTSize () const;
    ZLINK_CPP_NODISCARD int addThreadAffinity (int cpu_);
    ZLINK_CPP_NODISCARD int removeThreadAffinity (int cpu_);

  private:
    context_t &_ctx;
};

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
        (void) term ();
    }

    context_t (context_t &&other) noexcept : _ctx (other._ctx)
    {
        other._ctx = NULL;
    }

    context_t &operator= (context_t &&other) noexcept
    {
        if (this == &other)
            return *this;

        (void) term ();
        _ctx = other._ctx;
        other._ctx = NULL;
        return *this;
    }

    context_t (const context_t &) = delete;
    context_t &operator= (const context_t &) = delete;

    bool valid () const noexcept { return _ctx != NULL; }

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
     * @brief Terminate and release the context early.
     * @return 0 on success, -1 on failure.
     */
    ZLINK_CPP_NODISCARD int term () noexcept
    {
        if (!_ctx)
            return 0;

        void *ctx = _ctx;
        _ctx = NULL;
        return zlink_ctx_term (ctx);
    }

    context_options_t options () { return context_options_t (*this); }

  private:
    friend class context_options_t;

    ZLINK_CPP_NODISCARD int set_option_raw (context_option option_, int value_)
    {
        return _ctx
                 ? zlink_ctx_set (
                     _ctx, static_cast<zlink_ctx_option_t> (option_), value_)
                 : -1;
    }

    ZLINK_CPP_NODISCARD int get_option_raw (context_option option_) const
    {
        if (!_ctx)
            return -1;
        errno = 0;
        return zlink_ctx_get (_ctx, static_cast<zlink_ctx_option_t> (option_), nullptr);
    }

    void *_ctx;
};

inline int context_options_t::ioThreads () const
{
    return _ctx.get_option_raw (context_option::io_threads);
}

inline int context_options_t::ioThreads (int value_)
{
    return _ctx.set_option_raw (context_option::io_threads, value_);
}

inline int context_options_t::maxSockets () const
{
    return _ctx.get_option_raw (context_option::max_sockets);
}

inline int context_options_t::maxSockets (int value_)
{
    return _ctx.set_option_raw (context_option::max_sockets, value_);
}

inline int context_options_t::maxMsgSize () const
{
    return _ctx.get_option_raw (context_option::max_msgsz);
}

inline int context_options_t::maxMsgSize (int value_)
{
    return _ctx.set_option_raw (context_option::max_msgsz, value_);
}

inline int context_options_t::threadPriority () const
{
    return _ctx.get_option_raw (context_option::thread_priority);
}

inline int context_options_t::threadPriority (int value_)
{
    return _ctx.set_option_raw (context_option::thread_priority, value_);
}

inline int context_options_t::threadSchedulingPolicy () const
{
    return _ctx.get_option_raw (context_option::thread_sched_policy);
}

inline int context_options_t::threadSchedulingPolicy (int value_)
{
    return _ctx.set_option_raw (context_option::thread_sched_policy, value_);
}

inline bool context_options_t::blocky () const
{
    return _ctx.get_option_raw (context_option::blocky) != 0;
}

inline int context_options_t::blocky (bool enabled_)
{
    return _ctx.set_option_raw (context_option::blocky, enabled_ ? 1 : 0);
}

inline int context_options_t::socketLimit () const
{
    return _ctx.get_option_raw (context_option::socket_limit);
}

inline int context_options_t::msgTSize () const
{
    return _ctx.get_option_raw (context_option::msg_t_size);
}

inline int context_options_t::addThreadAffinity (int cpu_)
{
    return _ctx.set_option_raw (context_option::thread_affinity_cpu_add, cpu_);
}

inline int context_options_t::removeThreadAffinity (int cpu_)
{
    return _ctx.set_option_raw (context_option::thread_affinity_cpu_remove, cpu_);
}

} // namespace zlink

#endif
