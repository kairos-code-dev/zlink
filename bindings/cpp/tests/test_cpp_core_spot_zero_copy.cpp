/* SPDX-License-Identifier: MPL-2.0 */

#include "test_helpers.hpp"

#include <atomic>
#include <cstdlib>
#include <cstring>

namespace {

void counting_free_fn (void *data_, void *hint_) noexcept
{
    std::atomic<int> *count = static_cast<std::atomic<int> *> (hint_);
    if (count)
        count->fetch_add (1, std::memory_order_relaxed);
    std::free (data_);
}

char *alloc_payload (const char *text_, size_t len_)
{
    char *buf = static_cast<char *> (std::malloc (len_));
    assert (buf != NULL);
    if (len_ > 0)
        std::memcpy (buf, text_, len_);
    return buf;
}

bool wait_until_equal (const std::atomic<int> &value_,
                       int expected_,
                       int timeout_ms_)
{
    const auto deadline =
      std::chrono::steady_clock::now () + std::chrono::milliseconds (timeout_ms_);
    while (std::chrono::steady_clock::now () < deadline) {
        if (value_.load (std::memory_order_relaxed) == expected_)
            return true;
        sleep_ms (5);
    }
    return value_.load (std::memory_order_relaxed) == expected_;
}

bool recv_spot_with_timeout (zlink::service::spot_t &spot_,
                             std::vector<zlink::message_t> &parts_,
                             std::string &topic_,
                             std::string &service_name_,
                             int timeout_ms_)
{
    const auto deadline =
      std::chrono::steady_clock::now () + std::chrono::milliseconds (timeout_ms_);
    while (std::chrono::steady_clock::now () < deadline) {
        try {
            const zlink::topic_message_t message =
              spot_.subscribe (zlink::recv_flags_t::dontwait);
            parts_ = message.parts ();
            topic_ = message.topic ();
            service_name_ =
              message.service_name () ? *message.service_name () : std::string ();
            return true;
        }
        catch (const zlink::recv_error_t &err) {
            if (err.result () != zlink::recv_result_t::no_data
                && err.result () != zlink::recv_result_t::busy)
                return false;
        }
        sleep_ms (5);
    }
    return false;
}

void test_spot_publish_external_buffer_delivery ()
{
    std::atomic<int> free_count (0);
    zlink::context_t ctx;
    zlink::service::spot_node_t pub_node (ctx);
    zlink::service::spot_node_t sub_node (ctx);
    const std::string endpoint =
      unique_inproc ("inproc://cpp-", "spot-zero-copy");

    assert (pub_node.bind (endpoint.c_str ()) == 0);
    assert (sub_node.connect_peer (endpoint.c_str ()) == 0);

    const std::string service_name = "zero-copy";
    zlink::service::discovery_t pub_discovery (
      ctx, zlink::service_type::spot, service_name);
    assert (pub_discovery.valid ());
    pub_node.attach_discovery (pub_discovery);

    zlink::service::spot_t pub_spot = pub_node.create_spot ();
    zlink::service::spot_t sub_spot = sub_node.create_spot ();
    assert (pub_spot.valid ());
    assert (sub_spot.valid ());
    assert (sub_spot.set_subscription ("zero:topic") == 0);
    sleep_ms (100);

    char *payload = alloc_payload ("pong", 4);
    zlink::message_t part =
      zlink::message_t::from_external (payload, 4, &counting_free_fn,
                                       &free_count);
    assert (part.valid ());
    pub_spot.publish (service_name, "zero:topic", part);
    assert (!part.valid ());

    std::vector<zlink::message_t> recv_parts;
    std::string topic;
    std::string service_name_received;
    assert (
      recv_spot_with_timeout (sub_spot, recv_parts, topic,
                              service_name_received, 2000));
    assert (service_name_received == service_name);
    assert (topic == "zero:topic");
    assert (recv_parts.size () == 1);
    assert (recv_parts[0].size () == 4);
    assert (std::memcmp (recv_parts[0].data (), "pong", 4) == 0);
    assert (wait_until_equal (free_count, 1, 1000));
}

void test_spot_publish_external_buffer_failure_keeps_buffer_alive ()
{
    std::atomic<int> free_count (0);
    zlink::context_t ctx;
    zlink::service::spot_node_t node (ctx);
    zlink::service::spot_t spot = node.create_spot ();
    assert (spot.valid ());

    char *payload = alloc_payload ("bad", 3);
    {
        zlink::message_t part =
          zlink::message_t::from_external (payload, 3, &counting_free_fn,
                                           &free_count);
        assert (part.valid ());
        bool threw = false;
        try {
            spot.publish (std::string (), "zero:topic", part);
        }
        catch (const zlink::submit_error_t &) {
            threw = true;
        }
        assert (threw);
        assert (part.valid ());
        assert (free_count.load (std::memory_order_relaxed) == 0);
    }

    assert (wait_until_equal (free_count, 1, 1000));
}

void test_spot_publish_external_buffer_rejects_invalid_buffer ()
{
    std::atomic<int> free_count (0);
    zlink::message_t part =
      zlink::message_t::from_external (NULL, 1, &counting_free_fn,
                                       &free_count);
    assert (!part.valid ());
    assert (free_count.load (std::memory_order_relaxed) == 0);
}

} // namespace

int main ()
{
    test_spot_publish_external_buffer_delivery ();
    test_spot_publish_external_buffer_failure_keeps_buffer_alive ();
    test_spot_publish_external_buffer_rejects_invalid_buffer ();
    return 0;
}
