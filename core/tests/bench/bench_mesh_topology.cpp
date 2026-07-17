/* SPDX-License-Identifier: MPL-2.0 */

//  Multi-peer RouteMesh topology benchmark (S4-27 evidence).
//
//  One hub MeshNode admits PERF_MESH_PEERS forked peer processes (the
//  per-process MeshName rule forbids in-process peers), then measures
//  admission convergence, remote actor lookup latency and one NODROP
//  multicast fan-out over the admitted set. Results print as BENCH lines:
//
//    BENCH,mesh_topology,peers=<n>,admission_ms=..,lookup_avg_us=..,
//    multicast_ms=..,multicast_admitted=..
//
//  Run directly or through ctest (bench_mesh_topology, 16 peers). Set
//  PERF_MESH_PEERS for larger sweeps; a WSL host handles a few hundred.

#include <zlink.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <string>
#include <vector>

namespace
{
const char mesh_name[] = "bench-mesh";
const char channel_name[] = "ticks";

uint64_t now_us ()
{
    struct timeval tv;
    gettimeofday (&tv, NULL);
    return static_cast<uint64_t> (tv.tv_sec) * 1000000u + static_cast<uint64_t> (tv.tv_usec);
}

int env_peers ()
{
    const char *value = getenv ("PERF_MESH_PEERS");
    if (!value || !*value)
        return 16;
    const int parsed = atoi (value);
    return parsed > 0 ? parsed : 16;
}

void read_line (int fd_, char *out_, size_t capacity_)
{
    size_t used = 0;
    while (used + 1 < capacity_) {
        char ch = 0;
        if (read (fd_, &ch, 1) <= 0 || ch == '\n')
            break;
        out_[used++] = ch;
    }
    out_[used] = '\0';
}

//  Peer process: admit to the hub, create one actor, subscribe to the
//  benchmark channel and drain one multicast record before exiting.
int run_peer (int index_, int endpoint_fd_)
{
    char endpoint[512];
    read_line (endpoint_fd_, endpoint, sizeof (endpoint));
    if (!endpoint[0])
        return 10;

    void *ctx = zlink_ctx_new ();
    if (!ctx)
        return 11;
    zlink_mesh_node_options_t options;
    memset (&options, 0, sizeof (options));
    options.struct_size = sizeof (options);
    options.version = 1;
    options.mesh_name = mesh_name;
    options.mesh_name_size = strlen (mesh_name);
    void *node = zlink_mesh_node_new (ctx, &options);
    if (!node)
        return 12;
    char rid[32];
    snprintf (rid, sizeof (rid), "peer-%d", index_);
    if (zlink_set_routing_id (node, rid, strlen (rid)) != ZLINK_CONFIG_OK
        || zlink_mesh_node_set_bind (node, "tcp://127.0.0.1:0") != ZLINK_CONFIG_OK
        || zlink_mesh_node_add_channel_name (node, channel_name) != ZLINK_CONFIG_OK
        || zlink_mesh_node_start (node) != ZLINK_CONFIG_OK)
        return 13;

    //  One actor per peer for the hub's remote lookup sweep.
    zlink_actor_ref_t actor;
    memset (&actor, 0, sizeof (actor));
    char actor_id[32];
    snprintf (actor_id, sizeof (actor_id), "worker-%d", index_);
    if (zlink_mesh_node_actor_new (node, actor_id, NULL, 0, &actor, ZLINK_SEND_FLAGS_NONE, 1000)
        != ZLINK_REQUEST_OK)
        return 14;

    //  Subscribe the entry Spot so the hub multicast counts this peer.
    void *spot = NULL;
    if (zlink_mesh_node_entry_spot (node, &spot) != ZLINK_CONFIG_OK
        || zlink_spot_set_subscription (spot, channel_name, "tick.",
                                        ZLINK_SPOT_SUBSCRIPTION_PREFIX)
             != ZLINK_CONFIG_OK)
        return 15;

    zlink_mesh_peer_connection_options_t peer_options;
    memset (&peer_options, 0, sizeof (peer_options));
    peer_options.struct_size = sizeof (peer_options);
    peer_options.version = 1;
    peer_options.endpoint = endpoint;
    peer_options.endpoint_size = strlen (endpoint);
    uint64_t intent = 0;
    if (zlink_mesh_node_connect_peer (node, &peer_options, &intent) != ZLINK_CONNECT_OK)
        return 16;

    //  Wait for admission, then for one multicast record; both bounded.
    int rc = 17;
    for (int waited = 0; waited < 30000; waited += 10) {
        zlink_mesh_peer_entry_t entry;
        memset (&entry, 0, sizeof (entry));
        entry.struct_size = sizeof (entry);
        entry.version = 1;
        size_t count = 1;
        if (zlink_mesh_node_peers (node, &entry, &count) == ZLINK_CONFIG_OK && count == 1
            && entry.state == ZLINK_MESH_PEER_ADMITTED) {
            rc = 0;
            break;
        }
        usleep (10000);
    }
    if (rc == 0) {
        //  Drain until the multicast record arrives (the hub sends exactly
        //  one) so the fan-out measurement includes the full delivery.
        rc = 18;
        void *ready = zlink_mesh_ready_batch_new (8);
        void *batch = zlink_mesh_receive_batch_new (8, 32, 64 * 1024);
        for (int waited = 0; waited < 60000 && ready && batch; waited += 10) {
            uint32_t residue = 0;
            if (zlink_mesh_node_drain_ready (node, ZLINK_MESH_READY_APPLICATION, ready, &residue,
                                             ZLINK_RECV_FLAGS_DONTWAIT)
                != ZLINK_RECV_OK) {
                usleep (10000);
                continue;
            }
            const size_t count = zlink_mesh_ready_batch_count (ready);
            const zlink_mesh_ready_record_t *records = zlink_mesh_ready_batch_data (ready);
            bool got = false;
            for (size_t i = 0; i < count && !got; ++i) {
                if (records[i].owner_kind != ZLINK_MESH_OWNER_SPOT)
                    continue;
                zlink_mesh_claim_t claim;
                if (zlink_mesh_ready_batch_take_claim (ready, i, &claim) != ZLINK_CONFIG_OK)
                    continue;
                zlink_mesh_receive_requirements_t requirements;
                memset (&requirements, 0, sizeof (requirements));
                if (zlink_mesh_claim_recv_batch (&claim, batch, &requirements,
                                                 ZLINK_RECV_FLAGS_NONE)
                    == ZLINK_RECV_OK) {
                    const size_t received_count = zlink_mesh_receive_batch_count (batch);
                    const zlink_mesh_receive_record_t *received =
                      zlink_mesh_receive_batch_data (batch);
                    for (size_t r = 0; r < received_count; ++r) {
                        if (received[r].kind == ZLINK_MESH_RECORD_SPOT_MULTICAST) {
                            got = true;
                            break;
                        }
                    }
                }
                zlink_mesh_claim_release (&claim);
                zlink_mesh_receive_batch_reset (batch);
            }
            zlink_mesh_ready_batch_reset (ready);
            if (got) {
                rc = 0;
                break;
            }
            usleep (5000);
        }
        if (ready)
            zlink_mesh_ready_batch_destroy (&ready);
        if (batch)
            zlink_mesh_receive_batch_destroy (&batch);
    }

    zlink_spot_destroy (&spot);
    zlink_mesh_node_shutdown (node, 1000);
    zlink_mesh_node_destroy (&node);
    zlink_ctx_term (ctx);
    return rc;
}
} // namespace

int main ()
{
    const int peers = env_peers ();

    //  Fork every peer before the hub node exists: the forked children
    //  inherit the process-wide MeshName registry.
    std::vector<pid_t> children (static_cast<size_t> (peers));
    std::vector<int> pipes (static_cast<size_t> (peers));
    for (int i = 0; i < peers; ++i) {
        int fds[2];
        if (pipe (fds) != 0) {
            perror ("pipe");
            return 1;
        }
        fflush (NULL);
        const pid_t child = fork ();
        if (child < 0) {
            perror ("fork");
            return 1;
        }
        if (child == 0) {
            close (fds[1]);
            const int rc = run_peer (i, fds[0]);
            close (fds[0]);
            fflush (NULL);
            _exit (rc);
        }
        close (fds[0]);
        children[static_cast<size_t> (i)] = child;
        pipes[static_cast<size_t> (i)] = fds[1];
    }

    void *ctx = zlink_ctx_new ();
    zlink_mesh_node_options_t options;
    memset (&options, 0, sizeof (options));
    options.struct_size = sizeof (options);
    options.version = 1;
    options.mesh_name = mesh_name;
    options.mesh_name_size = strlen (mesh_name);
    void *node = zlink_mesh_node_new (ctx, &options);
    if (!node || zlink_set_routing_id (node, "hub", 3) != ZLINK_CONFIG_OK
        || zlink_mesh_node_set_bind (node, "tcp://127.0.0.1:0") != ZLINK_CONFIG_OK
        || zlink_mesh_node_add_channel_name (node, channel_name) != ZLINK_CONFIG_OK
        || zlink_mesh_node_start (node) != ZLINK_CONFIG_OK) {
        fprintf (stderr, "hub start failed errno=%d\n", errno);
        return 1;
    }
    zlink_mesh_node_status_t status;
    memset (&status, 0, sizeof (status));
    status.struct_size = sizeof (status);
    status.version = 1;
    zlink_mesh_node_status (node, &status);

    //  1) admission convergence over N peers.
    const uint64_t admission_start = now_us ();
    for (int i = 0; i < peers; ++i) {
        dprintf (pipes[static_cast<size_t> (i)], "%s\n", status.local_endpoint);
        close (pipes[static_cast<size_t> (i)]);
    }
    bool admitted_all = false;
    while (now_us () - admission_start < 120u * 1000000u) {
        zlink_mesh_node_status_t current;
        memset (&current, 0, sizeof (current));
        current.struct_size = sizeof (current);
        current.version = 1;
        zlink_mesh_node_status (node, &current);
        if (current.admitted_peer_count >= static_cast<uint32_t> (peers)) {
            admitted_all = true;
            break;
        }
        usleep (2000);
    }
    const uint64_t admission_us = now_us () - admission_start;
    if (!admitted_all) {
        fprintf (stderr, "admission did not converge (peers=%d)\n", peers);
        return 1;
    }

    //  2) remote actor lookup latency sweep (one per peer, sequential).
    std::vector<zlink_mesh_peer_entry_t> entries (static_cast<size_t> (peers) + 4);
    for (size_t i = 0; i < entries.size (); ++i) {
        memset (&entries[i], 0, sizeof (entries[i]));
        entries[i].struct_size = sizeof (entries[i]);
        entries[i].version = 1;
    }
    size_t entry_count = entries.size ();
    if (zlink_mesh_node_peers (node, &entries[0], &entry_count) != ZLINK_CONFIG_OK) {
        fprintf (stderr, "peers query failed\n");
        return 1;
    }
    uint64_t lookup_total_us = 0;
    uint32_t lookups = 0;
    void *ready = zlink_mesh_ready_batch_new (8);
    void *batch = zlink_mesh_receive_batch_new (8, 32, 64 * 1024);
    for (size_t i = 0; i < entry_count; ++i) {
        if (entries[i].state != ZLINK_MESH_PEER_ADMITTED)
            continue;
        char actor_id[32];
        snprintf (actor_id, sizeof (actor_id), "worker-%.*s", (int) entries[i].routing_id.size - 5,
                  (const char *) entries[i].routing_id.data + 5);
        const uint64_t start = now_us ();
        zlink_mesh_operation_id_t op;
        memset (&op, 0, sizeof (op));
        if (zlink_mesh_node_actor_lookup_remote (node, &entries[i].routing_id, actor_id, &op,
                                                 10000)
            != ZLINK_SUBMIT_OK)
            continue;
        //  Drain the completion.
        bool done = false;
        while (!done && now_us () - start < 10u * 1000000u) {
            uint32_t residue = 0;
            if (zlink_mesh_node_drain_ready (node, ZLINK_MESH_READY_INFRASTRUCTURE, ready,
                                             &residue, ZLINK_RECV_FLAGS_DONTWAIT)
                != ZLINK_RECV_OK) {
                usleep (200);
                continue;
            }
            const size_t count = zlink_mesh_ready_batch_count (ready);
            const zlink_mesh_ready_record_t *records = zlink_mesh_ready_batch_data (ready);
            for (size_t r = 0; r < count && !done; ++r) {
                if (records[r].owner_kind != ZLINK_MESH_OWNER_NODE)
                    continue;
                zlink_mesh_claim_t claim;
                if (zlink_mesh_ready_batch_take_claim (ready, r, &claim) != ZLINK_CONFIG_OK)
                    continue;
                zlink_mesh_receive_requirements_t requirements;
                memset (&requirements, 0, sizeof (requirements));
                if (zlink_mesh_claim_recv_batch (&claim, batch, &requirements,
                                                 ZLINK_RECV_FLAGS_NONE)
                    == ZLINK_RECV_OK) {
                    const size_t records_in = zlink_mesh_receive_batch_count (batch);
                    const zlink_mesh_receive_record_t *received =
                      zlink_mesh_receive_batch_data (batch);
                    for (size_t k = 0; k < records_in; ++k) {
                        if (received[k].kind == ZLINK_MESH_RECORD_COMPLETION
                            && received[k].operation_id.low == op.low)
                            done = true;
                    }
                }
                zlink_mesh_claim_release (&claim);
                zlink_mesh_receive_batch_reset (batch);
            }
            zlink_mesh_ready_batch_reset (ready);
            if (!done)
                usleep (200);
        }
        if (done) {
            lookup_total_us += now_us () - start;
            ++lookups;
        }
    }

    //  3) one NODROP multicast over the full admitted set.
    void *publisher = zlink_mesh_node_publisher_new (node);
    zlink_msg_t part;
    zlink_msg_init_size (&part, 64);
    memset (zlink_msg_data (&part), 't', 64);
    zlink_mesh_publish_detail_t detail;
    memset (&detail, 0, sizeof (detail));
    detail.struct_size = sizeof (detail);
    detail.version = 1;
    const uint64_t multicast_start = now_us ();
    const zlink_submit_result_t publish_rc = zlink_mesh_node_publisher_publish (
      publisher, channel_name, "tick.bench", NULL, &part, 1, &detail, ZLINK_SEND_FLAGS_NONE);
    const uint64_t multicast_us = now_us () - multicast_start;
    zlink_msg_close (&part);
    zlink_mesh_node_publisher_destroy (&publisher);
    if (publish_rc != ZLINK_SUBMIT_OK) {
        fprintf (stderr, "multicast publish failed rc=%d errno=%d\n", (int) publish_rc, errno);
        return 1;
    }

    //  4) drain: every peer exits after receiving the multicast.
    int failures = 0;
    for (int i = 0; i < peers; ++i) {
        int child_status = 0;
        if (waitpid (children[static_cast<size_t> (i)], &child_status, 0) < 0
            || !WIFEXITED (child_status) || WEXITSTATUS (child_status) != 0)
            ++failures;
    }
    const uint64_t drain_start = now_us ();
    bool drained = false;
    while (now_us () - drain_start < 60u * 1000000u) {
        zlink_mesh_node_status_t current;
        memset (&current, 0, sizeof (current));
        current.struct_size = sizeof (current);
        current.version = 1;
        zlink_mesh_node_status (node, &current);
        if (current.admitted_peer_count == 0) {
            drained = true;
            break;
        }
        usleep (5000);
    }
    const uint64_t drain_us = now_us () - drain_start;

    printf ("BENCH,mesh_topology,peers=%d,admission_ms=%.1f,lookup_avg_us=%.1f,"
            "multicast_ms=%.3f,multicast_admitted=%u,drain_ms=%.1f,peer_failures=%d,"
            "drained=%d\n",
            peers, admission_us / 1000.0, lookups ? (double) lookup_total_us / lookups : -1.0,
            multicast_us / 1000.0, detail.admitted_remote_target_count, drain_us / 1000.0,
            failures, drained ? 1 : 0);

    zlink_mesh_ready_batch_destroy (&ready);
    zlink_mesh_receive_batch_destroy (&batch);
    zlink_mesh_node_shutdown (node, 2000);
    zlink_mesh_node_destroy (&node);
    zlink_ctx_term (ctx);
    return (failures == 0 && drained) ? 0 : 1;
}
