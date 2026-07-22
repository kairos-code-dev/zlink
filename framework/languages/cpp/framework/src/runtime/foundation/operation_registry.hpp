/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace zlink::framework::runtime::foundation
{

using operation_id_t = std::array<std::uint8_t, 16>;

enum class operation_terminal_t
{
    completed,
    timed_out,
    cancelled,
    shutdown
};

class operation_registry_t
{
  public:
    using clock_t = std::chrono::steady_clock;
    using callback_t = std::function<void (operation_terminal_t, std::vector<std::uint8_t>)>;

    explicit operation_registry_t (std::size_t capacity);
    ~operation_registry_t () noexcept;

    bool register_operation (operation_id_t id,
                             clock_t::time_point deadline,
                             callback_t callback);
    bool complete (const operation_id_t &id, std::vector<std::uint8_t> payload);
    bool cancel (const operation_id_t &id);
    std::size_t expire (clock_t::time_point now);
    std::size_t shutdown ();
    std::size_t size () const;

  private:
    struct id_hash_t
    {
        std::size_t operator() (const operation_id_t &id) const noexcept;
    };

    struct pending_t
    {
        clock_t::time_point deadline;
        callback_t callback;
    };

    using entry_t = std::pair<callback_t, operation_terminal_t>;
    bool take (const operation_id_t &id, callback_t &callback);

    const std::size_t _capacity;
    mutable std::mutex _mutex;
    std::unordered_map<operation_id_t, pending_t, id_hash_t> _pending;
    bool _closed = false;
};

} // namespace zlink::framework::runtime::foundation
