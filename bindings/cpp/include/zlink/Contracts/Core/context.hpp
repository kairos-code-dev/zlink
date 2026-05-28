/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Errors/errors.hpp"

#include <chrono>
#include <climits>
#include <memory>
#include <string_view>

namespace zlink
{

class context_t;
namespace detail
{
struct context_access_t;
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
    context_t ();
    explicit context_t (io_thread_count_t io_threads_);
    ~context_t ();

    context_t (context_t &&other) noexcept;
    context_t &operator= (context_t &&other) noexcept;

    context_t (const context_t &) = delete;
    context_t &operator= (const context_t &) = delete;

    bool valid () const noexcept;

    void shutdown ();
    void term ();

    context_options_t options () { return context_options_t (*this); }

    void recalculate_auto_hwm ();

  private:
    friend class context_options_t;
    friend struct detail::context_access_t;

    void term_noexcept () noexcept;
    int get_option_raw (int option_, int *error_out_) const;
    config_result_t set_option_raw (int option_, int value_);
    config_result_t set_option_data_raw (int option_, std::string_view value_);

    struct impl;
    std::unique_ptr<impl> _impl;
};

} // namespace zlink
