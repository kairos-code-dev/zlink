/* SPDX-License-Identifier: MPL-2.0 */

#include <zlink/framework.hpp>

#include "runtime/channels/channel_runtime.hpp"
#include "runtime/messaging/envelope_codec.hpp"

#include <zlink.hpp>

#include <atomic>
#include <chrono>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace
{

struct event_t
{
    int value{};
};

void add_event_serializer (zlink::framework::serializer_registry_t &serializers)
{
    serializers.add<event_t> (
      [] (const event_t &value) { return zlink::message_t::from (std::to_string (value.value)); },
      [] (const zlink::message_t &message) { return event_t{std::stoi (message.to_string ())}; });
}

std::string unique_tcp_endpoint ()
{
    static std::atomic<unsigned> counter{0};
    std::ostringstream stream;
    stream << "tcp://127.0.0.1:" << 28500u + counter.fetch_add (1, std::memory_order_relaxed);
    return stream.str ();
}

bool contains_sequence_window (const std::vector<int> &values, int first)
{
    for (std::size_t index = 0; index + 2 < values.size (); ++index) {
        if (values[index] == first && values[index + 1] == first + 1
            && values[index + 2] == first + 2) {
            return true;
        }
    }
    return false;
}

} // namespace

int main ()
{
    zlink::framework::serializer_registry_t serializers;
    add_event_serializer (serializers);

    zlink::framework::zlink_builder_t app;
    const auto endpoint = unique_tcp_endpoint ();
    const std::string topic = "profile.changed";
    app.channel ("events").enable_publisher ().bind (endpoint);
    auto bus = app.message_bus ();
    zlink::framework::detail::channel_runtime_t::from (bus).bind_serializers (serializers);

    zlink::context_t context;
    zlink::sub_socket_t subscriber_a (context);
    zlink::sub_socket_t subscriber_b (context);
    zlink::sub_socket_t subscriber_c (context);
    subscriber_a.connect (endpoint);
    subscriber_b.connect (endpoint);
    subscriber_c.connect (endpoint);
    subscriber_a.set_subscription (topic);
    subscriber_b.set_subscription (topic);
    subscriber_c.set_subscription (topic);

    zlink::framework::runtime::messaging::envelope_codec_t envelope_codec;
    auto drain = [&] (zlink::sub_socket_t &subscriber, std::vector<int> &received_values) {
        const auto deadline = std::chrono::steady_clock::now () + std::chrono::milliseconds (100);
        while (std::chrono::steady_clock::now () < deadline) {
            zlink::topic_message_t inbound;
            const int rc = subscriber.subscribe (inbound, zlink::recv_flags_t::dontwait);
            if (rc != static_cast<int> (zlink::recv_result_t::ok)) {
                std::this_thread::sleep_for (std::chrono::milliseconds (10));
                continue;
            }
            const bool has_expected_parts = inbound.topic () == topic && inbound.parts ().size () == 2;
            if (!has_expected_parts) {
                inbound.close ();
                return false;
            }
            auto header = envelope_codec.decode_header (inbound.parts ()[0]);
            const auto body_value = serializers.get<event_t> ().deserialize (inbound.parts ()[1]).value;
            inbound.close ();
            if (!header
                || header.value ().kind
                     != zlink::framework::runtime::messaging::message_kind_t::publish
                || header.value ().message_name != "profile.changed.event"
                || header.value ().topic != topic) {
                return false;
            }
            received_values.push_back (body_value);
        }
        return true;
    };

    std::vector<int> sequence_a;
    std::vector<int> sequence_b;
    std::vector<int> sequence_c;
    bool found_common_sequence = false;
    for (int sequence = 1; sequence <= 12 && !found_common_sequence; ++sequence) {
        const auto publish_result = app.publisher ()
                                      .publish ("events", topic, event_t{sequence})
                                      .packet_name ("profile.changed.event")
                                      .async ()
                                      .result ();
        if (!publish_result) {
            return 1;
        }
        if (!drain (subscriber_a, sequence_a) || !drain (subscriber_b, sequence_b)
            || !drain (subscriber_c, sequence_c)) {
            return 2;
        }
        for (int first = 1; first <= sequence - 2; ++first) {
            if (contains_sequence_window (sequence_a, first)
                && contains_sequence_window (sequence_b, first)
                && contains_sequence_window (sequence_c, first)) {
                found_common_sequence = true;
                break;
            }
        }
    }

    return found_common_sequence ? 0 : 3;
}
