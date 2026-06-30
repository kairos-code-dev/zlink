/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Configuration/subscriber_options.hpp"

#include <mutex>
#include <vector>

namespace zlink::framework::e2e::pubsub::server::subscriber
{

class evidence_store_t
{
  public:
    evidence_store_t (std::string subscriber_id, std::string accepted_topics, int handler_delay_ms) :
        subscriber_id (std::move (subscriber_id)),
        accepted_topics (std::move (accepted_topics)),
        handler_delay_ms (handler_delay_ms)
    {
    }

    bool accepts_topic (const std::string &topic) const
    {
        return server::env_has_topic (accepted_topics, topic);
    }

    void record_event (std::string topic, std::string value)
    {
        std::lock_guard lock (_mutex);
        events.push_back ({subscriber_id, std::move (topic), std::move (value)});
    }

    void record_ignored_event (std::string topic, std::string value)
    {
        std::lock_guard lock (_mutex);
        ignored_events.push_back ({subscriber_id, std::move (topic), std::move (value)});
    }

    void record_error (const zlink::framework::message_flow_event_t &error)
    {
        if (error.outcome != zlink::framework::message_flow_outcome_t::error) {
            return;
        }
        std::string message;
        if (error.exception) {
            try {
                std::rethrow_exception (error.exception);
            }
            catch (const std::exception &ex) {
                message = ex.what ();
            }
            catch (...) {
                message = "unknown";
            }
        }
        std::lock_guard lock (_mutex);
        errors.push_back ({server::kind_name (error.message_kind),
                           server::reason_name (*error.error_reason),
                           server::action_name (*error.error_action),
                           error.packet_name.value_or (""),
                           error.topic.value_or (""),
                           std::move (message)});
    }

    evidence_snapshot_t snapshot () const
    {
        std::lock_guard lock (_mutex);
        return {.subscriber_id = subscriber_id,
                .events = events,
                .ignored_events = ignored_events,
                .errors = errors};
    }

    std::string subscriber_id;
    std::string accepted_topics;
    int handler_delay_ms = 0;

  private:
    mutable std::mutex _mutex;
    std::vector<evidence_event_t> events;
    std::vector<evidence_event_t> ignored_events;
    std::vector<dispatch_error_evidence_t> errors;
};

} // namespace zlink::framework::e2e::pubsub::server::subscriber
