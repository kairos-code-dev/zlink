/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Errors/errors.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

namespace zlink
{

class timer_t;
namespace service
{
class spot_t;
} // namespace service
namespace detail
{
struct timer_access_t;
} // namespace detail

class timer_t
{
  public:
    timer_t ();

    ~timer_t ();

    timer_t (timer_t &&other) noexcept;

    timer_t &operator= (timer_t &&other) noexcept;

    timer_t (const timer_t &) = delete;
    timer_t &operator= (const timer_t &) = delete;

    static timer_t from_spot (service::spot_t &spot_);

    bool valid () const noexcept;

    template<class Rep, class Period>
    void start (std::chrono::duration<Rep, Period> interval_,
                uint64_t repeat_count_ = 0)
    {
        const uint64_t interval_ns = static_cast<uint64_t> (
          std::chrono::duration_cast<std::chrono::nanoseconds> (interval_)
            .count ());
        start_ns (interval_ns, repeat_count_);
    }

  private:
    void start_ns (uint64_t interval_ns_, uint64_t repeat_count_);

  public:
    void stop ();

    std::optional<uint64_t> recv ();

    void set_handler (std::function<void(uint64_t)> handler_);

    void destroy ();

  private:
    struct impl;
    std::unique_ptr<impl> _impl;
    friend struct detail::timer_access_t;
};

} // namespace zlink
