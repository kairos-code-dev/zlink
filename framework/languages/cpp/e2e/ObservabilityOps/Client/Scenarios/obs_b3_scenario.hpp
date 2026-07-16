/* SPDX-License-Identifier: MPL-2.0 */
#pragma once
#include "../Support/scenario_context.hpp"

namespace zlink::framework::e2e::observability_ops::client::scenarios
{
inline void run_obs_b3_scenario (const verification_input_t &input)
{
    const auto before_publisher = read_json (input, "beforePublisherEvidence");
    const auto before_subscriber = read_json (input, "beforeSubscriberEvidence");
    const auto publisher = read_json (input, "publisherEvidence");
    const auto subscriber = read_json (input, "subscriberEvidence");
    const auto published = metrics_named (publisher, "zlink.fanout.published");
    require (!published.empty (), "OBS-B3 fanout instruments are missing");
    for (const auto &metric : published) {
        require (metric.at ("tags").value ("topic", "") == "obs.projection",
                 "OBS-B3 publisher topic label is not closed");
    }
    const auto published_delta = metric_total (publisher, "zlink.fanout.published")
                                 - metric_total (before_publisher, "zlink.fanout.published");
    const auto received_delta = metric_total (publisher, "zlink.fanout.received")
                                + metric_total (subscriber, "zlink.fanout.received")
                                - metric_total (before_publisher, "zlink.fanout.received")
                                - metric_total (before_subscriber, "zlink.fanout.received");
    require (published_delta == 1 && received_delta == 2,
             "OBS-B3 fanout delta is not the required 1:2");
    require (metrics_named (publisher, "zlink.fanout.dropped").empty ()
               && metrics_named (subscriber, "zlink.fanout.dropped").empty (),
             "OBS-B3 unobservable backend exposed fanout.dropped");
    require_bounded_metric_labels (publisher,
                                   "OBS-B3 publisher metric has a forbidden label");
    require_bounded_metric_labels (subscriber,
                                   "OBS-B3 subscriber metric has a forbidden label");
    const auto lateness = metrics_named (publisher, "zlink.location.owner_lease.renew.lateness");
    require (std::any_of (lateness.begin (), lateness.end (), [] (const auto &metric) {
                 return metric.at ("value").template get<double> () >= 0.5;
             }),
             "OBS-B3 external Redis delay did not produce lease renewal lateness");
}
} // namespace zlink::framework::e2e::observability_ops::client::scenarios
