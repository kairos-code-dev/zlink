#include "../common/perf_common.hpp"
#include "../common/perf_common_multi.hpp"
#include "../common/perf_multi_handshake.hpp"
#include "../common/perf_multi_metric_header.hpp"

#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

#ifndef PERF_SPOT_PATTERN_NAME
#define PERF_SPOT_PATTERN_NAME "MULTI_SPOT_PUBSUB"
#endif

namespace
{

const char *const k_pattern = PERF_SPOT_PATTERN_NAME;
const char *const k_mesh_name = "perf-multi-spot";
const char *const k_hub_channel = "perf-hub";
const char *const k_topic = "bench";
const char *const k_hello = "SPOT_PERF_HELLO";

enum pattern_kind_t
{
    pattern_pubsub,
    pattern_reqrep,
    pattern_sendsend
};

pattern_kind_t pattern_kind ()
{
    const std::string name (k_pattern);
    if (name.find ("REQREP") != std::string::npos)
        return pattern_reqrep;
    if (name.find ("SENDSEND") != std::string::npos)
        return pattern_sendsend;
    return pattern_pubsub;
}

std::string mesh_bind_endpoint (const std::string &transport)
{
    const char *host = (transport == "tls" || transport == "wss") ? "localhost" : "127.0.0.1";
    return transport + "://" + host + ":0";
}

struct command_state_t
{
    command_state_t () : stop (false), pending_size (0) {}

    std::atomic<bool> stop;
    std::atomic<size_t> pending_size;
};

void read_commands (command_state_t *state)
{
    std::string line;
    while (state && std::getline (std::cin, line)) {
        if (line == "STOP" || line == "QUIT") {
            state->stop.store (true, std::memory_order_release);
            return;
        }
        size_t size = 0;
        if (perf_multi_handshake::parse_size_command_line (line, "START,", &size))
            state->pending_size.store (size, std::memory_order_release);
    }
    if (state)
        state->stop.store (true, std::memory_order_release);
}

bool payload_is_hello (const zlink_mesh_receive_record_t &record, const zlink_msg_t *parts)
{
    if (record.part_count != 1 || !parts)
        return false;
    const zlink_msg_t *part = &parts[record.part_offset];
    return zlink_msg_size (part) == std::strlen (k_hello)
           && std::memcmp (zlink_msg_data (const_cast<zlink_msg_t *> (part)), k_hello,
                           std::strlen (k_hello))
                == 0;
}

bool retryable_backpressure (zlink_submit_result_t result)
{
    const int err = zlink_errno ();
    return result == ZLINK_SUBMIT_BACKPRESSURED
           && (err == EAGAIN || err == ETIMEDOUT
               || err == EINTR);
}

bool reply_generation (const zlink_mesh_reply_token_t &token,
                       uint64_t generation,
                       const command_state_t *commands)
{
    zlink_msg_t reply;
    if (zlink_msg_init_size (&reply, sizeof (generation)) != 0)
        return false;
    std::memcpy (zlink_msg_data (&reply), &generation, sizeof (generation));
    zlink_submit_result_t rc = ZLINK_SUBMIT_INTERNAL_ERROR;
    for (;;) {
        rc = zlink_mesh_reply (
          &token, &reply, 1, ZLINK_SEND_FLAGS_DONTWAIT);
        if (rc == ZLINK_SUBMIT_OK
            || !retryable_backpressure (rc)
            || (commands
                && commands->stop.load (
                  std::memory_order_acquire)))
            break;
        std::this_thread::yield ();
    }
    zlink_msg_close (&reply);
    return rc == ZLINK_SUBMIT_OK
           || (commands
               && commands->stop.load (
                 std::memory_order_acquire));
}

bool echo_request (const zlink_mesh_receive_record_t &record,
                   const zlink_msg_t *parts,
                   const command_state_t *commands)
{
    if (record.part_count == 0)
        return false;
    for (;;) {
        const zlink_submit_result_t rc =
          zlink_mesh_reply (
            &record.reply_token, &parts[record.part_offset],
            record.part_count, ZLINK_SEND_FLAGS_DONTWAIT);
        if (rc == ZLINK_SUBMIT_OK)
            return true;
        if (commands
            && commands->stop.load (
              std::memory_order_acquire))
            return true;
        if (!retryable_backpressure (rc))
            return false;
        std::this_thread::yield ();
    }
}

bool echo_send (void *spot,
                const zlink_mesh_receive_record_t &record,
                const zlink_msg_t *parts,
                const command_state_t *commands)
{
    if (!spot || record.part_count != 1)
        return false;
    const zlink_msg_t *part = &parts[record.part_offset];
    if (zlink_msg_size (part) < perf_multi_metric::header_size () + sizeof (uint64_t))
        return false;

    uint64_t source_generation = 0;
    const unsigned char *data =
      static_cast<const unsigned char *> (zlink_msg_data (const_cast<zlink_msg_t *> (part)));
    std::memcpy (&source_generation, data + perf_multi_metric::header_size (),
                 sizeof (source_generation));
    if (source_generation == 0)
        return false;

    for (;;) {
        const zlink_submit_result_t rc =
          zlink_spot_send_to_spot (
            spot, &record.source_node_rid,
            &record.source_spot_rid, source_generation,
            NULL, part, 1, ZLINK_SEND_FLAGS_DONTWAIT);
        if (rc == ZLINK_SUBMIT_OK)
            return true;
        if (commands
            && commands->stop.load (
              std::memory_order_acquire))
            return true;
        if (!retryable_backpressure (rc))
            return false;
        std::this_thread::yield ();
    }
}

bool drain_server (void *node,
                   void *spot,
                   void *ready,
                   void *batch,
                   uint64_t generation,
                   const command_state_t *commands)
{
    uint32_t residue = 0;
    const zlink_recv_result_t ready_rc = zlink_mesh_node_drain_ready (
      node, ZLINK_MESH_READY_ALL, ready, &residue, ZLINK_RECV_FLAGS_DONTWAIT);
    if (ready_rc == ZLINK_RECV_NO_DATA)
        return true;
    if (ready_rc != ZLINK_RECV_OK)
        return false;

    const size_t ready_count = zlink_mesh_ready_batch_count (ready);
    for (size_t i = 0; i < ready_count; ++i) {
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
        const zlink_mesh_receive_record_t *records = zlink_mesh_receive_batch_data (batch);
        const zlink_msg_t *parts = zlink_mesh_receive_batch_parts (batch);
        for (size_t r = 0; r < count; ++r) {
            bool ok = true;
            if (records[r].kind == ZLINK_MESH_RECORD_NODE_REQUEST
                && payload_is_hello (records[r], parts)) {
                ok = reply_generation (
                  records[r].reply_token, generation, commands);
            } else if (pattern_kind () == pattern_reqrep
                       && records[r].kind == ZLINK_MESH_RECORD_SPOT_REQUEST) {
                ok = echo_request (
                  records[r], parts, commands);
            } else if (pattern_kind () == pattern_sendsend
                       && records[r].kind == ZLINK_MESH_RECORD_SPOT_SEND) {
                ok = echo_send (
                  spot, records[r], parts, commands);
            }
            if (!ok) {
                zlink_mesh_receive_batch_reset (batch);
                zlink_mesh_claim_release (&claim);
                return false;
            }
        }

        zlink_mesh_receive_batch_reset (batch);
        zlink_mesh_claim_release (&claim);
    }
    zlink_mesh_ready_batch_reset (ready);
    return true;
}

bool publish_payload (
  void *spot,
  size_t msg_size,
  uint32_t run_id,
  uint64_t sequence,
  perf_multi_metric::phase_t phase,
  const command_state_t *commands)
{
    const size_t payload_size = std::max<size_t> (msg_size, perf_multi_metric::header_size ());
    zlink_msg_t part;
    if (zlink_msg_init_size (&part, payload_size) != 0)
        return false;
    std::memset (zlink_msg_data (&part), 's', payload_size);
    if (!perf_multi_metric::stamp_payload (zlink_msg_data (&part), payload_size, run_id, phase,
                                           msg_size, sequence, perf_multi_metric::now_ns ())) {
        zlink_msg_close (&part);
        return false;
    }

    zlink_submit_result_t rc = ZLINK_SUBMIT_INTERNAL_ERROR;
    for (;;) {
        zlink_mesh_publish_detail_t detail;
        std::memset (&detail, 0, sizeof (detail));
        detail.struct_size = sizeof (detail);
        detail.version = 1;
        rc = zlink_spot_publish (
          spot, k_hub_channel, k_topic, NULL, &part, 1,
          &detail, ZLINK_SEND_FLAGS_DONTWAIT);
        const uint32_t admitted_targets =
          detail.admitted_remote_target_count + detail.admitted_local_spot_count;
        if (rc == ZLINK_SUBMIT_BACKPRESSURED && admitted_targets > 0) {
            //  Logical Multicast admits targets independently. Retrying the
            //  same sequence after partial admission would duplicate it at
            //  targets that already accepted the message.
            rc = ZLINK_SUBMIT_OK;
            break;
        }
        if (rc == ZLINK_SUBMIT_OK
            || !retryable_backpressure (rc)
            || (commands
                && commands->stop.load (
                  std::memory_order_acquire)))
            break;
        std::this_thread::yield ();
    }
    zlink_msg_close (&part);
    if (rc == ZLINK_SUBMIT_OK)
        return true;
    if (commands
        && commands->stop.load (
          std::memory_order_acquire))
        return true;
    const int err = zlink_errno ();
    if (bench_debug_enabled ())
        std::cerr << "[multi-spot-server] publish failed rc=" << static_cast<int> (rc)
                  << " errno=" << err << std::endl;
    return false;
}

int run_server (const std::string &lib_name, const std::string &transport)
{
    if (!perf_supports_service_transport (transport) || transport == "inproc"
        || transport == "ipc") {
        std::cout << "UNSUPPORTED," << lib_name << "," << k_pattern << "," << transport
                  << std::endl;
        return 0;
    }

    set_perf_multi_pattern_env (k_pattern);
    const multi_bench_settings_t settings = resolve_multi_bench_settings ();
    ctx_guard_t ctx;
    if (!ctx.valid ())
        return 1;

    zlink_mesh_node_options_t options;
    std::memset (&options, 0, sizeof (options));
    options.struct_size = sizeof (options);
    options.version = 1;
    options.mesh_name = k_mesh_name;
    options.mesh_name_size = std::strlen (k_mesh_name);
    void *node = zlink_mesh_node_new (ctx.get (), &options);
    if (!node || !setup_tls_server (node, transport) || !setup_tls_client (node, transport)
        || zlink_set_routing_id (node, "hub", 3) != ZLINK_CONFIG_OK
        || zlink_mesh_node_set_bind (node, mesh_bind_endpoint (transport).c_str ())
             != ZLINK_CONFIG_OK
        || zlink_mesh_node_add_channel_name (node, k_hub_channel) != ZLINK_CONFIG_OK
        || zlink_mesh_node_start (node) != ZLINK_CONFIG_OK) {
        if (node)
            zlink_mesh_node_destroy (&node);
        return 1;
    }

    void *spot = NULL;
    if (zlink_mesh_node_entry_spot (node, &spot) != ZLINK_CONFIG_OK) {
        zlink_mesh_node_shutdown (node, 1000);
        zlink_mesh_node_destroy (&node);
        return 1;
    }
    zlink_spot_status_t spot_status;
    std::memset (&spot_status, 0, sizeof (spot_status));
    spot_status.struct_size = sizeof (spot_status);
    spot_status.version = 1;
    if (zlink_spot_status (spot, &spot_status) != ZLINK_CONFIG_OK) {
        zlink_spot_destroy (&spot);
        zlink_mesh_node_shutdown (node, 1000);
        zlink_mesh_node_destroy (&node);
        return 1;
    }
    zlink_mesh_node_status_t status;
    std::memset (&status, 0, sizeof (status));
    status.struct_size = sizeof (status);
    status.version = 1;
    if (zlink_mesh_node_status (node, &status) != ZLINK_CONFIG_OK) {
        zlink_spot_destroy (&spot);
        zlink_mesh_node_shutdown (node, 1000);
        zlink_mesh_node_destroy (&node);
        return 1;
    }
    std::cout << "READY," << status.local_endpoint << std::endl;

    void *ready = zlink_mesh_ready_batch_new (256);
    const size_t max_size = perf_current_benchmark_max_msg_size (64);
    void *batch = zlink_mesh_receive_batch_new (64, 64, max_size * 64);
    if (!ready || !batch) {
        zlink_mesh_ready_batch_destroy (&ready);
        zlink_mesh_receive_batch_destroy (&batch);
        zlink_spot_destroy (&spot);
        zlink_mesh_node_shutdown (node, 1000);
        zlink_mesh_node_destroy (&node);
        return 1;
    }

    command_state_t commands;
    std::thread command_thread (read_commands, &commands);
    size_t active_size = 0;
    size_t last_size = 0;
    uint32_t run_id = 0;
    uint64_t sequence = 1;
    std::chrono::steady_clock::time_point deadline;
    bool ok = true;
    while (!commands.stop.load (std::memory_order_acquire)) {
        if (!drain_server (
              node, spot, ready, batch,
              spot_status.lifecycle_generation, &commands)) {
            if (bench_debug_enabled ())
                std::cerr << "[multi-spot-server] receive drain failed errno=" << zlink_errno ()
                          << std::endl;
            ok = false;
            break;
        }

        const size_t pending = commands.pending_size.exchange (0, std::memory_order_acq_rel);
        if (pending != 0) {
            active_size = pending;
            last_size = pending;
            ++run_id;
            sequence = 1;
            deadline = std::chrono::steady_clock::now ()
                       + std::chrono::seconds (std::max (1, settings.duration_seconds));
            if (!apply_benchmark_context_auto_hwm_msg_unit (ctx.get (), active_size)) {
                if (bench_debug_enabled ())
                    std::cerr << "[multi-spot-server] message unit update failed errno="
                              << zlink_errno () << std::endl;
                ok = false;
                break;
            }
        }

        if (pattern_kind () == pattern_pubsub && active_size != 0) {
            if (std::chrono::steady_clock::now () < deadline) {
                if (!publish_payload (
                      spot, active_size, run_id, sequence++,
                      perf_multi_metric::phase_active,
                      &commands)) {
                    ok = false;
                    break;
                }
            } else {
                for (int i = 0; i < 8; ++i) {
                    if (!publish_payload (
                          spot, active_size, run_id, sequence++,
                          perf_multi_metric::phase_cooldown,
                          &commands)) {
                        ok = false;
                        break;
                    }
                }
                active_size = 0;
            }
        } else {
            std::this_thread::yield ();
        }
    }

    commands.stop.store (true, std::memory_order_release);
    if (!ok) {
        std::cerr.flush ();
        std::cout.flush ();
        std::_Exit (1);
    }
    if (command_thread.joinable ())
        command_thread.join ();
    zlink_mesh_node_status_t final_status;
    std::memset (&final_status, 0, sizeof (final_status));
    final_status.struct_size = sizeof (final_status);
    final_status.version = 1;
    if (zlink_mesh_node_status (node, &final_status) == ZLINK_CONFIG_OK) {
        std::cout << "SPOT_DIAG," << lib_name << "," << k_pattern << ","
                  << transport << "," << last_size
                  << ",role=hub"
                  << ",pending_application_messages="
                  << final_status.pending_application_messages
                  << ",pending_infrastructure_messages="
                  << final_status.pending_infrastructure_messages
                  << ",pending_bytes=" << final_status.pending_bytes
                  << ",multicast_submitted="
                  << final_status.multicast_submitted
                  << ",multicast_dropped_targets="
                  << final_status.multicast_dropped_targets << std::endl;
    }
    zlink_mesh_ready_batch_destroy (&ready);
    zlink_mesh_receive_batch_destroy (&batch);
    zlink_spot_destroy (&spot);
    zlink_mesh_node_shutdown (node, 3000);
    zlink_mesh_node_destroy (&node);
    return ok ? 0 : 1;
}

} // namespace

int main (int argc, char **argv)
{
    if (argc < 3)
        return 1;
    return run_server (argv[1], argv[2]);
}
