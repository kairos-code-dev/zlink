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

    int io_threads () const;
    void io_threads (int value_);
    int ioThreads () const;
    void ioThreads (int value_);
    int max_sockets () const;
    void max_sockets (int value_);
    int maxSockets () const;
    void maxSockets (int value_);
    int max_msg_size () const;
    void max_msg_size (int value_);
    int maxMsgSize () const;
    void maxMsgSize (int value_);
    int thread_priority () const;
    void thread_priority (int value_);
    int threadPriority () const;
    void threadPriority (int value_);
    int thread_scheduling_policy () const;
    void thread_scheduling_policy (int value_);
    int threadSchedulingPolicy () const;
    void threadSchedulingPolicy (int value_);
    bool blocky () const;
    void blocky (bool enabled_);
    bool auto_hwm_enabled () const;
    void auto_hwm_enabled (bool enabled_);
    int auto_hwm_total_memory_budget_mb () const;
    void auto_hwm_total_memory_budget_mb (int value_);
    auto_hwm_profile auto_hwm_profile_value () const;
    void auto_hwm_profile_value (auto_hwm_profile profile_);
    auto_hwm_profile autoHwmProfile () const;
    void autoHwmProfile (auto_hwm_profile profile_);
    int socket_limit () const;
    int socketLimit () const;
    int msg_t_size () const;
    int msgTSize () const;
    void add_thread_affinity (int cpu_);
    void remove_thread_affinity (int cpu_);
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

    void auto_hwm_recalculate ()
    {
        if (!_ctx)
            throw config_error_t (config_result_t::invalid_handle);
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            zlink_ctx_auto_hwm_recalculate (_ctx)));
    }

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

inline int context_options_t::io_threads () const
{
    zlink_config_result_t error = ZLINK_CONFIG_OK;
    const int value = _ctx.get_option_raw (ZLINK_IO_THREADS, &error);
    if (error != ZLINK_CONFIG_OK)
        throw config_error_t (static_cast<config_result_t> (error));
    return value;
}

inline int context_options_t::ioThreads () const
{
    return io_threads ();
}

inline void context_options_t::io_threads (int value_)
{
    detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (_ctx.set_option_raw (ZLINK_IO_THREADS, value_)));
}

inline void context_options_t::ioThreads (int value_)
{
    io_threads (value_);
}

inline int context_options_t::max_sockets () const
{
    zlink_config_result_t error = ZLINK_CONFIG_OK;
    const int value = _ctx.get_option_raw (ZLINK_MAX_SOCKETS, &error);
    if (error != ZLINK_CONFIG_OK)
        throw config_error_t (static_cast<config_result_t> (error));
    return value;
}

inline int context_options_t::maxSockets () const
{
    return max_sockets ();
}

inline void context_options_t::max_sockets (int value_)
{
    detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (_ctx.set_option_raw (ZLINK_MAX_SOCKETS, value_)));
}

inline void context_options_t::maxSockets (int value_)
{
    max_sockets (value_);
}

inline int context_options_t::max_msg_size () const
{
    zlink_config_result_t error = ZLINK_CONFIG_OK;
    const int value = _ctx.get_option_raw (ZLINK_MAX_MSGSZ, &error);
    if (error != ZLINK_CONFIG_OK)
        throw config_error_t (static_cast<config_result_t> (error));
    return value;
}

inline int context_options_t::maxMsgSize () const
{
    return max_msg_size ();
}

inline void context_options_t::max_msg_size (int value_)
{
    detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (_ctx.set_option_raw (ZLINK_MAX_MSGSZ, value_)));
}

inline void context_options_t::maxMsgSize (int value_)
{
    max_msg_size (value_);
}

inline int context_options_t::thread_priority () const
{
    zlink_config_result_t error = ZLINK_CONFIG_OK;
    const int value = _ctx.get_option_raw (ZLINK_THREAD_PRIORITY, &error);
    if (error != ZLINK_CONFIG_OK)
        throw config_error_t (static_cast<config_result_t> (error));
    return value;
}

inline int context_options_t::threadPriority () const
{
    return thread_priority ();
}

inline void context_options_t::thread_priority (int value_)
{
    detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (
        _ctx.set_option_raw (ZLINK_THREAD_PRIORITY, value_)));
}

inline void context_options_t::threadPriority (int value_)
{
    thread_priority (value_);
}

inline int context_options_t::thread_scheduling_policy () const
{
    zlink_config_result_t error = ZLINK_CONFIG_OK;
    const int value = _ctx.get_option_raw (ZLINK_THREAD_SCHED_POLICY, &error);
    if (error != ZLINK_CONFIG_OK)
        throw config_error_t (static_cast<config_result_t> (error));
    return value;
}

inline int context_options_t::threadSchedulingPolicy () const
{
    return thread_scheduling_policy ();
}

inline void context_options_t::thread_scheduling_policy (int value_)
{
    detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (
        _ctx.set_option_raw (ZLINK_THREAD_SCHED_POLICY, value_)));
}

inline void context_options_t::threadSchedulingPolicy (int value_)
{
    thread_scheduling_policy (value_);
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

inline bool context_options_t::auto_hwm_enabled () const
{
    zlink_config_result_t error = ZLINK_CONFIG_OK;
    const int value =
      _ctx.get_option_raw (ZLINK_CTX_OPT_AUTO_HWM_ENABLE, &error);
    if (error != ZLINK_CONFIG_OK)
        throw config_error_t (static_cast<config_result_t> (error));
    return value != 0;
}

inline void context_options_t::auto_hwm_enabled (bool enabled_)
{
    detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (_ctx.set_option_raw (
        ZLINK_CTX_OPT_AUTO_HWM_ENABLE, enabled_ ? 1 : 0)));
}

inline int context_options_t::auto_hwm_total_memory_budget_mb () const
{
    zlink_config_result_t error = ZLINK_CONFIG_OK;
    const int value = _ctx.get_option_raw (
      ZLINK_CTX_OPT_AUTO_HWM_TOTAL_MEMORY_BUDGET_MB, &error);
    if (error != ZLINK_CONFIG_OK)
        throw config_error_t (static_cast<config_result_t> (error));
    return value;
}

inline void context_options_t::auto_hwm_total_memory_budget_mb (int value_)
{
    detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (_ctx.set_option_raw (
        ZLINK_CTX_OPT_AUTO_HWM_TOTAL_MEMORY_BUDGET_MB, value_)));
}

inline auto_hwm_profile context_options_t::auto_hwm_profile_value () const
{
    zlink_config_result_t error = ZLINK_CONFIG_OK;
    const int value =
      _ctx.get_option_raw (ZLINK_CTX_OPT_AUTO_HWM_PROFILE, &error);
    if (error != ZLINK_CONFIG_OK)
        throw config_error_t (static_cast<config_result_t> (error));
    return static_cast<auto_hwm_profile> (value);
}

inline auto_hwm_profile context_options_t::autoHwmProfile () const
{
    return auto_hwm_profile_value ();
}

inline void context_options_t::auto_hwm_profile_value (auto_hwm_profile profile_)
{
    detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (_ctx.set_option_raw (
        ZLINK_CTX_OPT_AUTO_HWM_PROFILE, static_cast<int> (profile_))));
}

inline void context_options_t::autoHwmProfile (auto_hwm_profile profile_)
{
    auto_hwm_profile_value (profile_);
}

inline int context_options_t::socket_limit () const
{
    zlink_config_result_t error = ZLINK_CONFIG_OK;
    const int value = _ctx.get_option_raw (ZLINK_SOCKET_LIMIT, &error);
    if (error != ZLINK_CONFIG_OK)
        throw config_error_t (static_cast<config_result_t> (error));
    return value;
}

inline int context_options_t::socketLimit () const
{
    return socket_limit ();
}

inline int context_options_t::msg_t_size () const
{
    zlink_config_result_t error = ZLINK_CONFIG_OK;
    const int value = _ctx.get_option_raw (ZLINK_MSG_T_SIZE, &error);
    if (error != ZLINK_CONFIG_OK)
        throw config_error_t (static_cast<config_result_t> (error));
    return value;
}

inline int context_options_t::msgTSize () const
{
    return msg_t_size ();
}

inline void context_options_t::add_thread_affinity (int cpu_)
{
    detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (
        _ctx.set_option_raw (ZLINK_THREAD_AFFINITY_CPU_ADD, cpu_)));
}

inline void context_options_t::addThreadAffinity (int cpu_)
{
    add_thread_affinity (cpu_);
}

inline void context_options_t::remove_thread_affinity (int cpu_)
{
    detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (
        _ctx.set_option_raw (ZLINK_THREAD_AFFINITY_CPU_REMOVE, cpu_)));
}

inline void context_options_t::removeThreadAffinity (int cpu_)
{
    remove_thread_affinity (cpu_);
}

} // namespace zlink

#endif
