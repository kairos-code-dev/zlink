/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_RUNTIME_NATIVE_SUBSCRIPTION_READER_HPP_INCLUDED
#define ZLINK_CPP_RUNTIME_NATIVE_SUBSCRIPTION_READER_HPP_INCLUDED

#include <Runtime/Core/routing_id_access.hpp>
#include <Runtime/Native/message_access.hpp>

#include <zlink/Contracts/Core/routing_id.hpp>
#include <zlink/Contracts/Messaging/message.hpp>

#include <optional>
#include <string>

namespace zlink
{
namespace detail
{

inline size_t bounded_topic_size (size_t length_, size_t capacity_) noexcept
{
    return length_ < capacity_ ? length_ : capacity_ - 1u;
}

inline std::optional<routing_id_t>
optional_native_routing_id (const zlink_routing_id_t *routing_id_)
{
    return routing_id_ && routing_id_->size > 0
             ? std::optional<routing_id_t> (native_routing_id (*routing_id_))
             : std::nullopt;
}

inline routing_id_t routing_id_or_empty (const zlink_routing_id_t *routing_id_)
{
    return routing_id_ && routing_id_->size > 0
             ? native_routing_id (*routing_id_)
             : unchecked_empty_routing_id ();
}

inline void assign_subscription_part (std::optional<routing_id_t> *source_out_,
                                      std::string &topic_out_,
                                      message_t &part_out_,
                                      bool &has_more_out_,
                                      const zlink_routing_id_t *source_,
                                      const char *topic_,
                                      size_t topic_length_,
                                      size_t topic_capacity_,
                                      zlink_msg_t *part_,
                                      zlink_part_flag_t has_more_)
{
    if (source_out_)
        *source_out_ = optional_native_routing_id (source_);
    topic_out_.assign (topic_,
                       bounded_topic_size (topic_length_, topic_capacity_));
    adopt_native_message (part_out_, part_);
    has_more_out_ = has_more_ != ZLINK_PART_FINAL;
}

} // namespace detail
} // namespace zlink

#endif
