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

void test_gateway_send_zero_consumes_on_failure ()
{
    zlink::context_t ctx;
    zlink::service::discovery_t discovery (ctx, zlink::service_type::gateway);
    zlink::service::gateway_t gateway (ctx, discovery);
    assert (discovery.valid ());
    assert (gateway.valid ());

    std::atomic<int> free_count (0);
    char *payload = alloc_payload ("zero", 4);

    assert (gateway.send_zero ("missing-service",
                               payload,
                               4,
                               &counting_free_fn,
                               &free_count,
                               zlink::send_flag::dontwait)
            == -1);
    assert (free_count.load (std::memory_order_relaxed) == 1);
}

void test_gateway_send_rid_zero_consumes_on_failure ()
{
    zlink::context_t ctx;
    zlink::service::discovery_t discovery (ctx, zlink::service_type::gateway);
    zlink::service::gateway_t gateway (ctx, discovery);
    assert (discovery.valid ());
    assert (gateway.valid ());

    std::atomic<int> free_count (0);
    char *payload = alloc_payload ("rid0", 4);

    assert (gateway.send_rid_zero ("missing-service",
                                   "rid-a",
                                   payload,
                                   4,
                                   &counting_free_fn,
                                   &free_count,
                                   zlink::send_flag::dontwait)
            == -1);
    assert (free_count.load (std::memory_order_relaxed) == 1);
}

void test_gateway_send_zero_rejects_null_buffer ()
{
    zlink::context_t ctx;
    zlink::service::discovery_t discovery (ctx, zlink::service_type::gateway);
    zlink::service::gateway_t gateway (ctx, discovery);
    assert (discovery.valid ());
    assert (gateway.valid ());

    std::atomic<int> free_count (0);
    assert (gateway.send_zero ("missing-service",
                               NULL,
                               1,
                               &counting_free_fn,
                               &free_count,
                               zlink::send_flag::dontwait)
            == -1);
    assert (free_count.load (std::memory_order_relaxed) == 0);
}

} // namespace

int main ()
{
    test_gateway_send_zero_consumes_on_failure ();
    test_gateway_send_rid_zero_consumes_on_failure ();
    test_gateway_send_zero_rejects_null_buffer ();
    return 0;
}
