/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "context_options.hpp"
#include "../Errors/errors.hpp"

#include <memory>
#include <string_view>

namespace zlink
{

class context_t;
namespace detail
{
struct context_access_t;
} // namespace detail

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
