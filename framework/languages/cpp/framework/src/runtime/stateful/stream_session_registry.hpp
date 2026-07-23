/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "runtime/stateful/stateful_object_runtime.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace zlink::framework::runtime::stateful
{

struct stream_connection_t
{
    std::string connection_id;
    std::uint64_t connection_generation = 0;

    friend bool operator== (const stream_connection_t &,
                            const stream_connection_t &) = default;
};

struct stream_binding_t
{
    stream_connection_t connection;
    std::uint64_t binding_generation = 0;
    object_ref_t actor;

    friend bool operator== (const stream_binding_t &,
                            const stream_binding_t &) = default;
};

struct stream_dispatch_t
{
    stream_binding_t binding;
    std::uint64_t inbound_sequence = 0;
};

struct stream_barrier_t
{
    std::uint64_t token = 0;
    object_ref_t actor;
};

class stream_session_registry_t
{
  public:
    using authority_resolver_t =
      std::function<std::optional<object_ref_t> (const std::string &)>;

    explicit stream_session_registry_t (authority_resolver_t resolver);

    stream_connection_t open (std::string connection_id);
    bool close (const stream_connection_t &connection);
    std::pair<stateful_error_t, stream_binding_t> bind (
      const stream_connection_t &connection,
      const object_ref_t &actor);
    stateful_error_t unbind (const stream_binding_t &binding);
    std::pair<stateful_error_t, std::optional<stream_dispatch_t>>
    admit_inbound (const stream_binding_t &binding);
    stateful_error_t complete_inbound (const stream_dispatch_t &dispatch);
    std::pair<stateful_error_t, stream_barrier_t>
    try_seal_actor (const object_ref_t &actor);
    stateful_error_t abort_barrier (const stream_barrier_t &barrier);
    stateful_error_t commit_barrier (
      const stream_barrier_t &barrier, const object_ref_t &target);
    bool try_seal_all ();
    void release_all () noexcept;
    void force_close_all () noexcept;
    bool is_current (const stream_binding_t &binding) const;

  private:
    struct connection_state_t
    {
        stream_connection_t connection;
        std::uint64_t next_binding_generation = 1;
        std::uint64_t next_inbound_sequence = 1;
        std::optional<stream_binding_t> binding;
        std::set<std::uint64_t> active_inbound;
        std::optional<std::uint64_t> barrier_token;
    };

    static bool exact_actor (const object_ref_t &left,
                             const object_ref_t &right);

    authority_resolver_t _resolver;
    mutable std::mutex _mutex;
    std::map<std::string, connection_state_t> _connections;
    std::map<std::string, std::uint64_t> _last_connection_generation;
    std::map<std::uint64_t, object_ref_t> _barriers;
    std::uint64_t _next_barrier_token = 1;
    bool _all_sealed = false;
};

} // namespace zlink::framework::runtime::stateful
