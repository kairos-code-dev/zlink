/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Core/routing_id.hpp"
#include "../Messaging/message.hpp"
#include "../Sockets/results.hpp"
#include "actor_models.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace zlink
{
namespace service
{

namespace detail
{
struct dispatch_access_t;
} // namespace detail

/// @brief Ready domains a dispatch drain may target (bit mask).
enum class ready_domain_t : uint32_t
{
    none = 0u,
    application = 1u << 0,
    infrastructure = 1u << 1,
    all = (1u << 0) | (1u << 1)
};

inline ready_domain_t operator| (ready_domain_t a_, ready_domain_t b_) noexcept
{
    return static_cast<ready_domain_t> (static_cast<uint32_t> (a_) | static_cast<uint32_t> (b_));
}

inline ready_domain_t operator& (ready_domain_t a_, ready_domain_t b_) noexcept
{
    return static_cast<ready_domain_t> (static_cast<uint32_t> (a_) & static_cast<uint32_t> (b_));
}

/// @brief The owner class a ready record refers to.
enum class owner_kind_t : int
{
    node = 1,
    spot = 2,
    actor = 3
};

/// @brief The wire record kind a received record carries.
enum class record_kind_t : int
{
    node_send = 1,
    node_request = 2,
    channel_send = 3,
    channel_request = 4,
    spot_send = 5,
    spot_request = 6,
    spot_multicast = 7,
    spot_control = 8,
    actor_send = 9,
    actor_request = 10,
    completion = 11,
    send_ready = 12,
    transfer_control = 13
};

/// @brief The operation family a completion or request record refers to.
enum class operation_kind_t : int
{
    none = 0,
    node_request = 1,
    channel_request = 2,
    spot_request = 3,
    actor_request = 4,
    actor_lookup = 5,
    actor_destroy = 6,
    actor_join = 7,
    actor_leave = 8,
    stream_bind = 9,
    stream_unbind = 10,
    stream_close = 11
};

/// @brief Identifies an in-flight operation whose completion arrives via dispatch.
struct operation_id_t
{
    uint64_t high = 0;
    uint64_t low = 0;

    bool operator== (const operation_id_t &other_) const noexcept
    {
        return high == other_.high && low == other_.low;
    }
    bool operator!= (const operation_id_t &other_) const noexcept { return !(*this == other_); }
};

/// @brief Opaque token used to reply to a received request record.
class reply_token_t
{
  public:
    reply_token_t () noexcept : _opaque{} {}

  private:
    std::array<uint64_t, 4> _opaque;
    friend struct detail::dispatch_access_t;
};

/// @brief Byte/part/message requirements reported when a receive batch is too small.
struct receive_requirements_t
{
    size_t message_count = 0;
    size_t part_count = 0;
    size_t byte_count = 0;
};

/// @brief One ready-index record: which owner has messages waiting to be claimed.
struct ready_record_t
{
    ready_record_t () noexcept :
        owner_kind (owner_kind_t::node),
        domain (ready_domain_t::none),
        spot_rid (detail_empty_rid ()),
        actor ()
    {
    }

    owner_kind_t owner_kind;
    ready_domain_t domain;
    routing_id_t spot_rid;
    actor_ref_t actor;

  private:
    static routing_id_t detail_empty_rid () noexcept
    {
        return zlink::detail::unchecked_empty_routing_id ();
    }
};

/// @brief One received record decoded from a claim's receive batch.
struct receive_record_t
{
    receive_record_t () noexcept :
        kind (record_kind_t::node_send),
        domain (ready_domain_t::none),
        source_node_rid (zlink::detail::unchecked_empty_routing_id ()),
        source_spot_rid (zlink::detail::unchecked_empty_routing_id ()),
        source_actor (),
        operation_id (),
        operation_kind (operation_kind_t::none),
        reply_token (),
        channel_name (),
        topic (),
        application_metadata (),
        terminal_result (0),
        failure_errno (0),
        part_offset (0),
        part_count (0)
    {
    }

    record_kind_t kind;
    ready_domain_t domain;
    routing_id_t source_node_rid;
    routing_id_t source_spot_rid;
    actor_ref_t source_actor;
    operation_id_t operation_id;
    operation_kind_t operation_kind;
    reply_token_t reply_token;
    std::string channel_name;
    std::string topic;
    std::vector<uint8_t> application_metadata;
    int32_t terminal_result;
    int32_t failure_errno;
    size_t part_offset;
    size_t part_count;
};

class receive_batch_t;

/// @brief A claim over one ready owner's pending messages; drains into a receive batch.
class claim_t
{
  public:
    claim_t () noexcept;
    ~claim_t ();

    claim_t (claim_t &&other_) noexcept;
    claim_t &operator= (claim_t &&other_) noexcept;

    claim_t (const claim_t &) = delete;
    claim_t &operator= (const claim_t &) = delete;

    bool valid () const noexcept { return _valid; }

    /// @brief Drains this claim's messages into @p batch.
    /// @return a recv_result_t code; when the batch is too small the required
    ///         sizes are reported in @p required_out and the call must be retried.
    int recv_batch (receive_batch_t &batch_,
                    receive_requirements_t &required_out_,
                    recv_flags_t flags_ = recv_flags_t::none);

    void release () noexcept;

  private:
    std::array<uint64_t, 4> _opaque;
    bool _valid;
    friend struct detail::dispatch_access_t;
};

/// @brief A reusable ready-index batch filled by mesh_node_t::drain_ready().
class ready_batch_t
{
  public:
    explicit ready_batch_t (size_t record_capacity_);
    ~ready_batch_t ();

    ready_batch_t (ready_batch_t &&other_) noexcept;
    ready_batch_t &operator= (ready_batch_t &&other_) noexcept;

    ready_batch_t (const ready_batch_t &) = delete;
    ready_batch_t &operator= (const ready_batch_t &) = delete;

    bool valid () const noexcept { return _handle != nullptr; }

    void reset ();
    size_t count () const noexcept;
    ready_record_t at (size_t index_) const;

    /// @brief Takes an exclusive claim on the owner referenced by record @p index.
    claim_t take_claim (size_t index_);

  private:
    void *_handle;
    friend class mesh_node_t;
    friend struct detail::dispatch_access_t;
};

/// @brief A reusable receive batch filled by claim_t::recv_batch().
class receive_batch_t
{
  public:
    receive_batch_t (size_t message_capacity_, size_t part_capacity_, size_t byte_capacity_);
    ~receive_batch_t ();

    receive_batch_t (receive_batch_t &&other_) noexcept;
    receive_batch_t &operator= (receive_batch_t &&other_) noexcept;

    receive_batch_t (const receive_batch_t &) = delete;
    receive_batch_t &operator= (const receive_batch_t &) = delete;

    bool valid () const noexcept { return _handle != nullptr; }

    void reset ();
    size_t count () const noexcept;
    receive_record_t at (size_t index_) const;

    /// @brief Retains a copy of record @p index's message parts as owned messages.
    std::vector<message_t> retain_message (size_t index_) const;

  private:
    void *_handle;
    friend class claim_t;
    friend struct detail::dispatch_access_t;
};

/// @brief Replies to a request received via dispatch, using its reply token.
submit_result_t reply (const reply_token_t &token_,
                       std::vector<message_t> &parts_,
                       send_flags_t flags_ = send_flags_t::none);

} // namespace service
} // namespace zlink
