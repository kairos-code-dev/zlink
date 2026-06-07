/* SPDX-License-Identifier: MPL-2.0 */

#include "../testutil.hpp"

#include <cerrno>
#include <climits>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#if !defined(_WIN32)
#include <sys/resource.h>
#endif

namespace
{
const char *kPayload = "payload";

struct recv_run_result_t
{
    bool ok;
    double elapsed_ms;
    size_t delivered;
};

struct recv_prepare_result_t
{
    bool ok;
    double elapsed_ms;
    size_t prepared;
};

int env_int_or_default (const char *name_, int default_value_)
{
    const char *value = getenv (name_);
    if (!value || !*value)
        return default_value_;

    char *end = NULL;
    const long parsed = strtol (value, &end, 10);
    if (end == value || (end && *end != '\0') || parsed <= 0)
        return default_value_;

    return parsed > INT_MAX ? INT_MAX : static_cast<int> (parsed);
}

size_t read_rss_kb ()
{
#if defined(__linux__)
    FILE *fp = fopen ("/proc/self/status", "r");
    if (!fp)
        return 0;

    char line[256];
    size_t value = 0;
    while (fgets (line, sizeof (line), fp)) {
        if (strncmp (line, "VmRSS:", 6) == 0) {
            unsigned long parsed = 0;
            if (sscanf (line + 6, "%lu", &parsed) == 1)
                value = static_cast<size_t> (parsed);
            break;
        }
    }

    fclose (fp);
    return value;
#else
    return 0;
#endif
}

bool set_zero_linger (void *handle_)
{
    const int linger = 0;
    return zlink_set_option (handle_, ZLINK_OPT_LINGER, &linger, sizeof (linger))
           == ZLINK_CONFIG_OK;
}

bool set_ctx_socket_limit (void *ctx_, int total_spots_)
{
    const int desired_limit = total_spots_ > 12000 ? total_spots_ * 8 : 100000;
    return zlink_ctx_set (ctx_, ZLINK_MAX_SOCKETS, desired_limit) == ZLINK_CONFIG_OK;
}

bool raise_nofile_limit_for_bench (int total_spots_)
{
#if defined(_WIN32)
    (void) total_spots_;
    return true;
#else
    const rlim_t desired = static_cast<rlim_t> (total_spots_ > 12000 ? total_spots_ * 16 : 100000);

    struct rlimit current;
    if (getrlimit (RLIMIT_NOFILE, &current) != 0)
        return false;

    struct rlimit updated = current;
    if (updated.rlim_cur < desired)
        updated.rlim_cur = desired;
    if (updated.rlim_max < updated.rlim_cur)
        updated.rlim_cur = updated.rlim_max;

    if (updated.rlim_cur == current.rlim_cur)
        return true;

    return setrlimit (RLIMIT_NOFILE, &updated) == 0;
#endif
}

bool send_one_routed (void *router_,
                      const zlink_routing_id_t *node_rid_,
                      const zlink_routing_id_t *spot_rid_,
                      const char *payload_)
{
    zlink_msg_t part;
    if (zlink_msg_init_size (&part, payload_ ? strlen (payload_) : 0) != ZLINK_CONFIG_OK) {
        return false;
    }

    if (payload_ && *payload_)
        memcpy (zlink_msg_data (&part), payload_, strlen (payload_));

    const zlink_submit_result_t rc = zlink_router_send_spot_part (
      router_, node_rid_, spot_rid_, &part, static_cast<zlink_send_flags_t> (0), ZLINK_PART_FINAL);
    if (rc != ZLINK_SUBMIT_OK) {
        const int saved_errno = errno;
        zlink_msg_close (&part);
        errno = saved_errno;
        return false;
    }

    return true;
}

bool try_recv_one_routed (void *spot_,
                          const zlink_routing_id_t *expected_source_rid_,
                          const char *expected_payload_)
{
    errno = 0;
    const zlink_routing_id_t *source_node_rid = NULL;
    const zlink_routing_id_t *source_spot_rid = NULL;
    uint64_t request_seq = 0;
    zlink_msg_t part;
    zlink_msg_init (&part);
    zlink_part_flag_t has_more = ZLINK_PART_FINAL;

    const zlink_recv_result_t rc =
      zlink_spot_recv_part (spot_, &source_node_rid, &source_spot_rid, &request_seq, &part,
                            &has_more, static_cast<zlink_recv_flags_t> (ZLINK_DONTWAIT));

    if (rc == ZLINK_RECV_NO_DATA) {
        errno = 0;
        zlink_msg_close (&part);
        return false;
    }

    if (rc != ZLINK_RECV_OK) {
        zlink_msg_close (&part);
        return false;
    }

    const bool source_matches =
      expected_source_rid_ && source_node_rid && expected_source_rid_->size == source_node_rid->size
      && memcmp (expected_source_rid_->data, source_node_rid->data, source_node_rid->size) == 0;
    const bool shape_ok =
      source_matches && (!source_spot_rid || source_spot_rid->size == 0) && request_seq == 0
      && has_more == ZLINK_PART_FINAL && zlink_msg_size (&part) == strlen (expected_payload_)
      && memcmp (zlink_msg_data (&part), expected_payload_, strlen (expected_payload_)) == 0;

    zlink_msg_close (&part);

    if (!shape_ok) {
        fprintf (stderr, "routed recv payload or metadata mismatch\n");
        errno = EPROTO;
    }

    return shape_ok;
}

recv_prepare_result_t prepare_all_receivers_for_routed_recv (const std::vector<void *> &receivers_)
{
    recv_prepare_result_t result = {false, 0.0, 0};
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now ();

    for (size_t i = 0; i < receivers_.size (); ++i) {
        errno = 0;
        const zlink_routing_id_t *source_node_rid = NULL;
        const zlink_routing_id_t *source_spot_rid = NULL;
        uint64_t request_seq = 0;
        zlink_msg_t part;
        zlink_msg_init (&part);
        zlink_part_flag_t has_more = ZLINK_PART_FINAL;

        const zlink_recv_result_t rc =
          zlink_spot_recv_part (receivers_[i], &source_node_rid, &source_spot_rid, &request_seq,
                                &part, &has_more, static_cast<zlink_recv_flags_t> (ZLINK_DONTWAIT));
        zlink_msg_close (&part);

        if (rc == ZLINK_RECV_NO_DATA && errno == EAGAIN) {
            ++result.prepared;
            continue;
        }

        if (rc == ZLINK_RECV_OK) {
            ++result.prepared;
            continue;
        }

        result.elapsed_ms =
          std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now () - start)
            .count ();
        return result;
    }

    result.elapsed_ms =
      std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now () - start)
        .count ();
    result.ok = true;
    return result;
}

recv_run_result_t recv_all_routed (const std::vector<void *> &receivers_,
                                   const zlink_routing_id_t *expected_source_rid_,
                                   const char *payload_,
                                   int timeout_ms_)
{
    recv_run_result_t result = {false, 0.0, 0};
    std::vector<unsigned char> delivered (receivers_.size (), 0);
    size_t remaining = receivers_.size ();
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now ();
    const std::chrono::steady_clock::time_point deadline =
      start + std::chrono::milliseconds (timeout_ms_);

    while (remaining > 0 && std::chrono::steady_clock::now () < deadline) {
        bool made_progress = false;
        for (size_t i = 0; i < receivers_.size (); ++i) {
            if (delivered[i])
                continue;

            if (try_recv_one_routed (receivers_[i], expected_source_rid_, payload_)) {
                delivered[i] = 1;
                --remaining;
                ++result.delivered;
                made_progress = true;
            } else if (errno == EPROTO) {
                return result;
            }
        }

        if (!made_progress)
            msleep (1);
    }

    result.elapsed_ms =
      std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now () - start)
        .count ();
    result.ok = remaining == 0;
    return result;
}

void destroy_spots (std::vector<void *> *spots_)
{
    if (!spots_)
        return;

    for (size_t i = 0; i < spots_->size (); ++i) {
        if (!(*spots_)[i])
            continue;
        void *spot = (*spots_)[i];
        zlink_spot_destroy (&spot);
        (*spots_)[i] = NULL;
    }
}

} // namespace

int main (int, char **)
{
    setup_test_environment (0);

    const int total_spots = env_int_or_default ("ZLINK_SPOT_BENCH_SPOTS", 10000);
    const int timeout_ms = env_int_or_default ("ZLINK_SPOT_BENCH_TIMEOUT_MS", 30000);

    if (total_spots < 2) {
        fprintf (stderr, "need at least 2 spots\n");
        return 2;
    }

    void *ctx = zlink_ctx_new ();
    if (!ctx) {
        fprintf (stderr, "zlink_ctx_new failed errno=%d (%s)\n", errno, zlink_strerror (errno));
        return 1;
    }

    (void) raise_nofile_limit_for_bench (total_spots);

    if (!set_ctx_socket_limit (ctx, total_spots)) {
        fprintf (stderr, "zlink_ctx_set(ZLINK_MAX_SOCKETS) failed errno=%d (%s)\n", errno,
                 zlink_strerror (errno));
        zlink_ctx_term (ctx);
        return 1;
    }

    void *node = zlink_spot_node_new (ctx, NULL);
    void *router = zlink_socket (ctx, ZLINK_SOCKET_ROUTER);
    if (!node || !router) {
        fprintf (stderr, "node/router create failed errno=%d (%s)\n", errno,
                 zlink_strerror (errno));
        if (router)
            zlink_close (router);
        if (node)
            zlink_spot_node_destroy (&node);
        zlink_ctx_term (ctx);
        return 1;
    }

    set_zero_linger (node);
    set_zero_linger (router);

    const size_t receiver_count = static_cast<size_t> (total_spots - 1);
    std::vector<void *> receivers;
    receivers.reserve (receiver_count);

    const std::chrono::steady_clock::time_point create_start = std::chrono::steady_clock::now ();
    for (size_t i = 0; i < receiver_count; ++i) {
        void *spot = zlink_spot_new (node);
        if (!spot) {
            fprintf (stderr, "zlink_spot_new(receiver %zu) failed errno=%d (%s)\n", i, errno,
                     zlink_strerror (errno));
            destroy_spots (&receivers);
            zlink_close (router);
            zlink_spot_node_destroy (&node);
            zlink_ctx_term (ctx);
            return 1;
        }
        set_zero_linger (spot);
        receivers.push_back (spot);
    }
    const double create_ms =
      std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now () - create_start)
        .count ();
    const size_t rss_after_create_kb = read_rss_kb ();

    const std::chrono::steady_clock::time_point routing_id_start =
      std::chrono::steady_clock::now ();
    zlink_routing_id_t node_rid;
    zlink_routing_id_t router_rid;
    memset (&node_rid, 0, sizeof (node_rid));
    memset (&router_rid, 0, sizeof (router_rid));
    if (zlink_get_routing_id (node, &node_rid) != ZLINK_CONFIG_OK
        || zlink_set_routing_id (router, "bench-router", 12) != ZLINK_CONFIG_OK
        || zlink_get_routing_id (router, &router_rid) != ZLINK_CONFIG_OK) {
        fprintf (stderr, "routing id setup failed errno=%d (%s)\n", errno, zlink_strerror (errno));
        destroy_spots (&receivers);
        zlink_close (router);
        zlink_spot_node_destroy (&node);
        zlink_ctx_term (ctx);
        return 1;
    }

    std::vector<zlink_routing_id_t> receiver_rids (receiver_count);
    for (size_t i = 0; i < receivers.size (); ++i) {
        memset (&receiver_rids[i], 0, sizeof (receiver_rids[i]));
        if (zlink_get_routing_id (receivers[i], &receiver_rids[i]) != ZLINK_CONFIG_OK) {
            fprintf (stderr, "receiver routing id fetch failed index=%zu errno=%d (%s)\n", i, errno,
                     zlink_strerror (errno));
            destroy_spots (&receivers);
            zlink_close (router);
            zlink_spot_node_destroy (&node);
            zlink_ctx_term (ctx);
            return 1;
        }
    }
    const double routing_id_ms = std::chrono::duration<double, std::milli> (
                                   std::chrono::steady_clock::now () - routing_id_start)
                                   .count ();
    const size_t rss_after_routing_id_kb = read_rss_kb ();

    const recv_prepare_result_t recv_prepare = prepare_all_receivers_for_routed_recv (receivers);
    if (!recv_prepare.ok) {
        fprintf (stderr, "routed recv prepare failed prepared=%zu/%zu errno=%d (%s)\n",
                 recv_prepare.prepared, receivers.size (), errno, zlink_strerror (errno));
        destroy_spots (&receivers);
        zlink_close (router);
        zlink_spot_node_destroy (&node);
        zlink_ctx_term (ctx);
        return 1;
    }
    const size_t rss_after_recv_prepare_kb = read_rss_kb ();

    const std::chrono::steady_clock::time_point send_start = std::chrono::steady_clock::now ();
    for (size_t i = 0; i < receivers.size (); ++i) {
        if (!send_one_routed (router, &node_rid, &receiver_rids[i], kPayload)) {
            fprintf (stderr, "router send failed index=%zu errno=%d (%s)\n", i, errno,
                     zlink_strerror (errno));
            destroy_spots (&receivers);
            zlink_close (router);
            zlink_spot_node_destroy (&node);
            zlink_ctx_term (ctx);
            return 1;
        }
    }
    const double send_ms =
      std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now () - send_start)
        .count ();

    const recv_run_result_t recv_result =
      recv_all_routed (receivers, &router_rid, kPayload, timeout_ms);
    if (!recv_result.ok) {
        fprintf (stderr, "routed recv timed out delivered=%zu/%zu errno=%d (%s)\n",
                 recv_result.delivered, receivers.size (), errno, zlink_strerror (errno));
        destroy_spots (&receivers);
        zlink_close (router);
        zlink_spot_node_destroy (&node);
        zlink_ctx_term (ctx);
        return 1;
    }

    const size_t rss_after_recv_kb = read_rss_kb ();

    const std::chrono::steady_clock::time_point destroy_start = std::chrono::steady_clock::now ();
    destroy_spots (&receivers);
    zlink_close (router);
    zlink_spot_node_destroy (&node);
    zlink_ctx_term (ctx);
    const double destroy_ms =
      std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now () - destroy_start)
        .count ();

    printf ("spot_total=%d\n", total_spots);
    printf ("receiver_spots=%zu\n", receiver_count);
    printf ("ctx_max_sockets=%d\n", total_spots > 12000 ? total_spots * 4 : 50000);
    printf ("create_ms=%.3f\n", create_ms);
    printf ("rss_after_create_kb=%zu\n", rss_after_create_kb);
    printf ("routing_id_ms=%.3f\n", routing_id_ms);
    printf ("rss_after_routing_id_kb=%zu\n", rss_after_routing_id_kb);
    printf ("recv_prepare_ms=%.3f\n", recv_prepare.elapsed_ms);
    printf ("rss_after_recv_prepare_kb=%zu\n", rss_after_recv_prepare_kb);
    printf ("send_ms=%.3f\n", send_ms);
    printf ("recv_drain_ms=%.3f\n", recv_result.elapsed_ms);
    printf ("end_to_end_ms=%.3f\n", send_ms + recv_result.elapsed_ms);
    printf ("rss_after_recv_kb=%zu\n", rss_after_recv_kb);
    printf ("destroy_ms=%.3f\n", destroy_ms);

    return 0;
}
