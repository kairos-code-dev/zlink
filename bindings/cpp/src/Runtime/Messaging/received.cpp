/* SPDX-License-Identifier: MPL-2.0 */

#include <zlink/Contracts/Messaging/received.hpp>
#include <zlink/Contracts/Messaging/topic_message.hpp>
#include <zlink/Contracts/Errors/errors.hpp>

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
    for (std::vector<message_t>::iterator it = parts_.begin ();
         it != parts_.end (); ++it)
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
    _single_part (),
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
    _single_part (std::move (part_)),
    _parts ()
{
}

message_t &received_t::first_part ()
{
    if (!is_single_part ())
        throw invalid_single_part_error ();
    if (_single_part.has_value ())
        return *_single_part;
    return _parts.front ();
}

void received_t::materialize_parts () const
{
    if (!_single_part.has_value ())
        return;
    message_t part = std::move (*_single_part);
    _single_part.reset ();
    _parts.push_back (std::move (part));
}

const std::vector<message_t> &received_t::parts () const
{
    materialize_parts ();
    return _parts;
}

std::vector<message_t> &received_t::parts ()
{
    materialize_parts ();
    return _parts;
}

message_t topic_message_t::single_part_or_throw ()
{
    if (!is_single_part ())
        throw invalid_single_part_error ();
    if (_single_part.has_value ())
        return *_single_part;
    return _parts.front ();
}

message_t &topic_message_t::first_part ()
{
    if (!is_single_part ())
        throw invalid_single_part_error ();
    if (_single_part.has_value ())
        return *_single_part;
    return _parts.front ();
}

void topic_message_t::materialize_parts () const
{
    if (!_single_part.has_value ())
        return;
    message_t part = std::move (*_single_part);
    _single_part.reset ();
    _parts.push_back (std::move (part));
}

const std::vector<message_t> &topic_message_t::parts () const
{
    materialize_parts ();
    return _parts;
}

std::vector<message_t> &topic_message_t::parts ()
{
    materialize_parts ();
    return _parts;
}

message_t received_t::single_part_or_throw ()
{
    if (!is_single_part ())
        throw invalid_single_part_error ();
    if (_single_part.has_value ())
        return *_single_part;
    return _parts.front ();
}

void received_t::close ()
{
    if (_single_part.has_value ()) {
        _single_part->close ();
        _single_part.reset ();
    }
    close_parts (_parts);
}

void topic_message_t::close ()
{
    if (_single_part.has_value ()) {
        _single_part->close ();
        _single_part.reset ();
    }
    close_parts (_parts);
}

} // namespace zlink
