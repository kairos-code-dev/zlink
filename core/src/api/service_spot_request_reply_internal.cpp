/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "api/service_spot_request_reply_internal.hpp"
#include "api/request_reply_protocol_internal.hpp"
#include "core/multipart_send_txn.hpp"
#include "services/spot/spot_data_plane_internal.hpp"
#include "services/spot/spot_node.hpp"
#include "services/spot/spot_node_access.hpp"
#include "services/spot/spot_pub.hpp"
#include "services/spot/spot_runtime.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace zlink
{
namespace spot_reqrep_internal
{
namespace
{
#ifdef _WIN32
int current_process_id ()
{
    return _getpid ();
}
#else
int current_process_id ()
{
    return getpid ();
}
#endif

enum : uint8_t
{
    zmp_router_class = 0x02
};

size_t hash_combine (size_t seed_, size_t value_)
{
    return seed_ ^ (value_ + 0x9e3779b97f4a7c15ULL + (seed_ << 6)
                    + (seed_ >> 2));
}

bool spot_route_stats_enabled ()
{
    return std::getenv ("ZLINK_DEBUG_SPOT_ROUTE_STATS") != NULL;
}

struct spot_route_stats_t
{
    std::atomic<unsigned long long> publish_count;
    std::atomic<unsigned long long> publish_ns;

    spot_route_stats_t () : publish_count (0), publish_ns (0) {}

    ~spot_route_stats_t ()
    {
        if (!spot_route_stats_enabled ())
            return;
        const unsigned long long count =
          publish_count.load (std::memory_order_relaxed);
        const unsigned long long total_ns =
          publish_ns.load (std::memory_order_relaxed);
        const unsigned long long avg_ns = count == 0 ? 0 : total_ns / count;
        std::fprintf (stderr,
                      "[spot-route-stats] publish count=%llu ns=%llu avg_ns=%llu\n",
                      count, total_ns, avg_ns);
        std::fflush (stderr);
        char path[128];
        std::snprintf (path, sizeof (path), "/tmp/zlink_spot_route_publish_%d.log",
                       current_process_id ());
        FILE *fp = std::fopen (path, "a");
        if (fp) {
            std::fprintf (fp, "count=%llu ns=%llu avg_ns=%llu\n", count,
                          total_ns, avg_ns);
            std::fclose (fp);
        }
    }
};

spot_route_stats_t g_spot_route_stats;
}

bool pending_spot_key_t::operator== (const pending_spot_key_t &other_) const
{
    return request_seq == other_.request_seq
           && source_class == other_.source_class
           && source_rid == other_.source_rid
           && source_spot_rid == other_.source_spot_rid;
}

bool pending_spot_key_t::operator< (const pending_spot_key_t &other_) const
{
    if (request_seq != other_.request_seq)
        return request_seq < other_.request_seq;
    if (source_class != other_.source_class)
        return source_class < other_.source_class;
    if (source_rid != other_.source_rid)
        return source_rid < other_.source_rid;
    return source_spot_rid < other_.source_spot_rid;
}

size_t pending_spot_key_hash_t::operator() (const pending_spot_key_t &key_) const
{
    size_t seed = std::hash<uint64_t> () (key_.request_seq);
    seed = hash_combine (seed, std::hash<uint8_t> () (key_.source_class));
    seed = hash_combine (seed, std::hash<std::string> () (key_.source_rid));
    return hash_combine (seed,
                         std::hash<std::string> () (key_.source_spot_rid));
}

spot_dispatch_state_t::spot_dispatch_state_t () :
    handler (NULL),
    handler_userdata (NULL),
    runtime (NULL),
    task_id (0),
    active_info_valid (false),
    running (false)
{
    memset (&active_info, 0, sizeof (active_info));
}

spot_channel_reply_source_t::spot_channel_reply_source_t (void *dealer_) :
    dealer (dealer_)
{
}

queued_spot_subscribe_message_t::queued_spot_subscribe_message_t ()
{
    memset (&source_rid, 0, sizeof (source_rid));
}

queued_spot_subscribe_message_t::~queued_spot_subscribe_message_t ()
{
    for (size_t i = 0; i < parts.size (); ++i)
        zlink_msg_close (&parts[i]);
}

queued_spot_subscribe_message_t::queued_spot_subscribe_message_t (
  queued_spot_subscribe_message_t &&other_) noexcept :
    source_rid (other_.source_rid),
    topic (std::move (other_.topic)),
    parts (std::move (other_.parts))
{
    memset (&other_.source_rid, 0, sizeof (other_.source_rid));
    other_.parts.clear ();
}

queued_spot_subscribe_message_t &queued_spot_subscribe_message_t::operator= (
  queued_spot_subscribe_message_t &&other_) noexcept
{
    if (this == &other_)
        return *this;

    for (size_t i = 0; i < parts.size (); ++i)
        zlink_msg_close (&parts[i]);

    source_rid = other_.source_rid;
    topic = std::move (other_.topic);
    parts = std::move (other_.parts);

    memset (&other_.source_rid, 0, sizeof (other_.source_rid));
    other_.parts.clear ();
    return *this;
}

spot_subscribe_dispatch_queue_t::spot_subscribe_dispatch_queue_t () :
    closed (false)
{
}

queued_routed_message_t::queued_routed_message_t () :
    request_seq (0)
{
    memset (&source_rid, 0, sizeof (source_rid));
    memset (&spot_rid, 0, sizeof (spot_rid));
}

queued_routed_message_t::~queued_routed_message_t ()
{
    for (size_t i = 0; i < parts.size (); ++i)
        zlink_msg_close (&parts[i]);
}

queued_routed_message_t::queued_routed_message_t (
  queued_routed_message_t &&other_) noexcept :
    source_rid (other_.source_rid),
    spot_rid (other_.spot_rid),
    request_seq (other_.request_seq),
    parts (std::move (other_.parts))
{
    memset (&other_.source_rid, 0, sizeof (other_.source_rid));
    memset (&other_.spot_rid, 0, sizeof (other_.spot_rid));
    other_.request_seq = 0;
    other_.parts.clear ();
}

queued_routed_message_t &queued_routed_message_t::operator= (
  queued_routed_message_t &&other_) noexcept
{
    if (this == &other_)
        return *this;

    for (size_t i = 0; i < parts.size (); ++i)
        zlink_msg_close (&parts[i]);

    source_rid = other_.source_rid;
    spot_rid = other_.spot_rid;
    request_seq = other_.request_seq;
    parts = std::move (other_.parts);

    memset (&other_.source_rid, 0, sizeof (other_.source_rid));
    memset (&other_.spot_rid, 0, sizeof (other_.spot_rid));
    other_.request_seq = 0;
    other_.parts.clear ();
    return *this;
}

routed_message_queue_t::routed_message_queue_t () :
    pending_count (0),
    disconnected (false)
{
}

spot_request_reply_request_state_t::spot_request_reply_request_state_t ()
{
}

spot_request_reply_recv_state_t::spot_request_reply_recv_state_t () :
    routed_recv_socket (NULL),
    request_handler (NULL),
    request_handler_userdata (NULL)
{
}

spot_request_reply_completion_state_t::spot_request_reply_completion_state_t () :
    pending_channel_requests (0)
{
}

spot_request_reply_state_t::spot_request_reply_state_t (void *owner_) :
    owner (owner_)
{
}

router_spot_request_reply_request_state_t::
  router_spot_request_reply_request_state_t ()
{
}

router_spot_request_reply_state_t::router_spot_request_reply_state_t (
  void *owner_) :
    owner (owner_)
{
}

std::mutex g_spot_request_reply_index_mutex;
spot_state_identity_index_t g_spot_state_identity_index;
router_state_identity_index_t g_router_state_identity_index;
thread_local zlink_routing_id_t g_spot_recv_source_rid;
thread_local zlink_routing_id_t g_spot_recv_spot_rid;

int validate_request_parts (zlink_msg_t *parts_, size_t part_count_)
{
    if ((!parts_ && part_count_ > 0) || part_count_ == 0) {
        errno = EFAULT;
        return -1;
    }

    return 0;
}

int init_buffer_frame (zlink_msg_t *msg_, const void *data_, size_t size_)
{
    if (!msg_) {
        errno = EFAULT;
        return -1;
    }
    if (zlink_msg_init_size (msg_, size_) != 0)
        return -1;
    if (size_ > 0 && data_)
        memcpy (zlink_msg_data (msg_), data_, size_);
    return 0;
}
}
}
