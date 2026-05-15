/* SPDX-License-Identifier: MPL-2.0 */

#include "../testutil.hpp"
#include "../../src/runtime/services/spot/pubsub/spot_subject_access.hpp"

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
#include <unistd.h>
#endif

namespace
{
const char *kTopic = "bench.topic.10000";
const char *kWarmupPayload = "warmup";
const char *kMeasuredPayload = "payload";

struct publish_run_result_t
{
    bool ok;
    double elapsed_ms;
    size_t delivered;
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
    const int desired_limit =
      total_spots_ > 12000 ? total_spots_ * 8 : 100000;
    return zlink_ctx_set (ctx_, ZLINK_MAX_SOCKETS, desired_limit)
           == ZLINK_CONFIG_OK;
}

bool raise_nofile_limit_for_bench (int total_spots_)
{
#if defined(_WIN32)
    (void) total_spots_;
    return true;
#else
    const rlim_t desired =
      static_cast<rlim_t> (total_spots_ > 12000 ? total_spots_ * 16 : 100000);

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

bool publish_one_part (void *spot_, const char *payload_)
{
    zlink_msg_t part;
    if (zlink_msg_init_size (&part, payload_ ? strlen (payload_) : 0)
        != ZLINK_CONFIG_OK) {
        return false;
    }

    if (payload_ && *payload_)
        memcpy (zlink_msg_data (&part), payload_, strlen (payload_));

    if (spot_subject_publish (spot_, kTopic, &part, 1,
                              static_cast<zlink_send_flags_t> (0))
        != 0) {
        const int saved_errno = errno;
        zlink_msg_close (&part);
        errno = saved_errno;
        return false;
    }

    return true;
}

bool try_recv_one_part (void *spot_, const char *expected_payload_)
{
    errno = 0;
    char topic_name[128];
    size_t topic_name_len = sizeof (topic_name);
    zlink_msg_t *parts = NULL;
    size_t part_count = 0;

    if (spot_subject_recv (spot_, NULL, &parts, &part_count, topic_name,
                           &topic_name_len,
                           static_cast<zlink_recv_flags_t> (ZLINK_DONTWAIT))
        != 0) {
        if (errno == EAGAIN)
            errno = 0;
        return false;
    }

    const bool shape_ok =
      part_count == 1 && topic_name_len == strlen (kTopic)
      && memcmp (topic_name, kTopic, topic_name_len) == 0
      && zlink_msg_size (&parts[0]) == strlen (expected_payload_)
      && memcmp (zlink_msg_data (&parts[0]), expected_payload_,
                 strlen (expected_payload_))
           == 0;

    zlink_multipart_close (parts, part_count);

    if (!shape_ok) {
        fprintf (stderr, "recv payload mismatch or multipart shape mismatch\n");
        errno = EPROTO;
    }

    return shape_ok;
}

publish_run_result_t publish_and_collect (void *publisher_,
                                          const std::vector<void *> &subscribers_,
                                          const char *payload_,
                                          int timeout_ms_)
{
    publish_run_result_t result = {false, 0.0, 0};
    if (!publish_one_part (publisher_, payload_)) {
        fprintf (stderr, "publish failed errno=%d (%s)\n", errno,
                 zlink_strerror (errno));
        return result;
    }

    std::vector<unsigned char> delivered (subscribers_.size (), 0);
    size_t remaining = subscribers_.size ();
    const std::chrono::steady_clock::time_point start =
      std::chrono::steady_clock::now ();
    const std::chrono::steady_clock::time_point deadline =
      start + std::chrono::milliseconds (timeout_ms_);

    while (remaining > 0 && std::chrono::steady_clock::now () < deadline) {
        bool made_progress = false;
        for (size_t i = 0; i < subscribers_.size (); ++i) {
            if (delivered[i])
                continue;

            if (try_recv_one_part (subscribers_[i], payload_)) {
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
      std::chrono::duration<double, std::milli> (
        std::chrono::steady_clock::now () - start)
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
    const int settle_ms = env_int_or_default ("ZLINK_SPOT_BENCH_SETTLE_MS", 200);
    const int timeout_ms =
      env_int_or_default ("ZLINK_SPOT_BENCH_TIMEOUT_MS", 30000);

    if (total_spots < 2) {
        fprintf (stderr, "need at least 2 spots\n");
        return 2;
    }

    void *ctx = zlink_ctx_new ();
    if (!ctx) {
        fprintf (stderr, "zlink_ctx_new failed errno=%d (%s)\n", errno,
                 zlink_strerror (errno));
        return 1;
    }

    (void) raise_nofile_limit_for_bench (total_spots);

    if (!set_ctx_socket_limit (ctx, total_spots)) {
        fprintf (stderr, "zlink_ctx_set(ZLINK_MAX_SOCKETS) failed errno=%d (%s)\n",
                 errno, zlink_strerror (errno));
        zlink_ctx_term (ctx);
        return 1;
    }

    void *node = zlink_spot_node_new (ctx, NULL);
    if (!node) {
        fprintf (stderr, "zlink_spot_node_new failed errno=%d (%s)\n", errno,
                 zlink_strerror (errno));
        zlink_ctx_term (ctx);
        return 1;
    }

    set_zero_linger (node);

    const size_t subscriber_count = static_cast<size_t> (total_spots - 1);
    std::vector<void *> subscribers;
    subscribers.reserve (subscriber_count);

    const std::chrono::steady_clock::time_point create_start =
      std::chrono::steady_clock::now ();

    void *publisher = zlink_spot_new (node);
    if (!publisher) {
        fprintf (stderr, "zlink_spot_new(publisher) failed errno=%d (%s)\n",
                 errno, zlink_strerror (errno));
        zlink_spot_node_destroy (&node);
        zlink_ctx_term (ctx);
        return 1;
    }
    set_zero_linger (publisher);

    for (size_t i = 0; i < subscriber_count; ++i) {
        void *spot = zlink_spot_new (node);
        if (!spot) {
            fprintf (stderr,
                     "zlink_spot_new(subscriber %zu) failed errno=%d (%s)\n", i,
                     errno, zlink_strerror (errno));
            destroy_spots (&subscribers);
            zlink_spot_destroy (&publisher);
            zlink_spot_node_destroy (&node);
            zlink_ctx_term (ctx);
            return 1;
        }
        set_zero_linger (spot);
        subscribers.push_back (spot);
    }

    const double create_ms =
      std::chrono::duration<double, std::milli> (
        std::chrono::steady_clock::now () - create_start)
        .count ();
    const size_t rss_after_create_kb = read_rss_kb ();

    const std::chrono::steady_clock::time_point subscribe_start =
      std::chrono::steady_clock::now ();
    for (size_t i = 0; i < subscribers.size (); ++i) {
        if (zlink_set_subscription (subscribers[i], kTopic) != ZLINK_CONFIG_OK) {
            fprintf (stderr,
                     "zlink_set_subscription(subscriber %zu) failed errno=%d (%s)\n",
                     i, errno, zlink_strerror (errno));
            destroy_spots (&subscribers);
            zlink_spot_destroy (&publisher);
            zlink_spot_node_destroy (&node);
            zlink_ctx_term (ctx);
            return 1;
        }
    }

    const double subscribe_ms =
      std::chrono::duration<double, std::milli> (
        std::chrono::steady_clock::now () - subscribe_start)
        .count ();
    msleep (settle_ms);
    const size_t rss_after_subscribe_kb = read_rss_kb ();

    const publish_run_result_t warmup =
      publish_and_collect (publisher, subscribers, kWarmupPayload, timeout_ms);
    if (!warmup.ok) {
        fprintf (stderr,
                 "warmup publish timed out delivered=%zu/%zu errno=%d (%s)\n",
                 warmup.delivered, subscribers.size (), errno,
                 zlink_strerror (errno));
        destroy_spots (&subscribers);
        zlink_spot_destroy (&publisher);
        zlink_spot_node_destroy (&node);
        zlink_ctx_term (ctx);
        return 1;
    }

    const publish_run_result_t measured =
      publish_and_collect (publisher, subscribers, kMeasuredPayload, timeout_ms);
    if (!measured.ok) {
        fprintf (stderr,
                 "measured publish timed out delivered=%zu/%zu errno=%d (%s)\n",
                 measured.delivered, subscribers.size (), errno,
                 zlink_strerror (errno));
        destroy_spots (&subscribers);
        zlink_spot_destroy (&publisher);
        zlink_spot_node_destroy (&node);
        zlink_ctx_term (ctx);
        return 1;
    }

    const size_t rss_after_publish_kb = read_rss_kb ();

    const std::chrono::steady_clock::time_point destroy_start =
      std::chrono::steady_clock::now ();
    destroy_spots (&subscribers);
    zlink_spot_destroy (&publisher);
    zlink_spot_node_destroy (&node);
    zlink_ctx_term (ctx);
    const double destroy_ms =
      std::chrono::duration<double, std::milli> (
        std::chrono::steady_clock::now () - destroy_start)
        .count ();

    printf ("spot_total=%d\n", total_spots);
    printf ("subscriber_spots=%zu\n", subscriber_count);
    printf ("ctx_max_sockets=%d\n",
            total_spots > 12000 ? total_spots * 4 : 50000);
    printf ("create_ms=%.3f\n", create_ms);
    printf ("rss_after_create_kb=%zu\n", rss_after_create_kb);
    printf ("subscribe_ms=%.3f\n", subscribe_ms);
    printf ("rss_after_subscribe_kb=%zu\n", rss_after_subscribe_kb);
    printf ("warmup_publish_fanout_ms=%.3f\n", warmup.elapsed_ms);
    printf ("measured_publish_fanout_ms=%.3f\n", measured.elapsed_ms);
    printf ("rss_after_publish_kb=%zu\n", rss_after_publish_kb);
    printf ("destroy_ms=%.3f\n", destroy_ms);

    return 0;
}
