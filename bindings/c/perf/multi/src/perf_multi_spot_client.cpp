#include "../common/perf_common.hpp"
#include "../common/perf_common_multi.hpp"
#include "../common/perf_multi_client_helpers.hpp"
#include "../common/perf_multi_handshake.hpp"
#include "../common/perf_multi_metric_header.hpp"
#include "../common/perf_multi_metrics.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#if !defined(_WIN32)
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#ifndef PERF_SPOT_PATTERN_NAME
#define PERF_SPOT_PATTERN_NAME "MULTI_SPOT_PUBSUB"
#endif

#if !defined(_WIN32)

namespace
{

const char *const k_pattern = PERF_SPOT_PATTERN_NAME;
const char *const k_mesh_name = "perf-multi-spot";
const char *const k_hub_channel = "perf-hub";
const char *const k_topic = "bench";
const char *const k_hello = "SPOT_PERF_HELLO";
const size_t k_child_sample_cap = 1024;

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

struct child_command_t
{
    uint32_t stop;
    uint32_t run_id;
    uint64_t msg_size;
    uint32_t duration_seconds;
};

struct child_result_t
{
    int32_t status;
    uint32_t sample_count;
    uint64_t received;
    double latency_sum_ns;
    double samples[k_child_sample_cap];
};

struct child_slot_t
{
    pid_t pid;
    int command_fd;
    int result_fd;
};

bool write_full (int fd, const void *data, size_t size)
{
    const unsigned char *cursor = static_cast<const unsigned char *> (data);
    while (size > 0) {
        const ssize_t written = write (fd, cursor, size);
        if (written < 0) {
            if (errno == EINTR)
                continue;
            return false;
        }
        if (written == 0)
            return false;
        cursor += written;
        size -= static_cast<size_t> (written);
    }
    return true;
}

bool read_full (int fd, void *data, size_t size)
{
    unsigned char *cursor = static_cast<unsigned char *> (data);
    while (size > 0) {
        const ssize_t received = read (fd, cursor, size);
        if (received < 0) {
            if (errno == EINTR)
                continue;
            return false;
        }
        if (received == 0)
            return false;
        cursor += received;
        size -= static_cast<size_t> (received);
    }
    return true;
}

class fixed_reservoir_t
{
  public:
    fixed_reservoir_t () : _seen (0), _rng (0xA341316Cu) {}

    void add (double value)
    {
        ++_seen;
        if (_samples.size () < k_child_sample_cap) {
            _samples.push_back (value);
            return;
        }
        const uint64_t slot = next_random () % _seen;
        if (slot < _samples.size ())
            _samples[static_cast<size_t> (slot)] = value;
    }

    const std::vector<double> &samples () const { return _samples; }

  private:
    uint64_t _seen;
    uint32_t _rng;
    std::vector<double> _samples;

    uint32_t next_random ()
    {
        _rng = (_rng * 1664525u) + 1013904223u;
        return _rng;
    }
};

bool wait_admitted_peer (void *node, zlink_mesh_peer_entry_t *entry_out)
{
    const int timeout_ms =
      std::max (30000, resolve_multi_int_env ("PERF_MULTI_CONNECT_READY_TIMEOUT_MS", 1000, 1));
    for (int waited = 0; waited < timeout_ms; ++waited) {
        zlink_mesh_peer_entry_t entry;
        std::memset (&entry, 0, sizeof (entry));
        entry.struct_size = sizeof (entry);
        entry.version = 1;
        size_t count = 1;
        if (zlink_mesh_node_peers (node, &entry, &count) == ZLINK_CONFIG_OK && count == 1
            && entry.state == ZLINK_MESH_PEER_ADMITTED) {
            if (entry_out)
                *entry_out = entry;
            return true;
        }
        usleep (1000);
    }
    return false;
}

bool take_completion (void *node,
                      void *ready,
                      void *batch,
                      zlink_mesh_owner_kind_t owner,
                      zlink_mesh_record_kind_t expected_kind,
                      zlink_mesh_operation_id_t operation,
                      uint64_t *generation_out)
{
    uint32_t residue = 0;
    const zlink_recv_result_t ready_rc = zlink_mesh_node_drain_ready (
      node, ZLINK_MESH_READY_ALL, ready, &residue, ZLINK_RECV_FLAGS_DONTWAIT);
    if (ready_rc == ZLINK_RECV_NO_DATA)
        return false;
    if (ready_rc != ZLINK_RECV_OK)
        return false;

    bool found = false;
    const size_t ready_count = zlink_mesh_ready_batch_count (ready);
    const zlink_mesh_ready_record_t *ready_records = zlink_mesh_ready_batch_data (ready);
    for (size_t i = 0; i < ready_count; ++i) {
        if (ready_records[i].owner_kind != owner)
            continue;
        zlink_mesh_claim_t claim;
        if (zlink_mesh_ready_batch_take_claim (ready, i, &claim) != ZLINK_CONFIG_OK)
            continue;
        zlink_mesh_receive_requirements_t requirements;
        std::memset (&requirements, 0, sizeof (requirements));
        if (zlink_mesh_claim_recv_batch (&claim, batch, &requirements, ZLINK_RECV_FLAGS_NONE)
            == ZLINK_RECV_OK) {
            const size_t count = zlink_mesh_receive_batch_count (batch);
            const zlink_mesh_receive_record_t *records = zlink_mesh_receive_batch_data (batch);
            const zlink_msg_t *parts = zlink_mesh_receive_batch_parts (batch);
            for (size_t r = 0; r < count; ++r) {
                if (records[r].kind != expected_kind
                    || (expected_kind == ZLINK_MESH_RECORD_COMPLETION
                        && (records[r].operation_id.high != operation.high
                            || records[r].operation_id.low != operation.low)))
                    continue;
                if (generation_out && records[r].part_count == 1) {
                    const zlink_msg_t *part = &parts[records[r].part_offset];
                    if (zlink_msg_size (part) == sizeof (uint64_t))
                        std::memcpy (generation_out,
                                     zlink_msg_data (const_cast<zlink_msg_t *> (part)),
                                     sizeof (uint64_t));
                }
                found = true;
                break;
            }
        }
        zlink_mesh_receive_batch_reset (batch);
        zlink_mesh_claim_release (&claim);
        if (found)
            break;
    }
    zlink_mesh_ready_batch_reset (ready);
    return found;
}

bool discover_hub_generation (
  void *node, const zlink_routing_id_t &hub_rid, void *ready, void *batch, uint64_t *generation_out)
{
    zlink_msg_t hello;
    if (zlink_msg_init_size (&hello, std::strlen (k_hello)) != 0)
        return false;
    std::memcpy (zlink_msg_data (&hello), k_hello, std::strlen (k_hello));
    zlink_mesh_operation_id_t operation;
    std::memset (&operation, 0, sizeof (operation));
    const zlink_submit_result_t rc = zlink_mesh_node_request_to_node (
      node, &hub_rid, NULL, &hello, 1, &operation, ZLINK_SEND_FLAGS_NONE, 10000);
    zlink_msg_close (&hello);
    if (rc != ZLINK_SUBMIT_OK)
        return false;

    const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now () + std::chrono::seconds (30);
    while (std::chrono::steady_clock::now () < deadline) {
        if (take_completion (node, ready, batch, ZLINK_MESH_OWNER_NODE,
                             ZLINK_MESH_RECORD_COMPLETION, operation, generation_out))
            return generation_out && *generation_out != 0;
        std::this_thread::yield ();
    }
    return false;
}

bool init_payload (zlink_msg_t *part,
                   size_t msg_size,
                   uint32_t run_id,
                   uint64_t sequence,
                   uint64_t source_generation)
{
    const size_t payload_size =
      std::max<size_t> (msg_size, perf_multi_metric::header_size () + sizeof (uint64_t));
    if (zlink_msg_init_size (part, payload_size) != 0)
        return false;
    std::memset (zlink_msg_data (part), 'p', payload_size);
    if (!perf_multi_metric::stamp_payload (zlink_msg_data (part), payload_size, run_id,
                                           perf_multi_metric::phase_active, msg_size, sequence,
                                           perf_multi_metric::now_ns ())) {
        zlink_msg_close (part);
        return false;
    }
    unsigned char *data = static_cast<unsigned char *> (zlink_msg_data (part));
    std::memcpy (data + perf_multi_metric::header_size (), &source_generation,
                 sizeof (source_generation));
    return true;
}

bool record_payload (const zlink_msg_t *part,
                     uint32_t run_id,
                     size_t msg_size,
                     child_result_t *result,
                     fixed_reservoir_t *samples,
                     bool *cooldown_out)
{
    perf_multi_metric::header_t header;
    if (!part
        || !perf_multi_metric::decode_payload_header (
          zlink_msg_data (const_cast<zlink_msg_t *> (part)), zlink_msg_size (part), &header)
        || header.run_id != run_id || header.msg_size != static_cast<uint32_t> (msg_size)) {
        return true;
    }
    if (header.phase == static_cast<uint8_t> (perf_multi_metric::phase_cooldown)) {
        if (cooldown_out)
            *cooldown_out = true;
        return true;
    }
    if (header.phase != static_cast<uint8_t> (perf_multi_metric::phase_active))
        return true;

    const uint64_t now = perf_multi_metric::now_ns ();
    if (header.sent_ts_ns <= 0 || now < static_cast<uint64_t> (header.sent_ts_ns))
        return true;
    const double latency = static_cast<double> (now - header.sent_ts_ns);
    ++result->received;
    result->latency_sum_ns += latency;
    samples->add (latency);
    return true;
}

bool drain_data (void *node,
                 void *ready,
                 void *batch,
                 uint32_t run_id,
                 size_t msg_size,
                 zlink_mesh_record_kind_t expected_kind,
                 zlink_mesh_operation_id_t operation,
                 child_result_t *result,
                 fixed_reservoir_t *samples,
                 bool *received_out,
                 bool *cooldown_out)
{
    if (received_out)
        *received_out = false;
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
            continue;
        zlink_mesh_receive_requirements_t requirements;
        std::memset (&requirements, 0, sizeof (requirements));
        if (zlink_mesh_claim_recv_batch (&claim, batch, &requirements, ZLINK_RECV_FLAGS_NONE)
            != ZLINK_RECV_OK) {
            zlink_mesh_claim_release (&claim);
            return false;
        }

        const size_t count = zlink_mesh_receive_batch_count (batch);
        const zlink_mesh_receive_record_t *records = zlink_mesh_receive_batch_data (batch);
        const zlink_msg_t *parts = zlink_mesh_receive_batch_parts (batch);
        for (size_t r = 0; r < count; ++r) {
            if (records[r].kind != expected_kind)
                continue;
            if (expected_kind == ZLINK_MESH_RECORD_COMPLETION
                && (records[r].operation_id.high != operation.high
                    || records[r].operation_id.low != operation.low))
                continue;
            if (records[r].part_count != 1)
                continue;
            if (!record_payload (&parts[records[r].part_offset], run_id, msg_size, result, samples,
                                 cooldown_out)) {
                zlink_mesh_receive_batch_reset (batch);
                zlink_mesh_claim_release (&claim);
                return false;
            }
            if (received_out)
                *received_out = true;
        }

        zlink_mesh_receive_batch_reset (batch);
        zlink_mesh_claim_release (&claim);
    }
    zlink_mesh_ready_batch_reset (ready);
    return true;
}

bool run_child_case (void *ctx,
                     void *node,
                     void *spot,
                     void *ready,
                     void *batch,
                     const zlink_routing_id_t &hub_rid,
                     uint64_t hub_generation,
                     uint64_t source_generation,
                     const child_command_t &command,
                     child_result_t *result)
{
    std::memset (result, 0, sizeof (*result));
    fixed_reservoir_t samples;
    const size_t msg_size = static_cast<size_t> (command.msg_size);
    if (!apply_benchmark_context_auto_hwm_msg_unit (ctx, msg_size))
        return false;
    const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now () + std::chrono::seconds (command.duration_seconds);
    const std::chrono::steady_clock::time_point receive_deadline =
      deadline + std::chrono::seconds (5);
    uint64_t sequence = 1;
    bool waiting = false;
    zlink_mesh_operation_id_t operation;
    std::memset (&operation, 0, sizeof (operation));

    while (std::chrono::steady_clock::now ()
           < (pattern_kind () == pattern_pubsub ? receive_deadline : deadline)) {
        if (pattern_kind () != pattern_pubsub && !waiting
            && std::chrono::steady_clock::now () < deadline) {
            zlink_msg_t part;
            if (!init_payload (&part, msg_size, command.run_id, sequence++, source_generation))
                return false;
            zlink_submit_result_t submit = ZLINK_SUBMIT_INVALID_STATE;
            if (pattern_kind () == pattern_reqrep) {
                std::memset (&operation, 0, sizeof (operation));
                submit =
                  zlink_spot_request_to_spot (spot, &hub_rid, &hub_rid, hub_generation, NULL, &part,
                                              1, &operation, ZLINK_SEND_FLAGS_DONTWAIT, 10000);
            } else {
                submit = zlink_spot_send_to_spot (spot, &hub_rid, &hub_rid, hub_generation, NULL,
                                                  &part, 1, ZLINK_SEND_FLAGS_DONTWAIT);
            }
            zlink_msg_close (&part);
            if (submit == ZLINK_SUBMIT_OK) {
                waiting = true;
            } else {
                const int err = zlink_errno ();
                if (err != EAGAIN && err != EINTR)
                    return false;
            }
        }

        bool received = false;
        bool cooldown = false;
        const zlink_mesh_record_kind_t expected =
          pattern_kind () == pattern_pubsub
            ? ZLINK_MESH_RECORD_SPOT_MULTICAST
            : (pattern_kind () == pattern_reqrep ? ZLINK_MESH_RECORD_COMPLETION
                                                 : ZLINK_MESH_RECORD_SPOT_SEND);
        if (!drain_data (node, ready, batch, command.run_id, msg_size, expected, operation, result,
                         &samples, &received, &cooldown)) {
            return false;
        }
        if (received && pattern_kind () != pattern_pubsub)
            waiting = false;
        if (cooldown && pattern_kind () == pattern_pubsub)
            break;
        std::this_thread::yield ();
    }

    const std::vector<double> &kept = samples.samples ();
    result->sample_count = static_cast<uint32_t> (kept.size ());
    for (size_t i = 0; i < kept.size (); ++i)
        result->samples[i] = kept[i];
    result->status = result->received > 0 ? 0 : 1;
    return result->status == 0;
}

int child_main (int index,
                int command_fd,
                int result_fd,
                const std::string &transport,
                const std::string &endpoint)
{
    int ready_status = 1;
    void *ctx = zlink_ctx_new ();
    const int node_io_threads = resolve_multi_int_env ("PERF_MULTI_SPOT_NODE_IO_THREADS", 1, 1);
    if (!ctx || zlink_ctx_set (ctx, ZLINK_IO_THREADS, node_io_threads) != 0) {
        write_full (result_fd, &ready_status, sizeof (ready_status));
        return 10;
    }

    zlink_mesh_node_options_t options;
    std::memset (&options, 0, sizeof (options));
    options.struct_size = sizeof (options);
    options.version = 1;
    options.mesh_name = k_mesh_name;
    options.mesh_name_size = std::strlen (k_mesh_name);
    void *node = zlink_mesh_node_new (ctx, &options);
    char rid[64];
    char channel[64];
    std::snprintf (rid, sizeof (rid), "peer-%d", index);
    std::snprintf (channel, sizeof (channel), "perf-peer-%d", index);
    const bool configured =
      node && setup_tls_server (node, transport) && setup_tls_client (node, transport)
      && zlink_set_routing_id (node, rid, std::strlen (rid)) == ZLINK_CONFIG_OK
      && zlink_mesh_node_set_bind (node, mesh_bind_endpoint (transport).c_str ()) == ZLINK_CONFIG_OK
      && zlink_mesh_node_add_channel_name (node, channel) == ZLINK_CONFIG_OK
      && (pattern_kind () != pattern_pubsub
          || zlink_mesh_node_add_channel_name (node, k_hub_channel) == ZLINK_CONFIG_OK)
      && zlink_mesh_node_start (node) == ZLINK_CONFIG_OK;
    if (!configured) {
        write_full (result_fd, &ready_status, sizeof (ready_status));
        if (node)
            zlink_mesh_node_destroy (&node);
        zlink_ctx_term (ctx);
        return 11;
    }

    void *spot = NULL;
    if (zlink_mesh_node_entry_spot (node, &spot) != ZLINK_CONFIG_OK
        || (pattern_kind () == pattern_pubsub
            && zlink_spot_set_subscription (spot, k_hub_channel, k_topic,
                                            ZLINK_SPOT_SUBSCRIPTION_EXACT)
                 != ZLINK_CONFIG_OK)) {
        write_full (result_fd, &ready_status, sizeof (ready_status));
        zlink_mesh_node_shutdown (node, 1000);
        zlink_mesh_node_destroy (&node);
        zlink_ctx_term (ctx);
        return 12;
    }
    zlink_spot_status_t spot_status;
    std::memset (&spot_status, 0, sizeof (spot_status));
    spot_status.struct_size = sizeof (spot_status);
    spot_status.version = 1;
    if (zlink_spot_status (spot, &spot_status) != ZLINK_CONFIG_OK) {
        write_full (result_fd, &ready_status, sizeof (ready_status));
        return 13;
    }

    zlink_mesh_peer_connection_options_t peer_options;
    std::memset (&peer_options, 0, sizeof (peer_options));
    peer_options.struct_size = sizeof (peer_options);
    peer_options.version = 1;
    peer_options.endpoint = endpoint.c_str ();
    peer_options.endpoint_size = endpoint.size ();
    uint64_t intent = 0;
    if (zlink_mesh_node_connect_peer (node, &peer_options, &intent) != ZLINK_CONNECT_OK) {
        write_full (result_fd, &ready_status, sizeof (ready_status));
        return 14;
    }
    zlink_mesh_peer_entry_t hub;
    if (!wait_admitted_peer (node, &hub)) {
        write_full (result_fd, &ready_status, sizeof (ready_status));
        return 15;
    }

    const size_t max_size = perf_current_benchmark_max_msg_size (64);
    void *ready = zlink_mesh_ready_batch_new (32);
    void *batch = zlink_mesh_receive_batch_new (8, 8, max_size * 8);
    uint64_t hub_generation = 0;
    if (!ready || !batch
        || !discover_hub_generation (node, hub.routing_id, ready, batch, &hub_generation)) {
        write_full (result_fd, &ready_status, sizeof (ready_status));
        return 16;
    }

    ready_status = 0;
    if (!write_full (result_fd, &ready_status, sizeof (ready_status)))
        return 17;

    for (;;) {
        child_command_t command;
        if (!read_full (command_fd, &command, sizeof (command)))
            break;
        if (command.stop)
            break;

        child_result_t result;
        if (!run_child_case (ctx, node, spot, ready, batch, hub.routing_id, hub_generation,
                             spot_status.lifecycle_generation, command, &result)) {
            result.status = 1;
            if (bench_debug_enabled ())
                std::cerr << "[multi-spot-client] child=" << index << " size=" << command.msg_size
                          << " failed errno=" << zlink_errno () << std::endl;
        }
        if (!write_full (result_fd, &result, sizeof (result)))
            break;
    }

    zlink_mesh_ready_batch_destroy (&ready);
    zlink_mesh_receive_batch_destroy (&batch);
    zlink_spot_destroy (&spot);
    zlink_mesh_node_shutdown (node, 3000);
    zlink_mesh_node_destroy (&node);
    zlink_ctx_term (ctx);
    return 0;
}

struct weighted_latency_sample_t
{
    double value;
    double weight;
};

double weighted_percentile (
  const std::vector<weighted_latency_sample_t> &samples,
  double quantile)
{
    double total_weight = 0.0;
    for (size_t i = 0; i < samples.size (); ++i)
        total_weight += samples[i].weight;
    if (total_weight <= 0.0)
        return 0.0;

    const double target = total_weight * quantile;
    double cumulative = 0.0;
    for (size_t i = 0; i < samples.size (); ++i) {
        cumulative += samples[i].weight;
        if (cumulative >= target)
            return samples[i].value;
    }
    return samples.back ().value;
}

bench_latency_stats_t aggregate_latency (
  uint64_t total_count,
  double total_sum,
  std::vector<weighted_latency_sample_t> *samples)
{
    bench_latency_stats_t stats;
    if (total_count == 0)
        return stats;
    stats.mean_ns = total_sum / static_cast<double> (total_count);
    if (!samples || samples->empty ()) {
        stats.p95_ns = stats.mean_ns;
        stats.p99_ns = stats.mean_ns;
        return stats;
    }
    std::sort (
      samples->begin (), samples->end (),
      [] (const weighted_latency_sample_t &lhs,
          const weighted_latency_sample_t &rhs) {
          return lhs.value < rhs.value;
      });
    stats.p95_ns = weighted_percentile (*samples, 0.95);
    stats.p99_ns = weighted_percentile (*samples, 0.99);
    return stats;
}

int run_client (const std::string &lib_name,
                const std::string &transport,
                const std::string &endpoint,
                size_t fallback_size)
{
    if (!perf_supports_service_transport (transport) || transport == "inproc"
        || transport == "ipc") {
        std::cout << "UNSUPPORTED," << lib_name << "," << k_pattern << "," << transport
                  << std::endl;
        return 0;
    }
    set_perf_multi_pattern_env (k_pattern);
    signal (SIGPIPE, SIG_IGN);
    const multi_bench_settings_t settings = resolve_multi_bench_settings ();
    const std::vector<size_t> sizes = perf_multi_client::resolve_case_msg_sizes (fallback_size);
    std::vector<child_slot_t> children (settings.clients);

    for (size_t i = 0; i < settings.clients; ++i) {
        int commands[2];
        int results[2];
        if (pipe (commands) != 0 || pipe (results) != 0)
            return 1;
        const pid_t pid = fork ();
        if (pid < 0)
            return 1;
        if (pid == 0) {
            close (commands[1]);
            close (results[0]);
            const int rc =
              child_main (static_cast<int> (i), commands[0], results[1], transport, endpoint);
            close (commands[0]);
            close (results[1]);
            std::_Exit (rc);
        }
        close (commands[0]);
        close (results[1]);
        children[i].pid = pid;
        children[i].command_fd = commands[1];
        children[i].result_fd = results[0];
    }

    bool ready_ok = true;
    for (size_t i = 0; i < children.size (); ++i) {
        int ready_status = 1;
        if (!read_full (children[i].result_fd, &ready_status, sizeof (ready_status))
            || ready_status != 0) {
            ready_ok = false;
        }
    }
    if (!ready_ok) {
        for (size_t i = 0; i < children.size (); ++i) {
            close (children[i].command_fd);
            close (children[i].result_fd);
        }
        for (size_t i = 0; i < children.size (); ++i) {
            int status = 0;
            waitpid (children[i].pid, &status, 0);
        }
        return 1;
    }

    bool ok = true;
    for (size_t si = 0; si < sizes.size () && ok; ++si) {
        const size_t msg_size = sizes[si];
        std::cout << "CLIENT_READY," << msg_size << std::endl;
        if (!perf_multi_handshake::wait_for_start_from_stdin (msg_size)) {
            ok = false;
            break;
        }

        child_command_t command;
        std::memset (&command, 0, sizeof (command));
        command.run_id = static_cast<uint32_t> (si + 1);
        command.msg_size = msg_size;
        command.duration_seconds = static_cast<uint32_t> (std::max (1, settings.duration_seconds));
        for (size_t i = 0; i < children.size (); ++i) {
            if (!write_full (children[i].command_fd, &command, sizeof (command))) {
                ok = false;
                break;
            }
        }

        uint64_t total_received = 0;
        double total_latency = 0.0;
        std::vector<weighted_latency_sample_t> samples;
        samples.reserve (children.size () * k_child_sample_cap);
        for (size_t i = 0; i < children.size () && ok; ++i) {
            child_result_t result;
            std::memset (&result, 0, sizeof (result));
            if (!read_full (children[i].result_fd, &result, sizeof (result))
                || result.status != 0) {
                if (bench_debug_enabled ())
                    std::cerr << "[multi-spot-client] child result failed index=" << i
                              << " status=" << result.status << std::endl;
                ok = false;
                break;
            }
            total_received += result.received;
            total_latency += result.latency_sum_ns;
            if (result.sample_count > 0) {
                //  Each child reservoir is uniform over that child's complete
                //  successful stream. Weight its representatives by the
                //  child's exact count so fast and slow peers contribute in
                //  proportion to their observed records.
                const double sample_weight =
                  static_cast<double> (result.received)
                  / static_cast<double> (result.sample_count);
                for (size_t sample = 0;
                     sample < result.sample_count; ++sample) {
                    weighted_latency_sample_t weighted;
                    weighted.value = result.samples[sample];
                    weighted.weight = sample_weight;
                    samples.push_back (weighted);
                }
            }
        }
        if (!ok || total_received == 0)
            break;

        const bench_latency_stats_t latency =
          aggregate_latency (total_received, total_latency, &samples);
        print_result (lib_name, k_pattern, transport, msg_size,
                      static_cast<double> (total_received)
                        / static_cast<double> (std::max (1, settings.duration_seconds)),
                      latency.mean_ns, latency.p95_ns, latency.p99_ns);
        std::cout << "CLIENT_DONE," << msg_size << std::endl;
    }

    child_command_t stop;
    std::memset (&stop, 0, sizeof (stop));
    stop.stop = 1;
    for (size_t i = 0; i < children.size (); ++i) {
        write_full (children[i].command_fd, &stop, sizeof (stop));
        close (children[i].command_fd);
        close (children[i].result_fd);
    }
    for (size_t i = 0; i < children.size (); ++i) {
        int status = 0;
        if (waitpid (children[i].pid, &status, 0) < 0 || !WIFEXITED (status)
            || WEXITSTATUS (status) != 0)
            ok = false;
    }
    return ok ? 0 : 1;
}

} // namespace

int main (int argc, char **argv)
{
    if (argc < 4)
        return 1;
    const std::string lib_name = argv[1];
    const std::string transport = argv[2];
    const size_t fallback_size = static_cast<size_t> (std::strtoull (argv[3], NULL, 10));
    std::string endpoint;
    if (!perf_multi_client::parse_endpoint_arg (argc, argv, &endpoint))
        return 1;
    return run_client (lib_name, transport, endpoint, fallback_size);
}

#else

int main (int argc, char **argv)
{
    if (argc < 3)
        return 1;
    std::cout << "UNSUPPORTED," << argv[1] << "," << PERF_SPOT_PATTERN_NAME << "," << argv[2]
              << ",multi_meshnode_process_topology_requires_posix" << std::endl;
    return 0;
}

#endif
