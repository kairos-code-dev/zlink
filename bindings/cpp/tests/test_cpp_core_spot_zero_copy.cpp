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
                             int timeout_ms_)
{
    const auto deadline =
      std::chrono::steady_clock::now () + std::chrono::milliseconds (timeout_ms_);
    while (std::chrono::steady_clock::now () < deadline) {
        if (spot_.recv (parts_, topic_, zlink::recv_flag::dontwait) == 0)
            return true;
        if (zlink_errno () != EAGAIN)
            return false;
        sleep_ms (5);
    }
    return false;
}

void test_spot_publish_zero_local_delivery ()
{
    std::atomic<int> free_count (0);
    {
        zlink::context_t ctx;
        zlink::service::spot_node_t pub_node (ctx);
        zlink::service::spot_node_t sub_node (ctx);
        const std::string endpoint =
          unique_inproc ("inproc://cpp-", "spot-zero-copy");

        assert (pub_node.bind (endpoint.c_str ()) == 0);
        assert (sub_node.connect_peer_pub (endpoint.c_str ()) == 0);

        zlink::service::spot_t pub_spot (pub_node);
        zlink::service::spot_t sub_spot (sub_node);
        assert (pub_spot.valid ());
        assert (sub_spot.valid ());
        assert (sub_spot.subscribe ("zero:topic") == 0);
        sleep_ms (100);

        char *payload = alloc_payload ("pong", 4);
        assert (pub_spot.publish_zero (
                  "zero:topic", payload, 4, &counting_free_fn, &free_count)
                == 0);

        std::vector<zlink::message_t> recv_parts;
        std::string topic;
        assert (recv_spot_with_timeout (sub_spot, recv_parts, topic, 2000));
        assert (topic == "zero:topic");
        assert (recv_parts.size () == 1);
        assert (recv_parts[0].size () == 4);
        assert (std::memcmp (recv_parts[0].data (), "pong", 4) == 0);
    }
    assert (wait_until_equal (free_count, 1, 1000));
}

void test_spot_publish_zero_failure_consumes ()
{
    zlink::context_t ctx;
    zlink::service::spot_node_t node (ctx);
    zlink::service::spot_t spot (node);
    assert (spot.valid ());

    std::atomic<int> free_count (0);
    char *payload = alloc_payload ("bad", 3);
    assert (spot.publish_zero ("", payload, 3, &counting_free_fn, &free_count)
            == -1);
    assert (wait_until_equal (free_count, 1, 1000));
}

void test_spot_publish_zero_rejects_null_buffer ()
{
    zlink::context_t ctx;
    zlink::service::spot_node_t node (ctx);
    zlink::service::spot_t spot (node);
    assert (spot.valid ());

    std::atomic<int> free_count (0);
    assert (spot.publish_zero ("zero:null", NULL, 1, &counting_free_fn, &free_count)
            == -1);
    assert (free_count.load (std::memory_order_relaxed) == 0);
}

} // namespace

int main ()
{
    test_spot_publish_zero_local_delivery ();
    test_spot_publish_zero_failure_consumes ();
    test_spot_publish_zero_rejects_null_buffer ();
    return 0;
}
