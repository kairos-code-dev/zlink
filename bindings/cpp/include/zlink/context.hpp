/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_CONTEXT_HPP_INCLUDED
#define ZLINK_CPP_CONTEXT_HPP_INCLUDED

#include "error.hpp"

namespace zlink
{

class context_t;

class context_options_t
{
  public:
    explicit context_options_t (context_t &ctx_) : _ctx (ctx_) {}

    int ioThreads () const;
    void ioThreads (int value_);
    int maxSockets () const;
    void maxSockets (int value_);
    int maxMsgSize () const;
    void maxMsgSize (int value_);
    int threadPriority () const;
    void threadPriority (int value_);
    int threadSchedulingPolicy () const;
    void threadSchedulingPolicy (int value_);
    bool blocky () const;
    void blocky (bool enabled_);
    int socketLimit () const;
    int msgTSize () const;
    void addThreadAffinity (int cpu_);
    void removeThreadAffinity (int cpu_);

  private:
    context_t &_ctx;
};

class context_t
{
  public:
    context_t () : _ctx (zlink_ctx_new ()) {}

    explicit context_t (int io_threads) : _ctx (zlink_ctx_new ())
    {
        if (_ctx)
            (void) zlink_ctx_set (_ctx, ZLINK_IO_THREADS, io_threads);
    }

    ~context_t () { (void) term (); }

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
    void *handle () noexcept { return _ctx; }
    const void *handle () const noexcept { return _ctx; }

    void shutdown ()
    {
        if (!_ctx)
            throw close_error_t (close_result_t::invalid_handle);
        detail::throw_if_failed<close_error_t> (
          static_cast<close_result_t> (zlink_ctx_shutdown (_ctx)));
    }

    void term () noexcept
    {
        if (!_ctx)
            return;
        void *ctx = _ctx;
        _ctx = NULL;
        (void) zlink_ctx_term (ctx);
    }

    context_options_t options () { return context_options_t (*this); }

  private:
    friend class context_options_t;

    int get_option_raw (zlink_ctx_option_t option_, zlink_config_result_t *error_out_) const
    {
        if (!_ctx) {
            if (error_out_)
                *error_out_ = ZLINK_CONFIG_INVALID_HANDLE;
            return -1;
        }
        return zlink_ctx_get (_ctx, option_, error_out_);
    }

    zlink_config_result_t set_option_raw (zlink_ctx_option_t option_, int value_)
    {
        if (!_ctx)
            return ZLINK_CONFIG_INVALID_HANDLE;
        return zlink_ctx_set (_ctx, option_, value_);
    }

    void *_ctx;
};

inline int context_options_t::ioThreads () const
{
    zlink_config_result_t error = ZLINK_CONFIG_OK;
    const int value = _ctx.get_option_raw (ZLINK_IO_THREADS, &error);
    if (error != ZLINK_CONFIG_OK)
        throw config_error_t (static_cast<config_result_t> (error));
    return value;
}

inline void context_options_t::ioThreads (int value_)
{
    detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (_ctx.set_option_raw (ZLINK_IO_THREADS, value_)));
}

inline int context_options_t::maxSockets () const
{
    zlink_config_result_t error = ZLINK_CONFIG_OK;
    const int value = _ctx.get_option_raw (ZLINK_MAX_SOCKETS, &error);
    if (error != ZLINK_CONFIG_OK)
        throw config_error_t (static_cast<config_result_t> (error));
    return value;
}

inline void context_options_t::maxSockets (int value_)
{
    detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (_ctx.set_option_raw (ZLINK_MAX_SOCKETS, value_)));
}

inline int context_options_t::maxMsgSize () const
{
    zlink_config_result_t error = ZLINK_CONFIG_OK;
    const int value = _ctx.get_option_raw (ZLINK_MAX_MSGSZ, &error);
    if (error != ZLINK_CONFIG_OK)
        throw config_error_t (static_cast<config_result_t> (error));
    return value;
}

inline void context_options_t::maxMsgSize (int value_)
{
    detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (_ctx.set_option_raw (ZLINK_MAX_MSGSZ, value_)));
}

inline int context_options_t::threadPriority () const
{
    zlink_config_result_t error = ZLINK_CONFIG_OK;
    const int value = _ctx.get_option_raw (ZLINK_THREAD_PRIORITY, &error);
    if (error != ZLINK_CONFIG_OK)
        throw config_error_t (static_cast<config_result_t> (error));
    return value;
}

inline void context_options_t::threadPriority (int value_)
{
    detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (
        _ctx.set_option_raw (ZLINK_THREAD_PRIORITY, value_)));
}

inline int context_options_t::threadSchedulingPolicy () const
{
    zlink_config_result_t error = ZLINK_CONFIG_OK;
    const int value = _ctx.get_option_raw (ZLINK_THREAD_SCHED_POLICY, &error);
    if (error != ZLINK_CONFIG_OK)
        throw config_error_t (static_cast<config_result_t> (error));
    return value;
}

inline void context_options_t::threadSchedulingPolicy (int value_)
{
    detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (
        _ctx.set_option_raw (ZLINK_THREAD_SCHED_POLICY, value_)));
}

inline bool context_options_t::blocky () const
{
    zlink_config_result_t error = ZLINK_CONFIG_OK;
    const int value = _ctx.get_option_raw (ZLINK_CTX_OPT_BLOCKY, &error);
    if (error != ZLINK_CONFIG_OK)
        throw config_error_t (static_cast<config_result_t> (error));
    return value != 0;
}

inline void context_options_t::blocky (bool enabled_)
{
    detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (
        _ctx.set_option_raw (ZLINK_CTX_OPT_BLOCKY, enabled_ ? 1 : 0)));
}

inline int context_options_t::socketLimit () const
{
    zlink_config_result_t error = ZLINK_CONFIG_OK;
    const int value = _ctx.get_option_raw (ZLINK_SOCKET_LIMIT, &error);
    if (error != ZLINK_CONFIG_OK)
        throw config_error_t (static_cast<config_result_t> (error));
    return value;
}

inline int context_options_t::msgTSize () const
{
    zlink_config_result_t error = ZLINK_CONFIG_OK;
    const int value = _ctx.get_option_raw (ZLINK_MSG_T_SIZE, &error);
    if (error != ZLINK_CONFIG_OK)
        throw config_error_t (static_cast<config_result_t> (error));
    return value;
}

inline void context_options_t::addThreadAffinity (int cpu_)
{
    detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (
        _ctx.set_option_raw (ZLINK_THREAD_AFFINITY_CPU_ADD, cpu_)));
}

inline void context_options_t::removeThreadAffinity (int cpu_)
{
    detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (
        _ctx.set_option_raw (ZLINK_THREAD_AFFINITY_CPU_REMOVE, cpu_)));
}

} // namespace zlink

#endif
