/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_CONTEXT_HPP_INCLUDED
#define ZLINK_CPP_CONTEXT_HPP_INCLUDED

#include "../Errors/error.hpp"

#include <chrono>
#include <climits>

namespace zlink
{

class context_t;
namespace detail
{
inline void *native_handle (context_t &ctx_) noexcept;
inline const void *native_handle (const context_t &ctx_) noexcept;
} // namespace detail

class context_options_t
{
  public:
    explicit context_options_t (context_t &ctx_) : _ctx (ctx_) {}

    io_thread_count_t io_threads () const;
    void io_threads (io_thread_count_t value_);
    socket_count_t max_sockets () const;
    void max_sockets (socket_count_t value_);
    byte_size_t max_msg_size () const;
    void max_msg_size (byte_size_t value_);
    std::optional<thread_priority_t> thread_priority () const;
    void thread_priority (thread_priority_t value_);
    thread_scheduling_policy_t thread_scheduling_policy () const;
    void thread_scheduling_policy (thread_scheduling_policy_t value_);
    std::string thread_name_prefix () const;
    void thread_name_prefix (const std::string &value_);
    bool blocky () const;
    void blocky (bool enabled_);
    bool auto_hwm_enabled () const;
    void auto_hwm_enabled (bool enabled_);
    std::chrono::milliseconds auto_hwm_recalc_debounce () const;
    void auto_hwm_recalc_debounce (std::chrono::milliseconds value_);
    zlink::auto_hwm_profile auto_hwm_profile () const;
    void auto_hwm_profile (zlink::auto_hwm_profile profile_);
    byte_size_t auto_hwm_msg_unit_bytes () const;
    void auto_hwm_msg_unit_bytes (byte_size_t value_);
    socket_count_t socket_limit () const;
    byte_size_t msg_t_size () const;
    void add_thread_affinity (cpu_index_t cpu_);
    void remove_thread_affinity (cpu_index_t cpu_);

  private:
    context_t &_ctx;
};

class context_t
{
  public:
    context_t () : _ctx (zlink_ctx_new ()), _thread_name_prefix () {}

    explicit context_t (io_thread_count_t io_threads_)
        : _ctx (zlink_ctx_new ()), _thread_name_prefix ()
    {
        if (_ctx)
            (void) zlink_ctx_set (_ctx, ZLINK_IO_THREADS, io_threads_.value ());
    }

    ~context_t () { term_noexcept (); }

    context_t (context_t &&other) noexcept
        : _ctx (other._ctx),
          _thread_name_prefix (std::move (other._thread_name_prefix))
    {
        other._ctx = NULL;
    }

    context_t &operator= (context_t &&other) noexcept
    {
        if (this == &other)
            return *this;
        term_noexcept ();
        _ctx = other._ctx;
        _thread_name_prefix = std::move (other._thread_name_prefix);
        other._ctx = NULL;
        return *this;
    }

    context_t (const context_t &) = delete;
    context_t &operator= (const context_t &) = delete;

    bool valid () const noexcept { return _ctx != NULL; }

    void shutdown ()
    {
        if (!_ctx)
            throw close_error_t (close_result_t::invalid_handle);
        detail::throw_if_failed<close_error_t> (
          static_cast<close_result_t> (zlink_ctx_shutdown (_ctx)));
    }

    void term ()
    {
        if (!_ctx)
            return;
        void *ctx = _ctx;
        _ctx = NULL;
        detail::throw_if_failed<close_error_t> (
          static_cast<close_result_t> (zlink_ctx_term (ctx)));
    }

    context_options_t options () { return context_options_t (*this); }

    void recalculate_auto_hwm ()
    {
        if (!_ctx)
            throw config_error_t (config_result_t::invalid_handle);
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            zlink_ctx_auto_hwm_recalculate (_ctx)));
    }

  private:
    friend class context_options_t;
    friend void *detail::native_handle (context_t &ctx_) noexcept;
    friend const void *detail::native_handle (const context_t &ctx_) noexcept;

    void term_noexcept () noexcept
    {
        if (!_ctx)
            return;
        void *ctx = _ctx;
        _ctx = NULL;
        (void) zlink_ctx_term (ctx);
    }

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

    zlink_config_result_t set_option_data_raw (zlink_ctx_option_t option_,
                                               const void *value_,
                                               size_t size_)
    {
        if (!_ctx)
            return ZLINK_CONFIG_INVALID_HANDLE;
        return zlink_ctx_set_data (_ctx, option_, value_, size_);
    }

    void *_ctx;
    std::string _thread_name_prefix;
};

namespace detail
{
inline void *native_handle (context_t &ctx_) noexcept { return ctx_._ctx; }
inline const void *native_handle (const context_t &ctx_) noexcept
{
    return ctx_._ctx;
}
} // namespace detail

inline io_thread_count_t context_options_t::io_threads () const
{
    zlink_config_result_t error = ZLINK_CONFIG_OK;
    const int value = _ctx.get_option_raw (ZLINK_IO_THREADS, &error);
    if (error != ZLINK_CONFIG_OK)
        throw config_error_t (static_cast<config_result_t> (error));
    return io_thread_count_t::value (value);
}

inline void context_options_t::io_threads (io_thread_count_t value_)
{
    detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (
        _ctx.set_option_raw (ZLINK_IO_THREADS, value_.value ())));
}

inline socket_count_t context_options_t::max_sockets () const
{
    zlink_config_result_t error = ZLINK_CONFIG_OK;
    const int value = _ctx.get_option_raw (ZLINK_MAX_SOCKETS, &error);
    if (error != ZLINK_CONFIG_OK)
        throw config_error_t (static_cast<config_result_t> (error));
    return socket_count_t::value (value);
}

inline void context_options_t::max_sockets (socket_count_t value_)
{
    detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (
        _ctx.set_option_raw (ZLINK_MAX_SOCKETS, value_.value ())));
}

inline byte_size_t context_options_t::max_msg_size () const
{
    zlink_config_result_t error = ZLINK_CONFIG_OK;
    const int value = _ctx.get_option_raw (ZLINK_MAX_MSGSZ, &error);
    if (error != ZLINK_CONFIG_OK)
        throw config_error_t (static_cast<config_result_t> (error));
    return byte_size_t::bytes (value);
}

inline void context_options_t::max_msg_size (byte_size_t value_)
{
    detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (
        _ctx.set_option_raw (ZLINK_MAX_MSGSZ,
                             static_cast<int> (value_.bytes ()))));
}

inline std::optional<thread_priority_t> context_options_t::thread_priority () const
{
    zlink_config_result_t error = ZLINK_CONFIG_OK;
    const int value = _ctx.get_option_raw (ZLINK_THREAD_PRIORITY, &error);
    if (error != ZLINK_CONFIG_OK)
        throw config_error_t (static_cast<config_result_t> (error));
    if (value == ZLINK_THREAD_PRIORITY_DFLT)
        return std::nullopt;
    return thread_priority_t::value (value);
}

inline void context_options_t::thread_priority (thread_priority_t value_)
{
    detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (
        _ctx.set_option_raw (ZLINK_THREAD_PRIORITY, value_.value ())));
}

inline thread_scheduling_policy_t context_options_t::thread_scheduling_policy () const
{
    zlink_config_result_t error = ZLINK_CONFIG_OK;
    const int value = _ctx.get_option_raw (ZLINK_THREAD_SCHED_POLICY, &error);
    if (error != ZLINK_CONFIG_OK)
        throw config_error_t (static_cast<config_result_t> (error));
    return static_cast<thread_scheduling_policy_t> (value);
}

inline void context_options_t::thread_scheduling_policy (
  thread_scheduling_policy_t value_)
{
    detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (
        _ctx.set_option_raw (ZLINK_THREAD_SCHED_POLICY,
                             static_cast<int> (value_))));
}

inline std::string context_options_t::thread_name_prefix () const
{
    return _ctx._thread_name_prefix;
}

inline void context_options_t::thread_name_prefix (const std::string &value_)
{
    detail::validate_bounded_c_string (value_, 16u, "thread_name_prefix");
    detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (_ctx.set_option_data_raw (
        ZLINK_THREAD_NAME_PREFIX, value_.data (), value_.size ())));
    _ctx._thread_name_prefix = value_;
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

inline std::chrono::milliseconds context_options_t::auto_hwm_recalc_debounce () const
{
    zlink_config_result_t error = ZLINK_CONFIG_OK;
    const int value = _ctx.get_option_raw (
      ZLINK_CTX_OPT_AUTO_HWM_RECALC_DEBOUNCE_MS, &error);
    if (error != ZLINK_CONFIG_OK)
        throw config_error_t (static_cast<config_result_t> (error));
    return std::chrono::milliseconds (value);
}

inline void context_options_t::auto_hwm_recalc_debounce (
  std::chrono::milliseconds value_)
{
    detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (_ctx.set_option_raw (
        ZLINK_CTX_OPT_AUTO_HWM_RECALC_DEBOUNCE_MS,
        static_cast<int> (value_.count ()))));
}

inline zlink::auto_hwm_profile context_options_t::auto_hwm_profile () const
{
    zlink_config_result_t error = ZLINK_CONFIG_OK;
    const int value =
      _ctx.get_option_raw (ZLINK_CTX_OPT_AUTO_HWM_PROFILE, &error);
    if (error != ZLINK_CONFIG_OK)
        throw config_error_t (static_cast<config_result_t> (error));
    return static_cast<zlink::auto_hwm_profile> (value);
}

inline void context_options_t::auto_hwm_profile (
  zlink::auto_hwm_profile profile_)
{
    detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (_ctx.set_option_raw (
        ZLINK_CTX_OPT_AUTO_HWM_PROFILE, static_cast<int> (profile_))));
}

inline byte_size_t context_options_t::auto_hwm_msg_unit_bytes () const
{
    zlink_config_result_t error = ZLINK_CONFIG_OK;
    const int value =
      _ctx.get_option_raw (ZLINK_CTX_OPT_AUTO_HWM_MSG_UNIT_BYTES, &error);
    if (error != ZLINK_CONFIG_OK)
        throw config_error_t (static_cast<config_result_t> (error));
    return byte_size_t::bytes (value);
}

inline void context_options_t::auto_hwm_msg_unit_bytes (byte_size_t value_)
{
    if (value_.bytes () < 0 || value_.bytes () > INT_MAX)
        throw config_error_t (config_result_t::invalid_argument, EINVAL);
    detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (_ctx.set_option_raw (
        ZLINK_CTX_OPT_AUTO_HWM_MSG_UNIT_BYTES,
        static_cast<int> (value_.bytes ()))));
}

inline socket_count_t context_options_t::socket_limit () const
{
    zlink_config_result_t error = ZLINK_CONFIG_OK;
    const int value = _ctx.get_option_raw (ZLINK_SOCKET_LIMIT, &error);
    if (error != ZLINK_CONFIG_OK)
        throw config_error_t (static_cast<config_result_t> (error));
    return socket_count_t::value (value);
}

inline byte_size_t context_options_t::msg_t_size () const
{
    zlink_config_result_t error = ZLINK_CONFIG_OK;
    const int value = _ctx.get_option_raw (ZLINK_MSG_T_SIZE, &error);
    if (error != ZLINK_CONFIG_OK)
        throw config_error_t (static_cast<config_result_t> (error));
    return byte_size_t::bytes (value);
}

inline void context_options_t::add_thread_affinity (cpu_index_t cpu_)
{
    detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (
        _ctx.set_option_raw (ZLINK_THREAD_AFFINITY_CPU_ADD, cpu_.value ())));
}

inline void context_options_t::remove_thread_affinity (cpu_index_t cpu_)
{
    detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (
        _ctx.set_option_raw (ZLINK_THREAD_AFFINITY_CPU_REMOVE, cpu_.value ())));
}

} // namespace zlink

#endif
