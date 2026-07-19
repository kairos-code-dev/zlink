#include "../common/bench_common.hpp"
#include "../common/perf_single_latency.hpp"
#include "../common/perf_single_metric_header.hpp"
#include "../common/perf_single_monitor.hpp"

#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>
#include <vector>

namespace
{

const char *const k_pattern = "SPOT_PUBSUB";
const char *const k_mesh_name = "perf-single-spot-pubsub";
const char *const k_channel = "perf";
const char *const k_topic = "bench";

bool initialize_node (void *ctx, void **node_out, void **publisher_out, void **subscriber_out)
{
    if (!ctx || !node_out || !publisher_out || !subscriber_out)
        return false;

    zlink_mesh_node_options_t options;
    std::memset (&options, 0, sizeof (options));
    options.struct_size = sizeof (options);
    options.version = 1;
    options.mesh_name = k_mesh_name;
    options.mesh_name_size = std::strlen (k_mesh_name);

    void *node = zlink_mesh_node_new (ctx, &options);
    if (!node) {
        if (bench_debug_enabled ())
            std::cerr << "[perf-spot-pubsub] mesh node creation failed errno=" << zlink_errno ()
                      << std::endl;
        return false;
    }
    if (zlink_set_routing_id (node, "single", 6) != ZLINK_CONFIG_OK
        || zlink_mesh_node_set_bind (node, "tcp://127.0.0.1:0") != ZLINK_CONFIG_OK
        || zlink_mesh_node_add_channel_name (node, k_channel) != ZLINK_CONFIG_OK
        || zlink_mesh_node_start (node) != ZLINK_CONFIG_OK) {
        if (bench_debug_enabled ())
            std::cerr << "[perf-spot-pubsub] mesh node start failed errno=" << zlink_errno ()
                      << std::endl;
        zlink_mesh_node_destroy (&node);
        return false;
    }

    void *publisher = NULL;
    void *subscriber = zlink_spot_new (node);
    if (!subscriber || zlink_mesh_node_entry_spot (node, &publisher) != ZLINK_CONFIG_OK
        || zlink_spot_set_subscription (subscriber, k_channel, k_topic,
                                        ZLINK_SPOT_SUBSCRIPTION_EXACT)
             != ZLINK_CONFIG_OK) {
        if (bench_debug_enabled ())
            std::cerr << "[perf-spot-pubsub] spot setup failed errno=" << zlink_errno ()
                      << std::endl;
        zlink_spot_destroy (&subscriber);
        zlink_spot_destroy (&publisher);
        zlink_mesh_node_shutdown (node, 1000);
        zlink_mesh_node_destroy (&node);
        return false;
    }

    *node_out = node;
    *publisher_out = publisher;
    *subscriber_out = subscriber;
    return true;
}

void close_node (void **node_p, void **publisher_p, void **subscriber_p)
{
    zlink_spot_destroy (subscriber_p);
    zlink_spot_destroy (publisher_p);
    if (node_p && *node_p) {
        zlink_mesh_node_shutdown (*node_p, 2000);
        zlink_mesh_node_destroy (node_p);
    }
}

bool drain_active_records (void *node,
                           void *ready,
                           void *batch,
                           uint32_t run_id,
                           size_t msg_size,
                           unsigned long long *received_out,
                           latency_stats_builder_t *latency_out)
{
    uint32_t residue = 0;
    const zlink_recv_result_t ready_rc = zlink_mesh_node_drain_ready (
      node, ZLINK_MESH_READY_APPLICATION, ready, &residue, ZLINK_RECV_FLAGS_DONTWAIT);
    if (ready_rc == ZLINK_RECV_NO_DATA)
        return true;
    if (ready_rc != ZLINK_RECV_OK)
        return false;

    const size_t ready_count = zlink_mesh_ready_batch_count (ready);
    const zlink_mesh_ready_record_t *records = zlink_mesh_ready_batch_data (ready);
    for (size_t i = 0; i < ready_count; ++i) {
        if (records[i].owner_kind != ZLINK_MESH_OWNER_SPOT)
            continue;

        zlink_mesh_claim_t claim;
        if (zlink_mesh_ready_batch_take_claim (ready, i, &claim) != ZLINK_CONFIG_OK)
            return false;

        zlink_mesh_receive_requirements_t requirements;
        std::memset (&requirements, 0, sizeof (requirements));
        const zlink_recv_result_t recv_rc =
          zlink_mesh_claim_recv_batch (&claim, batch, &requirements, ZLINK_RECV_FLAGS_NONE);
        if (recv_rc != ZLINK_RECV_OK) {
            zlink_mesh_claim_release (&claim);
            return false;
        }

        const size_t count = zlink_mesh_receive_batch_count (batch);
        const zlink_mesh_receive_record_t *received = zlink_mesh_receive_batch_data (batch);
        const zlink_msg_t *parts = zlink_mesh_receive_batch_parts (batch);
        for (size_t r = 0; r < count; ++r) {
            if (received[r].kind != ZLINK_MESH_RECORD_SPOT_MULTICAST || received[r].part_count != 1)
                continue;
            const zlink_msg_t *part = &parts[received[r].part_offset];
            perf_single_metric::header_t header;
            if (!perf_single_metric::decode_payload_header (
                  zlink_msg_data (const_cast<zlink_msg_t *> (part)), zlink_msg_size (part), &header)
                || header.run_id != run_id
                || header.phase != static_cast<uint8_t> (perf_single_metric::phase_active)
                || header.msg_size != static_cast<uint32_t> (msg_size)) {
                continue;
            }

            ++(*received_out);
            const uint64_t now = perf_single_metric::now_ns ();
            if (header.sent_ts_ns > 0 && now >= static_cast<uint64_t> (header.sent_ts_ns))
                latency_out->add (static_cast<double> (now - header.sent_ts_ns));
        }

        zlink_mesh_receive_batch_reset (batch);
        zlink_mesh_claim_release (&claim);
    }
    zlink_mesh_ready_batch_reset (ready);
    return true;
}

void run_spot_pubsub (const std::string &transport, size_t msg_size, const std::string &lib_name)
{
    if (transport != "tcp" && transport != "tls" && transport != "ws" && transport != "wss") {
        std::cout << "UNSUPPORTED," << lib_name << "," << k_pattern << "," << transport
                  << std::endl;
        return;
    }

    auto fail = [&] () { print_fail_result (lib_name, k_pattern, transport, msg_size); };
    ctx_guard_t ctx;
    if (!ctx.valid ()) {
        if (bench_debug_enabled ())
            std::cerr << "[perf-spot-pubsub] context creation failed errno=" << zlink_errno ()
                      << std::endl;
        fail ();
        return;
    }
    if (!apply_single_auto_hwm_msg_unit (ctx.get (), msg_size)) {
        if (bench_debug_enabled ())
            std::cerr << "[perf-spot-pubsub] context message unit failed errno=" << zlink_errno ()
                      << std::endl;
        fail ();
        return;
    }

    void *node = NULL;
    void *publisher = NULL;
    void *subscriber = NULL;
    if (!initialize_node (ctx.get (), &node, &publisher, &subscriber)) {
        fail ();
        return;
    }

    const size_t payload_size = std::max<size_t> (msg_size, perf_single_metric::header_size ());
    void *ready = zlink_mesh_ready_batch_new (32);
    void *batch = zlink_mesh_receive_batch_new (32, 32, payload_size * 32);
    if (!ready || !batch) {
        if (bench_debug_enabled ())
            std::cerr << "[perf-spot-pubsub] dispatch batch creation failed errno="
                      << zlink_errno () << std::endl;
        zlink_mesh_ready_batch_destroy (&ready);
        zlink_mesh_receive_batch_destroy (&batch);
        close_node (&node, &publisher, &subscriber);
        fail ();
        return;
    }

    const int duration_s = std::max (1, resolve_single_duration_seconds ());
    const uint32_t run_id = next_single_metric_run_id ();
    std::atomic<bool> sending (true);
    std::atomic<bool> send_failed (false);
    std::thread sender ([&] () {
        std::vector<char> payload (payload_size, 's');
        uint64_t sequence = 1;
        while (sending.load (std::memory_order_acquire)) {
            if (!perf_single_metric::stamp_payload (payload.data (), payload.size (), run_id,
                                                    perf_single_metric::phase_active, msg_size,
                                                    sequence++, perf_single_metric::now_ns ())) {
                send_failed.store (true, std::memory_order_release);
                break;
            }

            zlink_msg_t part;
            if (zlink_msg_init_size (&part, payload.size ()) != 0) {
                send_failed.store (true, std::memory_order_release);
                break;
            }
            std::memcpy (zlink_msg_data (&part), payload.data (), payload.size ());
            const zlink_submit_result_t rc = zlink_spot_publish (
              publisher, k_channel, k_topic, NULL, &part, 1, NULL, ZLINK_SEND_FLAGS_DONTWAIT);
            zlink_msg_close (&part);
            if (rc == ZLINK_SUBMIT_OK)
                continue;
            const int err = zlink_errno ();
            if (err == EAGAIN || err == EINTR) {
                std::this_thread::yield ();
                continue;
            }
            if (bench_debug_enabled ())
                std::cerr << "[perf-spot-pubsub] publish failed rc=" << static_cast<int> (rc)
                          << " errno=" << err << std::endl;
            send_failed.store (true, std::memory_order_release);
            break;
        }
    });

    const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now () + std::chrono::seconds (duration_s);
    unsigned long long received = 0;
    latency_stats_builder_t latency;
    bool recv_ok = true;
    while (std::chrono::steady_clock::now () < deadline
           && !send_failed.load (std::memory_order_acquire)) {
        if (!drain_active_records (node, ready, batch, run_id, msg_size, &received, &latency)) {
            recv_ok = false;
            break;
        }
        std::this_thread::yield ();
    }
    sending.store (false, std::memory_order_release);
    sender.join ();

    latency_stats_t stats = latency.snapshot ();
    zlink_mesh_ready_batch_destroy (&ready);
    zlink_mesh_receive_batch_destroy (&batch);
    close_node (&node, &publisher, &subscriber);
    if (!recv_ok || send_failed.load (std::memory_order_acquire) || received == 0) {
        if (bench_debug_enabled ())
            std::cerr << "[perf-spot-pubsub] active phase failed recv_ok=" << (recv_ok ? 1 : 0)
                      << " send_failed=" << (send_failed.load (std::memory_order_acquire) ? 1 : 0)
                      << " received=" << received << " errno=" << zlink_errno () << std::endl;
        fail ();
        return;
    }

    print_result (lib_name, k_pattern, transport, msg_size,
                  static_cast<double> (received) / static_cast<double> (duration_s), stats.mean_ns,
                  stats.p95_ns, stats.p99_ns);
}

} // namespace

int main (int argc, char **argv)
{
    return run_standard_bench_main (argc, argv, k_pattern, run_spot_pubsub);
}
