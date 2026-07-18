/* SPDX-License-Identifier: MPL-2.0 */

#include <zlink/Contracts/Messaging/received.hpp>
#include <zlink/Contracts/Messaging/topic_message.hpp>
#include <zlink/Contracts/Messaging/operation_contracts.hpp>
#include <zlink/Contracts/Errors/errors.hpp>

#include <Runtime/Service/spot_state.hpp>

namespace zlink
{
namespace
{

inline recv_error_t invalid_single_part_error ()
{
    return recv_error_t (recv_result_t::not_supported, EMSGSIZE);
}

void close_parts (std::vector<message_t> &parts_)
{
    for (std::vector<message_t>::iterator it = parts_.begin (); it != parts_.end (); ++it)
        it->close ();
    parts_.clear ();
}

} // namespace

received_t::received_t (std::optional<routing_id_t> routing_id_,
                        std::optional<routing_id_t> spot_rid_,
                        std::optional<uint64_t> request_seq_,
                        std::vector<message_t> parts_) :
    _routing_id (std::move (routing_id_)),
    _spot_rid (std::move (spot_rid_)),
    _request_seq (std::move (request_seq_)),
    _parts (std::move (parts_))
{
}

received_t::received_t (std::optional<routing_id_t> routing_id_,
                        std::optional<routing_id_t> spot_rid_,
                        std::optional<uint64_t> request_seq_,
                        message_t part_) :
    _routing_id (std::move (routing_id_)),
    _spot_rid (std::move (spot_rid_)),
    _request_seq (std::move (request_seq_)),
    _parts (std::move (part_))
{
}

message_t &detail::lazy_message_parts_t::first_part ()
{
    if (!is_single_part ())
        throw invalid_single_part_error ();
    if (_single_part.has_value ())
        return *_single_part;
    return _parts.front ();
}

void detail::lazy_message_parts_t::materialize_parts () const
{
    if (!_single_part.has_value ())
        return;
    message_t part = std::move (*_single_part);
    _single_part.reset ();
    _parts.push_back (std::move (part));
}

const std::vector<message_t> &detail::lazy_message_parts_t::parts () const
{
    materialize_parts ();
    return _parts;
}

std::vector<message_t> &detail::lazy_message_parts_t::parts ()
{
    materialize_parts ();
    return _parts;
}

message_t detail::lazy_message_parts_t::single_part_or_throw ()
{
    if (!is_single_part ())
        throw invalid_single_part_error ();
    if (_single_part.has_value ())
        return *_single_part;
    return _parts.front ();
}

void detail::lazy_message_parts_t::close ()
{
    if (_single_part.has_value ())
        _single_part->close ();
    _single_part.reset ();
    close_parts (_parts);
}

message_t &received_t::first_part ()
{
    return _parts.first_part ();
}

const std::vector<message_t> &topic_message_t::parts () const
{
    return _parts.parts ();
}

std::vector<message_t> &topic_message_t::parts ()
{
    return _parts.parts ();
}

message_t received_t::single_part_or_throw ()
{
    return _parts.single_part_or_throw ();
}

void received_t::close ()
{
    _parts.close ();
}

void topic_message_t::close ()
{
    _parts.close ();
}

const std::vector<message_t> &received_t::parts () const
{
    return _parts.parts ();
}

std::vector<message_t> &received_t::parts ()
{
    return _parts.parts ();
}

message_t topic_message_t::single_part_or_throw ()
{
    return _parts.single_part_or_throw ();
}

message_t &topic_message_t::first_part ()
{
    return _parts.first_part ();
}

// Send/reply bound to this received message's encapsulated routing context.
// The supporting builder machinery (received_send/received_reply operation
// kinds, submit_received_*_parts) survives the 10.0.0 transition for the raw
// socket layer; only these two entry points were dropped and are restored here.
service::send_operation_t received_t::send ()
{
    auto state_ptr = service::detail::acquire_state ();
    state_ptr->kind = service::detail::spot_operation_kind_t::received_send;
    state_ptr->received.received = this;
    return service::send_operation_t (std::move (state_ptr));
}

service::reply_operation_t received_t::reply ()
{
    auto state_ptr = service::detail::acquire_state ();
    state_ptr->kind = service::detail::spot_operation_kind_t::received_reply;
    state_ptr->received.received = this;
    return service::reply_operation_t (std::move (state_ptr));
}

} // namespace zlink
