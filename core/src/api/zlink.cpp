/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"
#define ZLINK_TYPE_UNSAFE

#include "utils/macros.hpp"
#include "utils/random.hpp"

#include <cstdio>

#if !defined ZLINK_HAVE_WINDOWS
#include <unistd.h>
#ifdef ZLINK_HAVE_VXWORKS
#include <strings.h>
#endif
#endif

// XSI vector I/O
#if defined ZLINK_HAVE_UIO
#include <sys/uio.h>
#else
struct iovec
{
    void *iov_base;
    size_t iov_len;
};
#endif

#include <string.h>
#include <stdlib.h>
#include <new>
#include <climits>
#include <algorithm>
#include <string>
#include <thread>
#include <vector>
#include <mutex>

#include "sockets/proxy.hpp"
#include "sockets/socket_base.hpp"
#include "services/discovery/registry.hpp"
#include "services/discovery/discovery_protocol.hpp"
#include "services/discovery/discovery.hpp"
#include "services/discovery/discovery_access.hpp"
#include "services/common/service_public_api.hpp"
#include "services/gateway/gateway.hpp"
#include "services/gateway/gateway_access.hpp"
#include "services/spot/spot_dispatch_internal.hpp"
#include "services/spot/spot_node.hpp"
#include "services/spot/spot_node_access.hpp"
#include "services/spot/spot_internal_receiver.hpp"
#include "services/spot/spot_pub.hpp"
#include "services/spot/spot_sub.hpp"
#include "api/zlink_testing.hpp"
#include "utils/mutex.hpp"
#include "utils/stdint.hpp"
#include "utils/config.hpp"
#include "utils/likely.hpp"
#include "utils/clock.hpp"
#include "core/ctx.hpp"
#include "utils/err.hpp"
#include "core/msg.hpp"
#include "core/recv_internal.hpp"
#include "core/send_internal.hpp"
#include "core/socket_poller.hpp"
#include "utils/fd.hpp"
#include "protocol/metadata.hpp"
#include "core/timers.hpp"
#include "utils/ip.hpp"
#include "core/address.hpp"

#ifdef ZLINK_HAVE_PPOLL
#include "utils/polling_util.hpp"
#include <sys/select.h>
#endif

//  Compile time check whether msg_t fits into zlink_msg_t.
typedef char
  check_msg_t_size[sizeof (zlink::msg_t) == sizeof (zlink_msg_t) ? 1 : -1];

//  Forward declarations for internal API functions
int zlink_msg_init_buffer (zlink_msg_t *msg_, const void *buf_, size_t size_);
int zlink_ctx_set_ext (void *ctx_, int option_, const void *optval_, size_t optvallen_);
int zlink_recv_spot_handler (void *s_,
                             zlink_subscribe_handler_fn handler_,
                             void *userdata_);
int zlink_set_subscription (void *handle_, const char *filter_);
int zlink_unset_subscription (void *handle_, const char *filter_);

static bool is_public_ctx_set_option (int option_)
{
    switch (option_) {
        case ZLINK_IO_THREADS:
        case ZLINK_MAX_SOCKETS:
        case ZLINK_THREAD_PRIORITY:
        case ZLINK_THREAD_SCHED_POLICY:
        case ZLINK_MAX_MSGSZ:
        case ZLINK_THREAD_AFFINITY_CPU_ADD:
        case ZLINK_THREAD_AFFINITY_CPU_REMOVE:
        case ZLINK_THREAD_NAME_PREFIX:
        case ZLINK_CTX_OPT_BLOCKY:
            return true;
        default:
            return false;
    }
}

static bool is_public_ctx_get_option (int option_)
{
    return option_ == ZLINK_IO_THREADS || option_ == ZLINK_MAX_SOCKETS
           || option_ == ZLINK_SOCKET_LIMIT || option_ == ZLINK_THREAD_PRIORITY
           || option_ == ZLINK_THREAD_SCHED_POLICY
           || option_ == ZLINK_MAX_MSGSZ || option_ == ZLINK_MSG_T_SIZE
           || option_ == ZLINK_THREAD_AFFINITY_CPU_ADD
           || option_ == ZLINK_THREAD_AFFINITY_CPU_REMOVE
           || option_ == ZLINK_THREAD_NAME_PREFIX
           || option_ == ZLINK_CTX_OPT_BLOCKY;
}

static bool frame_has_more (const zlink_msg_t &msg_)
{
    return (reinterpret_cast<const zlink::msg_t *> (&msg_)->flags ()
            & zlink::msg_t::more)
           != 0;
}
int zlink_xpub_recv (void *s_,
                     zlink_routing_id_t *source_rid_out_,
                     int *subscribed_out_,
                     char *topic_id_out_,
                     size_t *topic_id_len_,
                     zlink_send_flags_t flags_);

//  Default discard handlers – used as sentinels so that sockets created
//  without an explicit handler silently drop incoming messages.
static void discard_socket_parts (const zlink_routing_id_t *,
                                  zlink_msg_t *parts_,
                                  size_t part_count_,
                                  void *)
{
    zlink_multipart_close (parts_, part_count_);
}

static void discard_spot_parts (const zlink_routing_id_t *,
                                const char *,
                                size_t,
                                zlink_msg_t *parts_,
                                size_t part_count_,
                                void *)
{
    zlink_multipart_close (parts_, part_count_);
}

static bool is_valid_pubsub_filter (const char *filter_,
                                    std::string *raw_filter_out_,
                                    bool *is_pattern_out_)
{
    if (!filter_ || filter_[0] == '\0')
        return false;

    const size_t len = strlen (filter_);
    if (len == 0 || len > 255)
        return false;

    const char *star = strchr (filter_, '*');
    if (!star) {
        if (raw_filter_out_)
            *raw_filter_out_ = std::string (filter_, len);
        if (is_pattern_out_)
            *is_pattern_out_ = false;
        return true;
    }

    if (star != filter_ + len - 1 || len < 2)
        return false;

    if (strchr (star + 1, '*'))
        return false;

    if (raw_filter_out_)
        *raw_filter_out_ = std::string (filter_, len - 1);
    if (is_pattern_out_)
        *is_pattern_out_ = true;
    return true;
}

static int copy_topic_to_output (const char *topic_data_,
                                 size_t topic_size_,
                                 char *topic_id_out_,
                                 size_t *topic_id_len_out_)
{
    if (!topic_id_len_out_) {
        errno = EFAULT;
        return -1;
    }

    if (!topic_id_out_ && *topic_id_len_out_ != 0) {
        errno = EFAULT;
        return -1;
    }

    if (*topic_id_len_out_ < topic_size_) {
        *topic_id_len_out_ = topic_size_;
        errno = EMSGSIZE;
        return -1;
    }

    if (topic_id_out_ && topic_size_ > 0)
        memcpy (topic_id_out_, topic_data_, topic_size_);
    *topic_id_len_out_ = topic_size_;
    return 0;
}

void zlink_version (int *major_, int *minor_, int *patch_)
{
    *major_ = ZLINK_VERSION_MAJOR;
    *minor_ = ZLINK_VERSION_MINOR;
    *patch_ = ZLINK_VERSION_PATCH;
}

const char *zlink_strerror (int errnum_)
{
    return zlink::errno_to_string (errnum_);
}

int zlink_errno (void)
{
    return errno;
}

void zlink_monitor_ignore_handler (const zlink_monitor_event_t *,
                                   void *userdata_)
{
    (void) userdata_;
}

//  New context API

void *zlink_ctx_new (void)
{
    if (!zlink::initialize_network ()) {
        return NULL;
    }

    zlink::ctx_t *ctx = new (std::nothrow) zlink::ctx_t;
    if (ctx) {
        if (!ctx->valid ()) {
            delete ctx;
            return NULL;
        }
    }
    return ctx;
}

int zlink_ctx_term (void *ctx_)
{
    if (!ctx_ || !(static_cast<zlink::ctx_t *> (ctx_))->check_tag ()) {
        errno = EFAULT;
        return -1;
    }

    const int rc = (static_cast<zlink::ctx_t *> (ctx_))->terminate ();
    const int en = errno;

    if (!rc || en != EINTR) {
        zlink::shutdown_network ();
    }

    errno = en;
    return rc;
}

int zlink_ctx_shutdown (void *ctx_)
{
    if (!ctx_ || !(static_cast<zlink::ctx_t *> (ctx_))->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return (static_cast<zlink::ctx_t *> (ctx_))->shutdown ();
}

int zlink_ctx_set (void *ctx_, zlink_ctx_option_t option_, int optval_)
{
    if (!is_public_ctx_set_option (option_)) {
        errno = EINVAL;
        return -1;
    }
    return zlink_ctx_set_ext (ctx_, option_, &optval_, sizeof (int));
}

int zlink_ctx_set_ext (void *ctx_,
                     int option_,
                     const void *optval_,
                     size_t optvallen_)
{
    if (!ctx_ || !(static_cast<zlink::ctx_t *> (ctx_))->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return (static_cast<zlink::ctx_t *> (ctx_))
      ->set (option_, optval_, optvallen_);
}

int zlink_ctx_get (void *ctx_, zlink_ctx_option_t option_)
{
    if (!is_public_ctx_get_option (option_)) {
        errno = EINVAL;
        return -1;
    }
    if (!ctx_ || !(static_cast<zlink::ctx_t *> (ctx_))->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return (static_cast<zlink::ctx_t *> (ctx_))->get (option_);
}

// Sockets

struct socket_handle_t
{
    zlink::socket_base_t *socket;
};

typedef int (*monitor_snapshot_provider_fn) (void *subject_,
                                             zlink_monitor_snapshot_t *out_);

struct monitor_handler_state_t
{
    monitor_handler_state_t (zlink::socket_base_t *socket_, bool service_) :
        socket (socket_),
        socket_handler (NULL),
        service_handler (NULL),
        socket_handler_userdata (NULL),
        service_handler_userdata (NULL),
        snapshot_provider (NULL),
        snapshot_subject (NULL),
        stop (false),
        callback_depth (0),
        close_requested (false),
        service (service_)
    {
    }

    zlink::socket_base_t *socket;
    std::atomic<zlink_monitor_handler_fn> socket_handler;
    std::atomic<zlink_service_monitor_handler_fn> service_handler;
    std::atomic<void *> socket_handler_userdata;
    std::atomic<void *> service_handler_userdata;
    std::atomic<monitor_snapshot_provider_fn> snapshot_provider;
    std::atomic<void *> snapshot_subject;
    std::atomic<bool> stop;
    std::atomic<int> callback_depth;
    std::atomic<bool> close_requested;
    bool service;
    std::thread worker;
};

struct monitor_handler_registry_t
{
    zlink::mutex_t sync;
    std::map<zlink::socket_base_t *, monitor_handler_state_t *> handlers;
};

struct spot_node_handler_entry_t
{
    zlink_subscribe_handler_fn handler;
    void *userdata;
};

struct spot_node_handler_registry_t
{
    zlink::mutex_t sync;
    std::map<zlink::spot_node_t *, spot_node_handler_entry_t> handlers;
};

struct spot_sub_handler_entry_t
{
    zlink_subscribe_handler_fn handler;
    void *userdata;
};

struct spot_sub_handler_registry_t
{
    zlink::mutex_t sync;
    std::map<zlink::spot_sub_t *, spot_sub_handler_entry_t> handlers;
};

struct spot_handle_t
{
    spot_handle_t () :
        tag (0x1e6700dc),
        node (NULL),
        pub (NULL),
        sub (NULL),
        handler (NULL),
        handler_userdata (NULL)
    {
    }

    bool check_tag () const { return tag == 0x1e6700dc; }

    uint32_t tag;
    zlink::service_public_api_guard_t public_api;
    zlink::spot_node_t *node;
    zlink::spot_pub_t *pub;
    zlink::spot_sub_t *sub;
    zlink_subscribe_handler_fn handler;
    void *handler_userdata;
    zlink::spot_node_t::pub_defaults_t pending_pub_defaults;
    zlink::spot_node_t::sub_defaults_t pending_sub_defaults;
};

struct service_mode_state_t
{
    service_mode_state_t () :
        receive_callback_active (false),
        send_ready_active (false),
        pollin_refs (0),
        pollout_refs (0)
    {
    }

    bool receive_callback_active;
    bool send_ready_active;
    int pollin_refs;
    int pollout_refs;
};

struct gateway_mode_registry_t
{
    zlink::mutex_t sync;
    std::map<void *, service_mode_state_t> states;
};

struct spot_node_mode_registry_t
{
    zlink::mutex_t sync;
    std::map<void *, service_mode_state_t> states;
};

struct spot_mode_registry_t
{
    zlink::mutex_t sync;
    std::map<void *, service_mode_state_t> states;
};

enum poller_subject_kind_t
{
    poller_subject_none = 0,
    poller_subject_gateway,
    poller_subject_spot_pub,
    poller_subject_spot_sub,
    poller_subject_spot_node_pub,
    poller_subject_spot_node_sub
};

struct poller_registration_t
{
    poller_registration_t () :
        socket (NULL),
        subject (NULL),
        subject_kind (poller_subject_none),
        events (0)
    {
    }

    void *socket;
    void *subject;
    poller_subject_kind_t subject_kind;
    short events;
};

struct poller_handle_t
{
    poller_handle_t () : tag (0x706f6c6c) {}

    bool check_tag () const { return tag == 0x706f6c6c; }

    uint32_t tag;
    zlink::socket_poller_t poller;
    std::vector<poller_registration_t> registrations;
};

static thread_local void *g_current_spot_dispatch_handle = NULL;
static thread_local bool g_current_spot_dispatch_is_node = false;
static thread_local monitor_handler_state_t *g_current_monitor_handler_state =
  NULL;

static monitor_handler_registry_t &monitor_handler_registry ()
{
    static monitor_handler_registry_t registry;
    return registry;
}

void *zlink::current_spot_dispatch_handle ()
{
    return g_current_spot_dispatch_handle;
}

bool zlink::current_spot_dispatch_is_node ()
{
    return g_current_spot_dispatch_is_node;
}

namespace zlink
{
void *current_monitor_dispatch_handle ()
{
    return g_current_monitor_handler_state
             ? static_cast<void *> (g_current_monitor_handler_state->socket)
             : NULL;
}
}

static spot_node_handler_registry_t &spot_node_handler_registry ()
{
    static spot_node_handler_registry_t registry;
    return registry;
}

static spot_sub_handler_registry_t &spot_sub_handler_registry ()
{
    static spot_sub_handler_registry_t registry;
    return registry;
}

static gateway_mode_registry_t &gateway_mode_registry ()
{
    static gateway_mode_registry_t registry;
    return registry;
}

static spot_node_mode_registry_t &spot_node_mode_registry ()
{
    static spot_node_mode_registry_t registry;
    return registry;
}

static spot_mode_registry_t &spot_mode_registry ()
{
    static spot_mode_registry_t registry;
    return registry;
}

static int transition_service_to_callback_mode (service_mode_state_t *state_);
static void revert_service_receive_callback_mode (service_mode_state_t *state_);
static int activate_service_send_ready_mode (service_mode_state_t *state_);
static void revert_service_send_ready_mode (service_mode_state_t *state_);
static int ensure_service_recv_model (const service_mode_state_t *state_);
static int increment_service_poller_refs (service_mode_state_t *state_,
                                          short events_);
static void decrement_service_poller_refs (service_mode_state_t *state_,
                                           short events_);
static void *open_discovery_service_monitor_internal (
  void *discovery_,
  zlink_discovery_monitor_event_mask_t events_,
  zlink_service_monitor_handler_fn handler_,
  void *userdata_);
static void *open_gateway_service_monitor_internal (
  void *gateway_,
  zlink_gateway_monitor_event_mask_t events_,
  zlink_service_monitor_handler_fn handler_,
  void *userdata_);
static void *open_spot_node_service_monitor_internal (
  void *node_,
  zlink_spot_role_t role_,
  zlink_spot_monitor_event_mask_t events_,
  zlink_service_monitor_handler_fn handler_,
  void *userdata_);
static void *open_spot_service_monitor_internal (
  void *spot_,
  zlink_spot_role_t role_,
  zlink_spot_monitor_event_mask_t events_,
  zlink_service_monitor_handler_fn handler_,
  void *userdata_);

static void close_spot_parts (zlink_msg_t *parts_, size_t part_count_)
{
    if (!parts_)
        return;
    for (size_t i = 0; i < part_count_; ++i)
        zlink_msg_close (&parts_[i]);
}

static inline poller_handle_t *as_poller_handle (void *poller_)
{
    if (!poller_) {
        errno = EFAULT;
        return NULL;
    }

    poller_handle_t *poller = static_cast<poller_handle_t *> (poller_);
    if (!poller->check_tag ()) {
        errno = EFAULT;
        return NULL;
    }
    return poller;
}

static bool is_registered_gateway_handle (void *gateway_)
{
    if (!gateway_)
        return false;
    gateway_mode_registry_t &registry = gateway_mode_registry ();
    zlink::scoped_lock_t lock (registry.sync);
    return registry.states.find (gateway_) != registry.states.end ();
}

static bool is_registered_spot_node_handle (void *node_)
{
    if (!node_)
        return false;
    spot_node_mode_registry_t &registry = spot_node_mode_registry ();
    zlink::scoped_lock_t lock (registry.sync);
    return registry.states.find (node_) != registry.states.end ();
}

static bool is_registered_spot_handle (void *spot_)
{
    if (!spot_)
        return false;
    spot_mode_registry_t &registry = spot_mode_registry ();
    zlink::scoped_lock_t lock (registry.sync);
    return registry.states.find (spot_) != registry.states.end ();
}

static zlink::spot_pub_t *as_spot_pub_side_handle (void *handle_)
{
    if (!handle_)
        return NULL;
    zlink::spot_pub_t *pub = static_cast<zlink::spot_pub_t *> (handle_);
    return pub->check_tag () ? pub : NULL;
}

static zlink::spot_sub_t *as_spot_sub_side_handle (void *handle_)
{
    if (!handle_)
        return NULL;
    zlink::spot_sub_t *sub = static_cast<zlink::spot_sub_t *> (handle_);
    return sub->check_tag () ? sub : NULL;
}

static int spot_node_transition_to_callback_mode (zlink::spot_node_t *node_)
{
    if (!node_) {
        errno = EFAULT;
        return -1;
    }
    spot_node_mode_registry_t &registry = spot_node_mode_registry ();
    zlink::scoped_lock_t lock (registry.sync);
    return transition_service_to_callback_mode (&registry.states[node_]);
}

static int spot_transition_to_callback_mode (spot_handle_t *spot_)
{
    if (!spot_) {
        errno = EFAULT;
        return -1;
    }
    spot_mode_registry_t &registry = spot_mode_registry ();
    zlink::scoped_lock_t lock (registry.sync);
    return transition_service_to_callback_mode (&registry.states[spot_]);
}

static void gateway_revert_callback_transition (zlink::gateway_t *gateway_)
{
    if (!gateway_)
        return;
    gateway_mode_registry_t &registry = gateway_mode_registry ();
    zlink::scoped_lock_t lock (registry.sync);
    revert_service_receive_callback_mode (&registry.states[gateway_]);
}

static int gateway_require_recv_model (zlink::gateway_t *gateway_)
{
    if (!gateway_) {
        errno = EFAULT;
        return -1;
    }
    gateway_mode_registry_t &registry = gateway_mode_registry ();
    zlink::scoped_lock_t lock (registry.sync);
    return ensure_service_recv_model (&registry.states[gateway_]);
}

static int spot_node_require_recv_model (zlink::spot_node_t *node_)
{
    if (!node_) {
        errno = EFAULT;
        return -1;
    }
    spot_node_mode_registry_t &registry = spot_node_mode_registry ();
    zlink::scoped_lock_t lock (registry.sync);
    return ensure_service_recv_model (&registry.states[node_]);
}

static void spot_node_revert_callback_transition (zlink::spot_node_t *node_)
{
    if (!node_)
        return;
    spot_node_mode_registry_t &registry = spot_node_mode_registry ();
    zlink::scoped_lock_t lock (registry.sync);
    revert_service_receive_callback_mode (&registry.states[node_]);
}

static int spot_require_recv_model (spot_handle_t *spot_)
{
    if (!spot_) {
        errno = EFAULT;
        return -1;
    }
    spot_mode_registry_t &registry = spot_mode_registry ();
    zlink::scoped_lock_t lock (registry.sync);
    return ensure_service_recv_model (&registry.states[spot_]);
}

static void spot_revert_callback_transition (spot_handle_t *spot_)
{
    if (!spot_)
        return;
    spot_mode_registry_t &registry = spot_mode_registry ();
    zlink::scoped_lock_t lock (registry.sync);
    revert_service_receive_callback_mode (&registry.states[spot_]);
}

static int increment_gateway_poller_ref (zlink::gateway_t *gateway_)
{
    if (!gateway_) {
        errno = EFAULT;
        return -1;
    }
    gateway_mode_registry_t &registry = gateway_mode_registry ();
    zlink::scoped_lock_t lock (registry.sync);
    return increment_service_poller_refs (&registry.states[gateway_],
                                          ZLINK_POLLIN | ZLINK_POLLOUT);
}

static int increment_gateway_poller_ref (zlink::gateway_t *gateway_, short events_)
{
    if (!gateway_) {
        errno = EFAULT;
        return -1;
    }
    gateway_mode_registry_t &registry = gateway_mode_registry ();
    zlink::scoped_lock_t lock (registry.sync);
    return increment_service_poller_refs (&registry.states[gateway_], events_);
}

static int increment_spot_node_poller_ref (zlink::spot_node_t *node_)
{
    if (!node_) {
        errno = EFAULT;
        return -1;
    }
    spot_node_mode_registry_t &registry = spot_node_mode_registry ();
    zlink::scoped_lock_t lock (registry.sync);
    return increment_service_poller_refs (&registry.states[node_],
                                          ZLINK_POLLIN | ZLINK_POLLOUT);
}

static int increment_spot_node_poller_ref (zlink::spot_node_t *node_,
                                           short events_)
{
    if (!node_) {
        errno = EFAULT;
        return -1;
    }
    spot_node_mode_registry_t &registry = spot_node_mode_registry ();
    zlink::scoped_lock_t lock (registry.sync);
    return increment_service_poller_refs (&registry.states[node_], events_);
}

static int increment_spot_poller_ref (spot_handle_t *spot_)
{
    if (!spot_) {
        errno = EFAULT;
        return -1;
    }
    spot_mode_registry_t &registry = spot_mode_registry ();
    zlink::scoped_lock_t lock (registry.sync);
    return increment_service_poller_refs (&registry.states[spot_],
                                          ZLINK_POLLIN | ZLINK_POLLOUT);
}

static int increment_spot_poller_ref (spot_handle_t *spot_, short events_)
{
    if (!spot_) {
        errno = EFAULT;
        return -1;
    }
    spot_mode_registry_t &registry = spot_mode_registry ();
    zlink::scoped_lock_t lock (registry.sync);
    return increment_service_poller_refs (&registry.states[spot_], events_);
}

static void decrement_gateway_poller_ref (zlink::gateway_t *gateway_)
{
    if (!gateway_)
        return;
    gateway_mode_registry_t &registry = gateway_mode_registry ();
    zlink::scoped_lock_t lock (registry.sync);
    decrement_service_poller_refs (&registry.states[gateway_],
                                   ZLINK_POLLIN | ZLINK_POLLOUT);
}

static void decrement_gateway_poller_ref (zlink::gateway_t *gateway_,
                                          short events_)
{
    if (!gateway_)
        return;
    gateway_mode_registry_t &registry = gateway_mode_registry ();
    zlink::scoped_lock_t lock (registry.sync);
    decrement_service_poller_refs (&registry.states[gateway_], events_);
}

static void decrement_spot_node_poller_ref (zlink::spot_node_t *node_)
{
    if (!node_)
        return;
    spot_node_mode_registry_t &registry = spot_node_mode_registry ();
    zlink::scoped_lock_t lock (registry.sync);
    decrement_service_poller_refs (&registry.states[node_],
                                   ZLINK_POLLIN | ZLINK_POLLOUT);
}

static void decrement_spot_node_poller_ref (zlink::spot_node_t *node_,
                                            short events_)
{
    if (!node_)
        return;
    spot_node_mode_registry_t &registry = spot_node_mode_registry ();
    zlink::scoped_lock_t lock (registry.sync);
    decrement_service_poller_refs (&registry.states[node_], events_);
}

static void decrement_spot_poller_ref (spot_handle_t *spot_)
{
    if (!spot_)
        return;
    spot_mode_registry_t &registry = spot_mode_registry ();
    zlink::scoped_lock_t lock (registry.sync);
    decrement_service_poller_refs (&registry.states[spot_],
                                   ZLINK_POLLIN | ZLINK_POLLOUT);
}

static void decrement_spot_poller_ref (spot_handle_t *spot_, short events_)
{
    if (!spot_)
        return;
    spot_mode_registry_t &registry = spot_mode_registry ();
    zlink::scoped_lock_t lock (registry.sync);
    decrement_service_poller_refs (&registry.states[spot_], events_);
}

static void register_gateway_mode_state (zlink::gateway_t *gateway_)
{
    if (!gateway_)
        return;
    gateway_mode_registry_t &registry = gateway_mode_registry ();
    zlink::scoped_lock_t lock (registry.sync);
    registry.states[gateway_] = service_mode_state_t ();
}

static void register_spot_node_mode_state (zlink::spot_node_t *node_)
{
    if (!node_)
        return;
    spot_node_mode_registry_t &registry = spot_node_mode_registry ();
    zlink::scoped_lock_t lock (registry.sync);
    registry.states[node_] = service_mode_state_t ();
}

static void register_spot_mode_state (spot_handle_t *spot_)
{
    if (!spot_)
        return;
    spot_mode_registry_t &registry = spot_mode_registry ();
    zlink::scoped_lock_t lock (registry.sync);
    registry.states[spot_] = service_mode_state_t ();
}

static void erase_gateway_mode_state (zlink::gateway_t *gateway_)
{
    if (!gateway_)
        return;
    gateway_mode_registry_t &registry = gateway_mode_registry ();
    zlink::scoped_lock_t lock (registry.sync);
    registry.states.erase (gateway_);
}

static void erase_spot_node_mode_state (zlink::spot_node_t *node_)
{
    if (!node_)
        return;
    spot_node_mode_registry_t &registry = spot_node_mode_registry ();
    zlink::scoped_lock_t lock (registry.sync);
    registry.states.erase (node_);
}

static void erase_spot_mode_state (spot_handle_t *spot_)
{
    if (!spot_)
        return;
    spot_mode_registry_t &registry = spot_mode_registry ();
    zlink::scoped_lock_t lock (registry.sync);
    registry.states.erase (spot_);
}

static int transition_service_to_callback_mode (service_mode_state_t *state_)
{
    if (!state_) {
        errno = EFAULT;
        return -1;
    }
    if (state_->receive_callback_active) {
        errno = EBUSY;
        return -1;
    }
    if (state_->pollin_refs > 0) {
        errno = EBUSY;
        return -1;
    }
    state_->receive_callback_active = true;
    return 0;
}

static void revert_service_receive_callback_mode (service_mode_state_t *state_)
{
    if (!state_)
        return;
    state_->receive_callback_active = false;
}

static int activate_service_send_ready_mode (service_mode_state_t *state_)
{
    if (!state_) {
        errno = EFAULT;
        return -1;
    }
    if (!state_->send_ready_active && state_->pollout_refs > 0) {
        errno = EBUSY;
        return -1;
    }
    state_->send_ready_active = true;
    return 0;
}

static void revert_service_send_ready_mode (service_mode_state_t *state_)
{
    if (!state_)
        return;
    state_->send_ready_active = false;
}

static int ensure_service_recv_model (const service_mode_state_t *state_)
{
    if (!state_) {
        errno = EFAULT;
        return -1;
    }
    if (state_->receive_callback_active) {
        errno = EBUSY;
        return -1;
    }
    return 0;
}

static int increment_service_poller_refs (service_mode_state_t *state_,
                                          short events_)
{
    if (!state_) {
        errno = EFAULT;
        return -1;
    }
    if ((events_ & ZLINK_POLLIN) != 0 && state_->receive_callback_active) {
        errno = EBUSY;
        return -1;
    }
    if ((events_ & ZLINK_POLLOUT) != 0 && state_->send_ready_active) {
        errno = EBUSY;
        return -1;
    }
    if ((events_ & ZLINK_POLLIN) != 0)
        ++state_->pollin_refs;
    if ((events_ & ZLINK_POLLOUT) != 0)
        ++state_->pollout_refs;
    return 0;
}

static void decrement_service_poller_refs (service_mode_state_t *state_,
                                           short events_)
{
    if (!state_)
        return;
    if ((events_ & ZLINK_POLLIN) != 0 && state_->pollin_refs > 0)
        --state_->pollin_refs;
    if ((events_ & ZLINK_POLLOUT) != 0 && state_->pollout_refs > 0)
        --state_->pollout_refs;
}

static int validate_recv_flags (int flags_)
{
    if (flags_ != 0 && flags_ != ZLINK_DONTWAIT) {
        errno = ENOTSUP;
        return -1;
    }
    return 0;
}

static int validate_gateway_poller_events (short events_)
{
    if ((events_ & ~(ZLINK_POLLIN | ZLINK_POLLOUT)) != 0 || events_ == 0) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

static int validate_spot_pub_poller_events (short events_)
{
    if (events_ != ZLINK_POLLOUT) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

static int validate_spot_sub_poller_events (short events_)
{
    if (events_ != ZLINK_POLLIN) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

static int validate_monitor_poller_events (short events_)
{
    if (events_ != ZLINK_POLLIN) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

static int recv_gateway_parts (zlink::socket_base_t *socket_,
                               zlink_routing_id_t *source_rid_out_,
                               zlink_msg_t **parts_out_,
                               size_t *part_count_out_,
                               int flags_)
{
    if (!socket_ || !parts_out_ || !part_count_out_) {
        errno = EINVAL;
        return -1;
    }

    *parts_out_ = NULL;
    *part_count_out_ = 0;
    if (source_rid_out_)
        memset (source_rid_out_, 0, sizeof (*source_rid_out_));

    std::vector<zlink_msg_t> frames;
    zlink_msg_t rid_frame;
    zlink_msg_init (&rid_frame);
    if (socket_->recv (reinterpret_cast<zlink::msg_t *> (&rid_frame), flags_)
        < 0) {
        zlink_msg_close (&rid_frame);
        return -1;
    }
    frames.push_back (rid_frame);

    while (frame_has_more (frames.back ())) {
        zlink_msg_t frame;
        zlink_msg_init (&frame);
        if (socket_->recv (reinterpret_cast<zlink::msg_t *> (&frame), 0) < 0) {
            zlink_msg_close (&frame);
            close_spot_parts (frames.data (), frames.size ());
            return -1;
        }
        frames.push_back (frame);
    }

    if (frames.empty ()) {
        errno = EPROTO;
        return -1;
    }

    const size_t routing_id_size = zlink_msg_size (&frames[0]);
    if (routing_id_size == 0 || routing_id_size > 255) {
        close_spot_parts (frames.data (), frames.size ());
        errno = EPROTO;
        return -1;
    }

    if (source_rid_out_) {
        source_rid_out_->size = static_cast<uint8_t> (routing_id_size);
        memcpy (source_rid_out_->data, zlink_msg_data (&frames[0]),
                routing_id_size);
    }

    const size_t payload_count = frames.size () - 1;
    if (payload_count == 0) {
        zlink_msg_close (&frames[0]);
        return 0;
    }

    zlink_msg_t *payload = static_cast<zlink_msg_t *> (
      malloc (payload_count * sizeof (zlink_msg_t)));
    if (!payload) {
        close_spot_parts (frames.data (), frames.size ());
        errno = ENOMEM;
        return -1;
    }
    memset (payload, 0, payload_count * sizeof (zlink_msg_t));

    for (size_t i = 0; i < payload_count; ++i) {
        zlink::msg_t *dst = reinterpret_cast<zlink::msg_t *> (&payload[i]);
        if (dst->init () != 0
            || dst->move (
                 *reinterpret_cast<zlink::msg_t *> (&frames[i + 1]))
                 != 0) {
            for (size_t j = 0; j <= i; ++j)
                zlink_msg_close (&payload[j]);
            free (payload);
            close_spot_parts (frames.data (), frames.size ());
            errno = EFAULT;
            return -1;
        }
    }

    zlink_msg_close (&frames[0]);
    *parts_out_ = payload;
    *part_count_out_ = payload_count;
    return 0;
}

static void *open_spot_service_monitor (
  void *monitor_,
  zlink_service_monitor_handler_fn handler_,
  monitor_snapshot_provider_fn snapshot_provider_,
  void *snapshot_subject_,
  void *userdata_);
static int socket_monitor_snapshot_provider (void *subject_,
                                             zlink_monitor_snapshot_t *out_);

static int recv_socket_monitor_event_unchecked (void *monitor_socket_,
                                                zlink_monitor_event_t *event_,
                                                int flags_);
static int recv_service_monitor_event_unchecked (void *monitor_,
                                                 zlink_service_event_t *event_,
                                                 int flags_);
static void spot_node_sub_handler_adapter (
  const zlink_routing_id_t *source_rid_,
  const char *topic_,
  size_t topic_len_,
  zlink_msg_t *parts_,
  size_t part_count_,
  void *userdata_);
static void spot_sub_handler_adapter (const zlink_routing_id_t *source_rid_,
                                      const char *topic_,
                                      size_t topic_len_,
                                      zlink_msg_t *parts_,
                                      size_t part_count_,
                                      void *userdata_);

static void stop_monitor_handler_state (monitor_handler_state_t *state_)
{
    if (!state_)
        return;

    state_->stop.store (true, std::memory_order_release);
    if (state_->worker.joinable ())
        state_->worker.join ();
    delete state_;
}

static void erase_monitor_handler_state (zlink::socket_base_t *socket_,
                                         monitor_handler_state_t *state_)
{
    if (!socket_ || !state_)
        return;

    monitor_handler_registry_t &registry = monitor_handler_registry ();
    zlink::scoped_lock_t lock (registry.sync);
    std::map<zlink::socket_base_t *, monitor_handler_state_t *>::iterator it =
      registry.handlers.find (socket_);
    if (it != registry.handlers.end () && it->second == state_)
        registry.handlers.erase (it);
}

static void finalize_monitor_handler_self_close (monitor_handler_state_t *state_)
{
    if (!state_)
        return;

    zlink::socket_base_t *socket = state_->socket;
    state_->stop.store (true, std::memory_order_release);
    erase_monitor_handler_state (socket, state_);
    state_->socket = NULL;
    if (socket) {
        socket->stop ();
        socket->close ();
    }
    if (state_->worker.joinable ())
        state_->worker.detach ();
    delete state_;
}

static void monitor_handler_worker (monitor_handler_state_t *state_)
{
    if (!state_ || !state_->socket)
        return;

    void *monitor_socket = static_cast<void *> (state_->socket);
    while (!state_->stop.load (std::memory_order_acquire)) {
        bool drained = false;
        while (!state_->stop.load (std::memory_order_acquire)) {
            if (!state_->service) {
                zlink_monitor_event_t event;
                const int rc = recv_socket_monitor_event_unchecked (
                  monitor_socket, &event, ZLINK_DONTWAIT);
                if (rc != 0)
                    break;
                drained = true;

                zlink_monitor_handler_fn handler =
                  state_->socket_handler.load (std::memory_order_acquire);
                if (handler) {
                    monitor_handler_state_t *previous =
                      g_current_monitor_handler_state;
                    g_current_monitor_handler_state = state_;
                    state_->callback_depth.fetch_add (
                      1, std::memory_order_acq_rel);
                    handler (&event, state_->socket_handler_userdata.load (std::memory_order_acquire));
                    const int depth_after =
                      state_->callback_depth.fetch_sub (
                        1, std::memory_order_acq_rel)
                      - 1;
                    g_current_monitor_handler_state = previous;
                    if (state_->close_requested.load (std::memory_order_acquire)
                        && depth_after == 0) {
                        finalize_monitor_handler_self_close (state_);
                        return;
                    }
                }
            } else {
                zlink_service_event_t event;
                const int rc = recv_service_monitor_event_unchecked (
                  monitor_socket, &event, ZLINK_DONTWAIT);
                if (rc != 0)
                    break;
                drained = true;

                zlink_service_monitor_handler_fn handler =
                  state_->service_handler.load (std::memory_order_acquire);
                if (handler) {
                    monitor_handler_state_t *previous =
                      g_current_monitor_handler_state;
                    g_current_monitor_handler_state = state_;
                    state_->callback_depth.fetch_add (
                      1, std::memory_order_acq_rel);
                    handler (&event, state_->service_handler_userdata.load (std::memory_order_acquire));
                    const int depth_after =
                      state_->callback_depth.fetch_sub (
                        1, std::memory_order_acq_rel)
                      - 1;
                    g_current_monitor_handler_state = previous;
                    if (state_->close_requested.load (std::memory_order_acquire)
                        && depth_after == 0) {
                        finalize_monitor_handler_self_close (state_);
                        return;
                    }
                }
            }
        }
        if (!drained)
            std::this_thread::sleep_for (std::chrono::milliseconds (10));
    }
}

static monitor_handler_state_t *
find_monitor_handler_state (zlink::socket_base_t *socket_)
{
    monitor_handler_registry_t &registry = monitor_handler_registry ();
    zlink::scoped_lock_t lock (registry.sync);
    std::map<zlink::socket_base_t *, monitor_handler_state_t *>::iterator it =
      registry.handlers.find (socket_);
    return it != registry.handlers.end () ? it->second : NULL;
}

static bool monitor_handler_active (zlink::socket_base_t *socket_)
{
    return find_monitor_handler_state (socket_) != NULL;
}

static bool has_open_service_monitor_for_subject (void *snapshot_subject_)
{
    if (!snapshot_subject_)
        return false;

    monitor_handler_registry_t &registry = monitor_handler_registry ();
    zlink::scoped_lock_t lock (registry.sync);
    for (std::map<zlink::socket_base_t *, monitor_handler_state_t *>::iterator it =
           registry.handlers.begin ();
         it != registry.handlers.end (); ++it) {
        monitor_handler_state_t *state = it->second;
        if (!state || !state->service)
            continue;
        if (state->snapshot_subject.load (std::memory_order_acquire)
            == snapshot_subject_) {
            return true;
        }
    }
    return false;
}

static bool has_open_spot_node_monitor_child (zlink::spot_node_t *node_)
{
    if (!node_)
        return false;
    if (has_open_service_monitor_for_subject (node_))
        return true;

    zlink::spot_internal_receiver_t *receiver =
      zlink::spot_node_access_t::internal_receiver (node_);
    if (receiver && has_open_service_monitor_for_subject (receiver))
        return true;

    zlink::spot_pub_t *pub = node_->default_pub ();
    return pub && has_open_service_monitor_for_subject (pub);
}

static bool in_spot_node_send_ready_callback (zlink::spot_node_t *node_)
{
    if (!node_)
        return false;

    zlink::socket_base_t *dispatch_socket =
      zlink::socket_base_t::current_send_ready_dispatch_socket ();
    if (!dispatch_socket)
        return false;

    zlink::spot_pub_t *pub = node_->default_pub ();
    return pub && pub->owns_socket (dispatch_socket);
}

static bool in_spot_node_monitor_callback (zlink::spot_node_t *node_)
{
    if (!node_ || !g_current_monitor_handler_state
        || !g_current_monitor_handler_state->service) {
        return false;
    }

    void *subject =
      g_current_monitor_handler_state->snapshot_subject.load (
        std::memory_order_acquire);
    if (!subject)
        return false;
    if (subject == node_)
        return true;

    zlink::spot_internal_receiver_t *receiver =
      zlink::spot_node_access_t::internal_receiver (node_);
    if (receiver && subject == receiver)
        return true;

    zlink::spot_pub_t *pub = node_->default_pub ();
    return pub && subject == pub;
}

static zlink::socket_base_t *
raw_monitor_snapshot_subject (monitor_handler_state_t *state_)
{
    if (!state_ || state_->service)
        return NULL;

    monitor_snapshot_provider_fn provider =
      state_->snapshot_provider.load (std::memory_order_acquire);
    if (provider != &socket_monitor_snapshot_provider)
        return NULL;

    return static_cast<zlink::socket_base_t *> (
      state_->snapshot_subject.load (std::memory_order_acquire));
}

static void clear_raw_monitor_snapshot_subjects (zlink::socket_base_t *source_)
{
    if (!source_)
        return;

    monitor_handler_registry_t &registry = monitor_handler_registry ();
    zlink::scoped_lock_t lock (registry.sync);
    for (std::map<zlink::socket_base_t *, monitor_handler_state_t *>::iterator it =
           registry.handlers.begin ();
         it != registry.handlers.end ();
         ++it) {
        monitor_handler_state_t *state = it->second;
        if (!state || state->service)
            continue;

        monitor_snapshot_provider_fn provider =
          state->snapshot_provider.load (std::memory_order_acquire);
        if (provider != &socket_monitor_snapshot_provider)
            continue;

        if (state->snapshot_subject.load (std::memory_order_acquire) == source_)
            state->snapshot_subject.store (NULL, std::memory_order_release);
    }
}

static void unregister_monitor_handlers (zlink::socket_base_t *socket_)
{
    if (!socket_)
        return;

    monitor_handler_registry_t &registry = monitor_handler_registry ();
    monitor_handler_state_t *state = NULL;
    {
        zlink::scoped_lock_t lock (registry.sync);
        std::map<zlink::socket_base_t *, monitor_handler_state_t *>::iterator it =
          registry.handlers.find (socket_);
        if (it == registry.handlers.end ())
            return;
        state = it->second;
        registry.handlers.erase (it);
    }

    stop_monitor_handler_state (state);
}

static void close_monitor_parts (zlink_msg_t *parts_, size_t part_count_)
{
    if (!parts_)
        return;

    for (size_t i = 0; i < part_count_; ++i)
        (void) zlink_msg_close (&parts_[i]);
}

static bool read_u64_part (const zlink_msg_t *part_, uint64_t *value_out_)
{
    if (!part_ || !value_out_ || zlink_msg_size (part_) < sizeof (uint64_t))
        return false;

    memcpy (value_out_, zlink_msg_data (const_cast<zlink_msg_t *> (part_)),
            sizeof (uint64_t));
    return true;
}

static bool decode_socket_monitor_event_parts (zlink_msg_t *parts_,
                                               size_t part_count_,
                                               zlink_monitor_event_t *event_)
{
    if (!parts_ || !event_ || part_count_ < 5) {
        errno = EPROTO;
        return false;
    }

    memset (event_, 0, sizeof (*event_));

    uint64_t value_count = 0;
    if (!read_u64_part (&parts_[0], &event_->event)
        || !read_u64_part (&parts_[1], &value_count)) {
        errno = EPROTO;
        return false;
    }

    const size_t expected_part_count = static_cast<size_t> (value_count) + 5;
    if (part_count_ != expected_part_count) {
        errno = EPROTO;
        return false;
    }

    if (value_count > 0
        && !read_u64_part (&parts_[2], &event_->value)) {
        errno = EPROTO;
        return false;
    }

    const size_t routing_id_index = static_cast<size_t> (value_count) + 2;
    const size_t routing_id_size = zlink_msg_size (&parts_[routing_id_index]);
    const size_t routing_id_copy =
      routing_id_size > sizeof (event_->routing_id.data)
        ? sizeof (event_->routing_id.data)
        : routing_id_size;
    event_->routing_id.size = static_cast<uint8_t> (routing_id_copy);
    if (routing_id_copy > 0) {
        memcpy (event_->routing_id.data,
                zlink_msg_data (&parts_[routing_id_index]), routing_id_copy);
    }

    const size_t local_index = routing_id_index + 1;
    const size_t local_size = zlink_msg_size (&parts_[local_index]);
    const size_t local_copy =
      local_size >= sizeof (event_->local_addr)
        ? sizeof (event_->local_addr) - 1
        : local_size;
    if (local_copy > 0)
        memcpy (event_->local_addr, zlink_msg_data (&parts_[local_index]),
                local_copy);
    event_->local_addr[local_copy] = '\0';

    const size_t remote_index = local_index + 1;
    const size_t remote_size = zlink_msg_size (&parts_[remote_index]);
    const size_t remote_copy =
      remote_size >= sizeof (event_->remote_addr)
        ? sizeof (event_->remote_addr) - 1
        : remote_size;
    if (remote_copy > 0)
        memcpy (event_->remote_addr, zlink_msg_data (&parts_[remote_index]),
                remote_copy);
    event_->remote_addr[remote_copy] = '\0';

    return true;
}

static int set_monitor_handler_state (zlink::socket_base_t *socket_,
                                      zlink_monitor_handler_fn socket_handler_,
                                      zlink_service_monitor_handler_fn service_handler_,
                                      bool service_,
                                      monitor_snapshot_provider_fn snapshot_provider_,
                                      void *snapshot_subject_,
                                      void *socket_handler_userdata_,
                                      void *service_handler_userdata_)
{
    if (!socket_) {
        errno = EINVAL;
        return -1;
    }

    monitor_handler_registry_t &registry = monitor_handler_registry ();
    monitor_handler_state_t *state = NULL;
    {
        zlink::scoped_lock_t lock (registry.sync);
        std::map<zlink::socket_base_t *, monitor_handler_state_t *>::iterator it =
          registry.handlers.find (socket_);
        if (it == registry.handlers.end ()) {
            state = new (std::nothrow) monitor_handler_state_t (socket_, service_);
            if (!state) {
                errno = ENOMEM;
                return -1;
            }
            registry.handlers[socket_] = state;
        } else {
            state = it->second;
            if (state->service != service_) {
                errno = EINVAL;
                return -1;
            }
        }
    }

    state->socket_handler.store (socket_handler_, std::memory_order_release);
    state->service_handler.store (service_handler_, std::memory_order_release);
    state->socket_handler_userdata.store (socket_handler_userdata_,
                                          std::memory_order_release);
    state->service_handler_userdata.store (service_handler_userdata_,
                                           std::memory_order_release);
    state->snapshot_provider.store (snapshot_provider_,
                                    std::memory_order_release);
    state->snapshot_subject.store (snapshot_subject_, std::memory_order_release);
    if ((socket_handler_ || service_handler_) && !state->worker.joinable ())
        state->worker = std::thread (&monitor_handler_worker, state);
    return 0;
}

static int socket_monitor_snapshot_provider (void *subject_,
                                             zlink_monitor_snapshot_t *out_)
{
    zlink::socket_base_t *socket =
      static_cast<zlink::socket_base_t *> (subject_);
    if (!socket || !out_) {
        errno = EINVAL;
        return -1;
    }
    return socket->monitor_snapshot (out_);
}

static int gateway_monitor_snapshot_provider (void *subject_,
                                              zlink_monitor_snapshot_t *out_)
{
    zlink::gateway_t *gateway = static_cast<zlink::gateway_t *> (subject_);
    if (!gateway || !out_) {
        errno = EINVAL;
        return -1;
    }
    return gateway->fill_monitor_snapshot (out_);
}

static int spot_pub_monitor_snapshot_provider (void *subject_,
                                               zlink_monitor_snapshot_t *out_)
{
    zlink::spot_pub_t *pub = static_cast<zlink::spot_pub_t *> (subject_);
    if (!pub || !out_) {
        errno = EINVAL;
        return -1;
    }
    return pub->fill_monitor_snapshot (out_);
}

static int spot_sub_monitor_snapshot_provider (void *subject_,
                                               zlink_monitor_snapshot_t *out_)
{
    zlink::spot_sub_t *sub = static_cast<zlink::spot_sub_t *> (subject_);
    if (!sub || !out_) {
        errno = EINVAL;
        return -1;
    }
    return sub->fill_monitor_snapshot (out_);
}

static int spot_internal_receiver_monitor_snapshot_provider (
  void *subject_,
  zlink_monitor_snapshot_t *out_)
{
    zlink::spot_internal_receiver_t *receiver =
      static_cast<zlink::spot_internal_receiver_t *> (subject_);
    if (!receiver || !out_) {
        errno = EINVAL;
        return -1;
    }
    return receiver->fill_monitor_snapshot (out_);
}

struct registry_query_client_t
{
    zlink::ctx_t *ctx;
    zlink::mutex_t sync;
    zlink::service_public_api_guard_t public_api;
    zlink::socket_base_t *dealer;
    uint32_t tag;

    explicit registry_query_client_t (zlink::ctx_t *ctx_) :
        ctx (ctx_),
        dealer (NULL),
        tag (0x1e6700f1)
    {
    }

    ~registry_query_client_t () { destroy_locked (); }

    bool check_tag () const { return tag == 0x1e6700f1; }

    int connect_locked (const char *endpoint_)
    {
        if (!endpoint_ || !*endpoint_) {
            errno = EINVAL;
            return -1;
        }
        if (dealer) {
            dealer->close ();
            dealer = NULL;
        }
        dealer = ctx->create_socket (ZLINK_CORE_SOCKET_DEALER);
        if (!dealer)
            return -1;
        unsigned char rid[5];
        rid[0] = 0;
        uint32_t rid_word = zlink::generate_random ();
        if (rid_word == 0)
            rid_word = 1;
        memcpy (rid + 1, &rid_word, sizeof (rid_word));
        dealer->setsockopt (ZLINK_INTERNAL_OPT_ROUTING_ID, rid, sizeof (rid));
        const int linger = 0;
        const int sndtimeo_ms = 1000;
        const int rcvtimeo_ms = 1000;
        dealer->setsockopt (ZLINK_INTERNAL_OPT_LINGER, &linger, sizeof (linger));
        dealer->setsockopt (ZLINK_INTERNAL_OPT_SNDTIMEO, &sndtimeo_ms,
                            sizeof (sndtimeo_ms));
        dealer->setsockopt (ZLINK_INTERNAL_OPT_RCVTIMEO, &rcvtimeo_ms,
                            sizeof (rcvtimeo_ms));
        if (dealer->connect (endpoint_) != 0)
            return -1;
        if (zlink::wait_socket_events_internal (
              static_cast<void *> (dealer), ZLINK_POLLOUT, 1000)
            <= 0) {
            errno = EAGAIN;
            return -1;
        }
        return 0;
    }

    int destroy_locked ()
    {
        tag = 0xdeadbeef;
        if (dealer) {
            dealer->close ();
            dealer = NULL;
        }
        return 0;
    }
};

static inline socket_handle_t as_socket_handle (void *s_)
{
    socket_handle_t handle;
    handle.socket = NULL;

    if (!s_) {
        errno = EFAULT;
        return handle;
    }

    zlink::socket_base_t *s = static_cast<zlink::socket_base_t *> (s_);
    if (!s->check_tag ()) {
        errno = EFAULT;
        return handle;
    }

    handle.socket = s;
    return handle;
}

static inline spot_handle_t *as_spot_handle (void *spot_)
{
    if (!spot_) {
        errno = EFAULT;
        return NULL;
    }

    spot_handle_t *spot = static_cast<spot_handle_t *> (spot_);
    if (!spot->check_tag ()) {
        errno = EFAULT;
        return NULL;
    }
    return spot;
}

static zlink::spot_pub_t *ensure_spot_pub (spot_handle_t *spot_)
{
    if (!spot_ || !spot_->node) {
        errno = EFAULT;
        return NULL;
    }

    if (spot_->pub)
        return spot_->pub;

    spot_->pub = spot_->node->create_spot_pub ();
    return spot_->pub;
}

static zlink::spot_sub_t *ensure_spot_sub (spot_handle_t *spot_)
{
    if (!spot_ || !spot_->node) {
        errno = EFAULT;
        return NULL;
    }

    if (spot_->sub)
        return spot_->sub;

    zlink::spot_sub_t *sub = spot_->node->create_spot_sub ();
    if (!sub)
        return NULL;

    if (spot_->handler
        && sub->set_direct_handler (&spot_sub_handler_adapter, spot_) != 0) {
        const int err = errno;
        (void) sub->destroy ();
        delete sub;
        errno = err;
        return NULL;
    }

    const zlink::spot_node_t::sub_defaults_t &defaults =
      spot_->pending_sub_defaults;
    if ((defaults.rcvhwm.enabled
         && sub->set_option (ZLINK_SPOT_SUB_OPT_RCVHWM, &defaults.rcvhwm.value,
                             defaults.rcvhwm.size)
              != 0)
        || (defaults.linger.enabled
            && sub->set_option (ZLINK_SPOT_SUB_OPT_LINGER,
                                &defaults.linger.value,
                                defaults.linger.size)
                 != 0)
        || (defaults.sndbuf.enabled
            && sub->set_option (ZLINK_SPOT_SUB_OPT_SNDBUF,
                                &defaults.sndbuf.value,
                                defaults.sndbuf.size)
                 != 0)
        || (defaults.rcvbuf.enabled
            && sub->set_option (ZLINK_SPOT_SUB_OPT_RCVBUF,
                                &defaults.rcvbuf.value,
                                defaults.rcvbuf.size)
                 != 0)
        || (defaults.rcvtimeo.enabled
            && sub->set_option (ZLINK_SPOT_SUB_OPT_RCVTIMEO,
                                &defaults.rcvtimeo.value,
                                defaults.rcvtimeo.size)
                 != 0)) {
        const int err = errno;
        (void) sub->destroy ();
        delete sub;
        errno = err;
        return NULL;
    }

    spot_->sub = sub;
    return spot_->sub;
}

static int validate_spot_sub_option (int option_,
                                     const void *optval_,
                                     size_t optvallen_)
{
    if (!optval_ || optvallen_ == 0 || optvallen_ > sizeof (int)) {
        errno = EINVAL;
        return -1;
    }
    switch (option_) {
        case ZLINK_SPOT_SUB_OPT_RCVHWM:
        case ZLINK_SPOT_SUB_OPT_LINGER:
        case ZLINK_SPOT_SUB_OPT_SNDBUF:
        case ZLINK_SPOT_SUB_OPT_RCVBUF:
        case ZLINK_SPOT_SUB_OPT_RCVTIMEO:
            return 0;
        default:
            errno = EINVAL;
            return -1;
    }
}

static void copy_spot_option_setting (zlink::spot_node_t::option_setting_t *dst_,
                                      const void *optval_,
                                      size_t optvallen_)
{
    if (!dst_)
        return;
    dst_->enabled = true;
    dst_->value = 0;
    dst_->size = optvallen_;
    memcpy (&dst_->value, optval_, optvallen_);
}

static void store_spot_pending_sub_option (spot_handle_t *spot_,
                                           int option_,
                                           const void *optval_,
                                           size_t optvallen_)
{
    if (!spot_)
        return;
    switch (option_) {
        case ZLINK_SPOT_SUB_OPT_RCVHWM:
            copy_spot_option_setting (&spot_->pending_sub_defaults.rcvhwm,
                                      optval_, optvallen_);
            return;
        case ZLINK_SPOT_SUB_OPT_LINGER:
            copy_spot_option_setting (&spot_->pending_sub_defaults.linger,
                                      optval_, optvallen_);
            return;
        case ZLINK_SPOT_SUB_OPT_SNDBUF:
            copy_spot_option_setting (&spot_->pending_sub_defaults.sndbuf,
                                      optval_, optvallen_);
            return;
        case ZLINK_SPOT_SUB_OPT_RCVBUF:
            copy_spot_option_setting (&spot_->pending_sub_defaults.rcvbuf,
                                      optval_, optvallen_);
            return;
        case ZLINK_SPOT_SUB_OPT_RCVTIMEO:
            copy_spot_option_setting (&spot_->pending_sub_defaults.rcvtimeo,
                                      optval_, optvallen_);
            return;
        default:
            return;
    }
}

zlink::service_public_api_guard_t *
zlink::registry_query_public_api_guard_for_testing (void *client_)
{
    registry_query_client_t *client =
      static_cast<registry_query_client_t *> (client_);
    if (!client || !client->check_tag ())
        return NULL;
    return &client->public_api;
}

void zlink::destroy_registry_query_client_for_testing (void *client_)
{
    registry_query_client_t *client =
      static_cast<registry_query_client_t *> (client_);
    if (!client || !client->check_tag ())
        return;
    client->destroy_locked ();
    delete client;
}

zlink::service_public_api_guard_t *
zlink::spot_public_api_guard_for_testing (void *spot_)
{
    spot_handle_t *spot = as_spot_handle (spot_);
    if (spot)
        return &spot->public_api;
    if (zlink::spot_pub_t *pub = as_spot_pub_side_handle (spot_))
        return pub->node () ? &pub->node ()->public_api_guard () : NULL;
    if (zlink::spot_sub_t *sub = as_spot_sub_side_handle (spot_))
        return sub->node () ? &sub->node ()->public_api_guard () : NULL;
    return NULL;
}

void zlink::destroy_spot_handle_for_testing (void *spot_)
{
    spot_handle_t *spot = as_spot_handle (spot_);
    if (!spot) {
        if (zlink::spot_pub_t *pub = as_spot_pub_side_handle (spot_)) {
            if (pub->node ())
                pub->node ()->public_api_guard ().cancel_close ();
            int rc = pub->destroy ();
            if (rc != 0)
                rc = pub->destroy_from_node ();
            zlink_assert (rc == 0);
            delete pub;
            return;
        }
        if (zlink::spot_sub_t *sub = as_spot_sub_side_handle (spot_)) {
            if (sub->node ())
                sub->node ()->public_api_guard ().cancel_close ();
            int rc = sub->destroy ();
            if (rc != 0)
                rc = sub->destroy_from_node ();
            zlink_assert (rc == 0);
            delete sub;
            return;
        }
        return;
    }

    if (spot->sub) {
        int rc = spot->sub->destroy ();
        if (rc != 0)
            rc = spot->sub->destroy_from_node ();
        zlink_assert (rc == 0);
        delete spot->sub;
        spot->sub = NULL;
    }
    if (spot->pub) {
        int rc = spot->pub->destroy ();
        if (rc != 0)
            rc = spot->pub->destroy_from_node ();
        zlink_assert (rc == 0);
        delete spot->pub;
        spot->pub = NULL;
    }

    spot->tag = 0xdeadbeef;
    delete spot;
}

void *zlink_spot_new (void *ctx_, const char *service_name_)
{
    if (!ctx_ || !(static_cast<zlink::ctx_t *> (ctx_))->check_tag ()) {
        errno = EFAULT;
        return NULL;
    }
    if (!service_name_ || service_name_[0] == '\0') {
        errno = EINVAL;
        return NULL;
    }

    zlink::spot_node_t *node =
      new (std::nothrow)
        zlink::spot_node_t (static_cast<zlink::ctx_t *> (ctx_), service_name_);
    if (!node) {
        errno = ENOMEM;
        return NULL;
    }
    if (!node->check_tag ()) {
        delete node;
        errno = EINVAL;
        return NULL;
    }

    spot_handle_t *spot = new (std::nothrow) spot_handle_t ();
    if (!spot) {
        delete node;
        errno = ENOMEM;
        return NULL;
    }

    spot->node = node;
    register_spot_mode_state (spot);
    register_spot_node_mode_state (node);
    return static_cast<void *> (spot);
}

int zlink_spot_destroy (void **spot_p_)
{
    if (!spot_p_ || !*spot_p_) {
        errno = EFAULT;
        return -1;
    }

    spot_handle_t *spot = as_spot_handle (*spot_p_);
    if (!spot)
        return -1;

    zlink::spot_node_t *node = spot->node;
    if (!node || !node->check_tag ()) {
        errno = EFAULT;
        return -1;
    }

    if (in_spot_node_send_ready_callback (node)
        || in_spot_node_monitor_callback (node)) {
        errno = EBUSY;
        return -1;
    }

    erase_spot_mode_state (spot);
    zlink::destroy_spot_handle_for_testing (spot);
    *spot_p_ = NULL;

    void *node_handle = node;
    return zlink_spot_node_destroy (&node_handle);
}

static int recv_registry_reply_frames (void *socket_,
                                       uint16_t expected_msg_id_,
                                       void *entries_,
                                       size_t entry_size_,
                                       size_t *count_)
{
    if (!count_) {
        errno = EINVAL;
        return -1;
    }

    zlink_msg_t frame;
    zlink_msg_init (&frame);
#define RECV_TOPOLOGY_FRAME_OR_RETURN()                                          \
    do {                                                                         \
        if (zlink::wait_socket_events_internal (socket_, ZLINK_POLLIN, 1000)     \
            <= 0) {                                                              \
            errno = EAGAIN;                                                      \
            return -1;                                                           \
        }                                                                        \
        if (zlink::recv_msg_internal (socket_, &frame, 0) < 0) {                \
            zlink_msg_close (&frame);                                             \
            return -1;                                                           \
        }                                                                        \
    } while (false)
    RECV_TOPOLOGY_FRAME_OR_RETURN();
    uint16_t msg_id = 0;
    const bool ok_msg =
      zlink::discovery_protocol::read_u16 (frame, &msg_id) && msg_id == expected_msg_id_;
    zlink_msg_close (&frame);
    if (!ok_msg) {
        errno = EPROTO;
        return -1;
    }

    zlink_msg_init (&frame);
    RECV_TOPOLOGY_FRAME_OR_RETURN();
    uint32_t remote_count = 0;
    const bool ok_count = zlink::discovery_protocol::read_u32 (frame, &remote_count);
    zlink_msg_close (&frame);
    if (!ok_count) {
        errno = EPROTO;
        return -1;
    }

    if (!entries_) {
        *count_ = remote_count;
        for (uint32_t i = 0; i < remote_count; ++i) {
            zlink_msg_init (&frame);
            RECV_TOPOLOGY_FRAME_OR_RETURN();
            zlink_msg_close (&frame);
        }
        return 0;
    }

    if (*count_ < remote_count) {
        for (uint32_t i = 0; i < remote_count; ++i) {
            zlink_msg_init (&frame);
            RECV_TOPOLOGY_FRAME_OR_RETURN();
            zlink_msg_close (&frame);
        }
        *count_ = remote_count;
        errno = ENOBUFS;
        return -1;
    }

    for (uint32_t i = 0; i < remote_count; ++i) {
        zlink_msg_init (&frame);
        RECV_TOPOLOGY_FRAME_OR_RETURN();
        if (zlink_msg_size (&frame) != entry_size_) {
            zlink_msg_close (&frame);
            errno = EPROTO;
            return -1;
        }
        memcpy (static_cast<char *> (entries_) + (i * entry_size_),
                zlink_msg_data (&frame), entry_size_);
        zlink_msg_close (&frame);
    }
#undef RECV_TOPOLOGY_FRAME_OR_RETURN

    *count_ = remote_count;
    return 0;
}

static int recv_topology_reply_frames (
  void *socket_,
  zlink_registry_topology_entry_t *entries_,
  size_t *count_)
{
    return recv_registry_reply_frames (
      socket_, zlink::discovery_protocol::msg_topology_reply, entries_,
      sizeof (zlink_registry_topology_entry_t), count_);
}

static int recv_gateway_peer_reply_frames (
  void *socket_,
  zlink_registry_gateway_peer_entry_t *entries_,
  size_t *count_)
{
    return recv_registry_reply_frames (
      socket_, zlink::discovery_protocol::msg_gateway_peer_reply, entries_,
      sizeof (zlink_registry_gateway_peer_entry_t), count_);
}


static zlink::spot_sub_t *as_spot_sub_service (void *sub_)
{
    if (!sub_)
        return NULL;
    zlink::spot_sub_t *sub = static_cast<zlink::spot_sub_t *> (sub_);
    if (!sub->check_tag ()) {
        errno = EFAULT;
        return NULL;
    }
    return sub;
}

static zlink::spot_pub_t *as_spot_pub_service (void *pub_)
{
    if (!pub_)
        return NULL;
    zlink::spot_pub_t *pub = static_cast<zlink::spot_pub_t *> (pub_);
    if (!pub->check_tag ()) {
        errno = EFAULT;
        return NULL;
    }
    return pub;
}

static zlink::gateway_t *as_gateway_service (void *gateway_)
{
    if (!gateway_)
        return NULL;
    zlink::gateway_t *gateway = static_cast<zlink::gateway_t *> (gateway_);
    if (!gateway->check_tag ()) {
        errno = EFAULT;
        return NULL;
    }
    return gateway;
}

namespace
{
static bool is_stream_type (socket_handle_t handle_)
{
    if (!handle_.socket)
        return false;

    int type = -1;
    size_t type_len = sizeof (type);
    if (handle_.socket->getsockopt (ZLINK_INTERNAL_OPT_TYPE, &type, &type_len) != 0)
        return false;
    return type == ZLINK_CORE_SOCKET_STREAM;
}

static int socket_type (socket_handle_t handle_)
{
    if (!handle_.socket)
        return -1;

    int type = -1;
    size_t type_len = sizeof (type);
    if (handle_.socket->getsockopt (ZLINK_INTERNAL_OPT_TYPE, &type, &type_len) != 0)
        return -1;
    return type;
}

static int core_socket_type_from_public_type (zlink_socket_type_t type_)
{
    switch (type_) {
        case ZLINK_SOCKET_PAIR:
            return ZLINK_CORE_SOCKET_PAIR;
        case ZLINK_SOCKET_PUB:
            return ZLINK_CORE_SOCKET_PUB;
        case ZLINK_SOCKET_SUB:
            return ZLINK_CORE_SOCKET_SUB;
        case ZLINK_SOCKET_DEALER:
            return ZLINK_CORE_SOCKET_DEALER;
        case ZLINK_SOCKET_ROUTER:
            return ZLINK_CORE_SOCKET_ROUTER;
        case ZLINK_SOCKET_XPUB:
            return ZLINK_CORE_SOCKET_XPUB;
        case ZLINK_SOCKET_XSUB:
            return ZLINK_CORE_SOCKET_XSUB;
        case ZLINK_SOCKET_STREAM:
            return ZLINK_CORE_SOCKET_STREAM;
        default:
            return -1;
    }
}

static bool is_send_only_socket_type (int type_)
{
    return type_ == ZLINK_CORE_SOCKET_PUB;
}

class stream_api_lock_t
{
  public:
    explicit stream_api_lock_t (socket_handle_t handle_) : _lock ()
    {
        if (!handle_.socket)
            return;

        std::recursive_mutex *mutex = handle_.socket->api_sync_mutex ();
        if (mutex)
            _lock = std::unique_lock<std::recursive_mutex> (*mutex);
    }

  private:
    std::unique_lock<std::recursive_mutex> _lock;
};

static void discard_socket_msg_handler (const zlink_routing_id_t *,
                                        zlink_msg_t *parts_,
                                        size_t part_count_,
                                        void *)
{
    for (size_t i = 0; i < part_count_; ++i)
        zlink_msg_close (&parts_[i]);
}

static void discard_spot_handler (const zlink_routing_id_t *,
                                  const char *,
                                  size_t,
                                  zlink_msg_t *parts_,
                                  size_t part_count_,
                                  void *)
{
    for (size_t i = 0; i < part_count_; ++i)
        zlink_msg_close (&parts_[i]);
}

static void spot_sub_side_handler_adapter (const zlink_routing_id_t *source_rid_,
                                           const char *topic_,
                                           size_t topic_len_,
                                           zlink_msg_t *parts_,
                                           size_t part_count_,
                                           void *userdata_)
{
    zlink::spot_sub_t *sub = static_cast<zlink::spot_sub_t *> (userdata_);
    if (!sub || !sub->check_tag ()) {
        close_spot_parts (parts_, part_count_);
        return;
    }

    zlink_subscribe_handler_fn handler = NULL;
    void *userdata = NULL;
    {
        spot_sub_handler_registry_t &registry = spot_sub_handler_registry ();
        zlink::scoped_lock_t lock (registry.sync);
        std::map<zlink::spot_sub_t *, spot_sub_handler_entry_t>::iterator it =
          registry.handlers.find (sub);
        if (it != registry.handlers.end ()) {
            handler = it->second.handler;
            userdata = it->second.userdata;
        }
    }

    if (!handler) {
        close_spot_parts (parts_, part_count_);
        return;
    }
    g_current_spot_dispatch_handle = sub;
    g_current_spot_dispatch_is_node = false;
    handler (source_rid_, topic_, topic_len_, parts_, part_count_, userdata);
    g_current_spot_dispatch_handle = NULL;
    g_current_spot_dispatch_is_node = false;
}

static int install_spot_node_handler (zlink::spot_node_t *node_,
                                      zlink_subscribe_handler_fn handler_,
                                      void *userdata_)
{
    if (!node_ || !handler_) {
        errno = EINVAL;
        return -1;
    }

    zlink::spot_internal_receiver_t *receiver =
      zlink::spot_node_access_t::ensure_internal_receiver (node_);
    if (!receiver)
        return -1;

    spot_node_handler_entry_t previous = {NULL, NULL};
    bool had_previous = false;
    {
        spot_node_handler_registry_t &registry = spot_node_handler_registry ();
        zlink::scoped_lock_t lock (registry.sync);
        std::map<zlink::spot_node_t *, spot_node_handler_entry_t>::iterator it =
          registry.handlers.find (node_);
        if (it != registry.handlers.end ()) {
            previous = it->second;
            had_previous = true;
        }
        spot_node_handler_entry_t entry = {handler_, userdata_};
        registry.handlers[node_] = entry;
    }

    if (receiver->set_direct_handler (&spot_node_sub_handler_adapter, node_)
        == 0)
        return 0;

    {
        spot_node_handler_registry_t &registry = spot_node_handler_registry ();
        zlink::scoped_lock_t lock (registry.sync);
        if (had_previous)
            registry.handlers[node_] = previous;
        else
            registry.handlers.erase (node_);
    }
    return -1;
}

static int install_spot_sub_handler (zlink::spot_sub_t *sub_,
                                     zlink_subscribe_handler_fn handler_,
                                     void *userdata_)
{
    if (!sub_ || !handler_) {
        errno = EINVAL;
        return -1;
    }

    spot_sub_handler_entry_t previous = {NULL, NULL};
    bool had_previous = false;
    {
        spot_sub_handler_registry_t &registry = spot_sub_handler_registry ();
        zlink::scoped_lock_t lock (registry.sync);
        std::map<zlink::spot_sub_t *, spot_sub_handler_entry_t>::iterator it =
          registry.handlers.find (sub_);
        if (it != registry.handlers.end ()) {
            previous = it->second;
            had_previous = true;
        }
        spot_sub_handler_entry_t entry = {handler_, userdata_};
        registry.handlers[sub_] = entry;
    }

    if (sub_->set_direct_handler (&spot_sub_side_handler_adapter, sub_) == 0)
        return 0;

    {
        spot_sub_handler_registry_t &registry = spot_sub_handler_registry ();
        zlink::scoped_lock_t lock (registry.sync);
        if (had_previous)
            registry.handlers[sub_] = previous;
        else
            registry.handlers.erase (sub_);
    }
    return -1;
}

static bool parse_stream_routing_id (const zlink_routing_id_t *rid_,
                                     uint32_t *routing_id_out_)
{
    if (!rid_ || !routing_id_out_ || rid_->size == 0
        || rid_->size > sizeof (rid_->data) || rid_->size != 4) {
        errno = EINVAL;
        return false;
    }

    *routing_id_out_ = (static_cast<uint32_t> (rid_->data[0]) << 24)
                       | (static_cast<uint32_t> (rid_->data[1]) << 16)
                       | (static_cast<uint32_t> (rid_->data[2]) << 8)
                       | static_cast<uint32_t> (rid_->data[3]);
    return true;
}

static void release_stream_send_msg (zlink::msg_t *msg_)
{
    if (!msg_ || !msg_->check ())
        return;

    int rc = msg_->close ();
    errno_assert (rc == 0);
    rc = msg_->init ();
    errno_assert (rc == 0);
}

static int stream_payload_result (size_t size_)
{
    return static_cast<int> (size_ < static_cast<size_t> (INT_MAX) ? size_
                                                                    : INT_MAX);
}

} // namespace

static void *create_socket_handle (void *ctx_, zlink_socket_type_t type_)
{
    if (!ctx_ || !(static_cast<zlink::ctx_t *> (ctx_))->check_tag ()) {
        errno = EFAULT;
        return NULL;
    }

    const int core_type = core_socket_type_from_public_type (type_);
    if (!is_send_only_socket_type (core_type)) {
        switch (core_type) {
            case ZLINK_CORE_SOCKET_PAIR:
            case ZLINK_CORE_SOCKET_DEALER:
            case ZLINK_CORE_SOCKET_ROUTER:
            case ZLINK_CORE_SOCKET_STREAM:
            case ZLINK_CORE_SOCKET_SUB:
            case ZLINK_CORE_SOCKET_XSUB:
            case ZLINK_CORE_SOCKET_XPUB:
                break;
            default:
                errno = EINVAL;
                return NULL;
        }
    }

    zlink::ctx_t *ctx = static_cast<zlink::ctx_t *> (ctx_);
    zlink::socket_base_t *socket = ctx->create_socket (core_type);
    if (!socket)
        return NULL;

    return static_cast<void *> (socket);
}

void *zlink_socket (void *ctx_, zlink_socket_type_t type_)
{
    return create_socket_handle (ctx_, type_);
}

int zlink_recv_handler (void *s_,
                        zlink_socket_msg_handler_fn handler_,
                        void *userdata_)
{
    if (!handler_) {
        errno = EINVAL;
        return -1;
    }

    if (is_registered_gateway_handle (s_)) {
        zlink::gateway_t *gateway = static_cast<zlink::gateway_t *> (s_);
        if (!gateway->check_tag ()) {
            errno = EFAULT;
            return -1;
        }
        zlink::service_public_api_scope_t admission (gateway->public_api_guard ());
        if (!admission.acquired ())
            return -1;
        {
            gateway_mode_registry_t &registry = gateway_mode_registry ();
            zlink::scoped_lock_t lock (registry.sync);
            if (transition_service_to_callback_mode (&registry.states[gateway])
                != 0) {
                return -1;
            }
        }
        const int rc = gateway->set_handler (handler_, userdata_);
        if (rc != 0)
            gateway_revert_callback_transition (gateway);
        return rc;
    }

    socket_handle_t handle = as_socket_handle (s_);
    if (!handle.socket)
        return -1;

    if (handler_ == &discard_socket_parts) {
        errno = EINVAL;
        return -1;
    }

    const int type = socket_type (handle);
    switch (type) {
        case ZLINK_CORE_SOCKET_PAIR:
        case ZLINK_CORE_SOCKET_DEALER:
        case ZLINK_CORE_SOCKET_ROUTER:
            return handle.socket->socket_set_msg_handler_with_userdata (
              handler_, NULL, userdata_);
        case ZLINK_CORE_SOCKET_SUB:
        case ZLINK_CORE_SOCKET_XSUB:
        case ZLINK_CORE_SOCKET_PUB:
        case ZLINK_CORE_SOCKET_XPUB:
            errno = ENOTSUP;
            return -1;
        case ZLINK_CORE_SOCKET_STREAM:
            return handle.socket->stream_set_msg_handler_with_userdata (
              handler_, userdata_);
        default:
            errno = ENOTSUP;
            return -1;
    }
}

int zlink_subscribe_handler (void *s_,
                             zlink_subscribe_handler_fn handler_,
                             void *userdata_)
{
    return zlink_recv_spot_handler (s_, handler_, userdata_);
}

int zlink_recv_spot_handler (void *s_,
                             zlink_subscribe_handler_fn handler_,
                             void *userdata_)
{
    if (!handler_) {
        errno = EINVAL;
        return -1;
    }
    if (handler_ == &discard_spot_parts) {
        errno = EINVAL;
        return -1;
    }

    if (is_registered_spot_handle (s_)) {
        spot_handle_t *spot = static_cast<spot_handle_t *> (s_);
        zlink::service_public_api_scope_t admission (spot->public_api);
        if (!admission.acquired ())
            return -1;
        if (spot_transition_to_callback_mode (spot) != 0)
            return -1;
        zlink::spot_sub_t *sub = ensure_spot_sub (spot);
        if (!sub)
            return -1;
        spot->handler = handler_;
        spot->handler_userdata = userdata_;
        const int rc = sub->set_direct_handler (&spot_sub_handler_adapter, spot);
        if (rc != 0) {
            spot->handler = NULL;
            spot->handler_userdata = NULL;
            spot_revert_callback_transition (spot);
        }
        return rc;
    }

    if (is_registered_spot_node_handle (s_)) {
        zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (s_);
        zlink::service_public_api_scope_t admission (node->public_api_guard ());
        if (!admission.acquired ())
            return -1;
        if (spot_node_transition_to_callback_mode (node) != 0)
            return -1;
        const int rc = install_spot_node_handler (node, handler_, userdata_);
        if (rc != 0)
            spot_node_revert_callback_transition (node);
        return rc;
    }

    socket_handle_t handle = as_socket_handle (s_);
    if (!handle.socket)
        return -1;

    const int type = socket_type (handle);
    switch (type) {
        case ZLINK_CORE_SOCKET_SUB:
        case ZLINK_CORE_SOCKET_XSUB:
            return handle.socket->socket_set_spot_handler_with_userdata (
              handler_, userdata_);
        default:
            errno = ENOTSUP;
            return -1;
    }
}

static int socket_send_ready_handler_internal (
  void *s_,
  zlink_send_ready_handler_fn handler_,
  void *userdata_)
{
    socket_handle_t handle = as_socket_handle (s_);
    if (!handle.socket)
        return -1;

    if (!handler_) {
        errno = EINVAL;
        return -1;
    }

    const int type = socket_type (handle);
    switch (type) {
        case ZLINK_CORE_SOCKET_PAIR:
        case ZLINK_CORE_SOCKET_PUB:
        case ZLINK_CORE_SOCKET_XPUB:
        case ZLINK_CORE_SOCKET_DEALER:
        case ZLINK_CORE_SOCKET_ROUTER:
        case ZLINK_CORE_SOCKET_STREAM:
            break;
        default:
            errno = ENOTSUP;
            return -1;
    }

    return handle.socket->socket_set_send_ready_handler_with_userdata (
      handler_, NULL, userdata_);
}

int zlink_send_ready_handler (void *s_,
                              zlink_send_ready_handler_fn handler_,
                              void *userdata_)
{
    if (!handler_) {
        errno = EINVAL;
        return -1;
    }

    if (is_registered_gateway_handle (s_)) {
        zlink::gateway_t *gateway = static_cast<zlink::gateway_t *> (s_);
        if (!gateway->check_tag ()) {
            errno = EFAULT;
            return -1;
        }
        zlink::service_public_api_scope_t admission (gateway->public_api_guard ());
        if (!admission.acquired ())
            return -1;
        bool already_active = false;
        {
            gateway_mode_registry_t &registry = gateway_mode_registry ();
            zlink::scoped_lock_t lock (registry.sync);
            already_active = registry.states[gateway].send_ready_active;
            if (activate_service_send_ready_mode (&registry.states[gateway])
                != 0) {
                return -1;
            }
        }
        const int rc = gateway->set_send_ready_handler (handler_, userdata_);
        if (rc != 0 && !already_active) {
            gateway_mode_registry_t &registry = gateway_mode_registry ();
            zlink::scoped_lock_t lock (registry.sync);
            revert_service_send_ready_mode (&registry.states[gateway]);
        }
        return rc;
    }

    if (is_registered_spot_handle (s_)) {
        spot_handle_t *spot = as_spot_handle (s_);
        if (!spot)
            return -1;
        zlink::service_public_api_scope_t admission (spot->public_api);
        if (!admission.acquired ())
            return -1;
        bool already_active = false;
        {
            spot_mode_registry_t &registry = spot_mode_registry ();
            zlink::scoped_lock_t lock (registry.sync);
            already_active = registry.states[spot].send_ready_active;
            if (activate_service_send_ready_mode (&registry.states[spot]) != 0)
                return -1;
        }
        zlink::spot_pub_t *pub = ensure_spot_pub (spot);
        if (!pub) {
            if (!already_active) {
                spot_mode_registry_t &registry = spot_mode_registry ();
                zlink::scoped_lock_t lock (registry.sync);
                revert_service_send_ready_mode (&registry.states[spot]);
            }
            errno = ENOTSUP;
            return -1;
        }
        const int rc = pub->set_send_ready_handler (handler_, spot, userdata_);
        if (rc != 0 && !already_active) {
            spot_mode_registry_t &registry = spot_mode_registry ();
            zlink::scoped_lock_t lock (registry.sync);
            revert_service_send_ready_mode (&registry.states[spot]);
        }
        return rc;
    }

    if (is_registered_spot_node_handle (s_)) {
        zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (s_);
        if (!node->check_tag ()) {
            errno = EFAULT;
            return -1;
        }
        zlink::service_public_api_scope_t admission (node->public_api_guard ());
        if (!admission.acquired ())
            return -1;
        bool already_active = false;
        {
            spot_node_mode_registry_t &registry = spot_node_mode_registry ();
            zlink::scoped_lock_t lock (registry.sync);
            already_active = registry.states[node].send_ready_active;
            if (activate_service_send_ready_mode (&registry.states[node]) != 0)
                return -1;
        }
        const int rc = node->set_send_ready_handler (handler_, userdata_);
        if (rc != 0 && !already_active) {
            spot_node_mode_registry_t &registry = spot_node_mode_registry ();
            zlink::scoped_lock_t lock (registry.sync);
            revert_service_send_ready_mode (&registry.states[node]);
        }
        return rc;
    }

    return socket_send_ready_handler_internal (s_, handler_, userdata_);
}

static int validate_socket_callback_poller_events (socket_handle_t handle_,
                                                   short events_)
{
    if (!handle_.socket)
        return 0;
    const int type = socket_type (handle_);
    if ((events_ & ZLINK_POLLIN) != 0) {
        if (handle_.socket->socket_msg_dispatch_active ()
            || ((type == ZLINK_CORE_SOCKET_SUB
                 || type == ZLINK_CORE_SOCKET_XSUB)
                && handle_.socket->sub_dispatch_active ())
            || (type == ZLINK_CORE_SOCKET_STREAM
                && handle_.socket->stream_dispatch_active ())) {
            errno = EBUSY;
            return -1;
        }
    }
    if ((events_ & ZLINK_POLLOUT) != 0
        && handle_.socket->send_ready_handler_active ()) {
        errno = EBUSY;
        return -1;
    }
    return 0;
}

int zlink_close (void *s_)
{
    socket_handle_t handle = as_socket_handle (s_);
    if (!handle.socket)
        return -1;

    monitor_handler_state_t *monitor_state =
      find_monitor_handler_state (handle.socket);
    zlink::socket_base_t *raw_monitor_source =
      raw_monitor_snapshot_subject (monitor_state);
    if (monitor_state) {
        if (g_current_monitor_handler_state == monitor_state) {
            monitor_state->close_requested.store (true,
                                                 std::memory_order_release);
            return 0;
        }

        if (monitor_state->close_requested.load (std::memory_order_acquire)
            || monitor_state->callback_depth.load (std::memory_order_acquire)
                 > 0) {
            errno = EBUSY;
            return -1;
        }
    }

    if (raw_monitor_source && raw_monitor_source != handle.socket) {
        monitor_state->snapshot_subject.store (NULL, std::memory_order_release);
        (void) raw_monitor_source->monitor (NULL, 0, 3, ZLINK_CORE_SOCKET_PAIR);
    } else {
        clear_raw_monitor_snapshot_subjects (handle.socket);
    }

    unregister_monitor_handlers (handle.socket);

    if (handle.socket->api_sync_mutex ()) {
        if (handle.socket->socket_msg_dispatch_active ()) {
            if (zlink::socket_base_t::current_socket_msg_dispatch_socket ()
                == handle.socket) {
                errno = EBUSY;
                return -1;
            }
            (void) handle.socket->socket_msg_dispatch_stop ();
        }
        if (handle.socket->stream_dispatch_in_callback ()) {
            errno = EBUSY;
            return -1;
        }

        handle.socket->stop ();
        stream_api_lock_t api_lock (handle);
        (void) handle.socket->stream_dispatch_stop ();
        handle.socket->close ();
        return 0;
    }

    if (handle.socket->socket_msg_dispatch_active ()) {
        if (zlink::socket_base_t::current_socket_msg_dispatch_socket ()
            == handle.socket) {
            errno = EBUSY;
            return -1;
        }
        (void) handle.socket->socket_msg_dispatch_stop ();
    }

    (void) handle.socket->stream_dispatch_stop ();
    handle.socket->close ();
    return 0;
}

static void *open_socket_monitor_with_handler_internal (
  void *s_,
  zlink_socket_monitor_event_mask_t events_,
  zlink_monitor_handler_fn handler_,
  void *userdata_)
{
    socket_handle_t handle = as_socket_handle (s_);
    if (!handle.socket)
        return NULL;
    if (!handler_) {
        errno = EINVAL;
        return NULL;
    }

    zlink_monitor_handler_fn effective_handler = handler_;
    if (handler_ == &zlink_monitor_ignore_handler)
        effective_handler = NULL;

    char endpoint[128];
    const uint32_t rand_id = zlink::generate_random ();
    snprintf (endpoint, sizeof endpoint, "inproc://monitor-%p-%u",
              static_cast<void *> (s_), rand_id);

    const int monitor_rc =
      handle.socket->monitor (endpoint, events_, 3, ZLINK_CORE_SOCKET_PAIR);
    if (monitor_rc != 0)
        return NULL;

    zlink::socket_base_t *monitor_socket_base =
      handle.socket->get_ctx ()->create_socket (ZLINK_CORE_SOCKET_PAIR);
    void *monitor_socket = static_cast<void *> (monitor_socket_base);
    if (!monitor_socket) {
        handle.socket->monitor (NULL, 0, 3, ZLINK_CORE_SOCKET_PAIR);
        return NULL;
    }

    if (zlink_connect (monitor_socket, endpoint) != 0) {
        zlink_close (monitor_socket);
        handle.socket->monitor (NULL, 0, 3, ZLINK_CORE_SOCKET_PAIR);
        return NULL;
    }

    if (set_monitor_handler_state (monitor_socket_base, effective_handler, NULL,
                                   false,
                                   &socket_monitor_snapshot_provider,
                                   static_cast<void *> (handle.socket),
                                   userdata_, NULL)
        != 0) {
        const int err = errno;
        zlink_close (monitor_socket);
        handle.socket->monitor (NULL, 0, 3, ZLINK_CORE_SOCKET_PAIR);
        errno = err;
        return NULL;
    }

    return monitor_socket;
}

static int recv_socket_monitor_event_unchecked (void *monitor_socket_,
                                                zlink_monitor_event_t *event_,
                                                int flags_)
{
    if (!monitor_socket_ || !event_) {
        errno = EINVAL;
        return -1;
    }

    zlink_msg_t msg;
    zlink_msg_init (&msg);
    int rc = zlink::recv_msg_internal (monitor_socket_, &msg, flags_);
    if (rc == -1) {
        zlink_msg_close (&msg);
        return -1;
    }

    memset (event_, 0, sizeof (*event_));

    if (zlink_msg_size (&msg) == sizeof (*event_)) {
        memcpy (event_, zlink_msg_data (&msg), sizeof (*event_));
        zlink_msg_close (&msg);
        return 0;
    }

    if (zlink_msg_size (&msg) < sizeof (uint64_t)) {
        zlink_msg_close (&msg);
        errno = EPROTO;
        return -1;
    }

    memcpy (&event_->event, zlink_msg_data (&msg), sizeof (uint64_t));
    zlink_msg_close (&msg);

    const int follow_flags = flags_ & ~ZLINK_DONTWAIT;

    zlink_msg_init (&msg);
    rc = zlink::recv_msg_internal (monitor_socket_, &msg, follow_flags);
    if (rc == -1) {
        zlink_msg_close (&msg);
        return -1;
    }
    if (zlink_msg_size (&msg) < sizeof (uint64_t)) {
        zlink_msg_close (&msg);
        errno = EPROTO;
        return -1;
    }

    uint64_t value_count = 0;
    memcpy (&value_count, zlink_msg_data (&msg), sizeof (uint64_t));
    zlink_msg_close (&msg);

    for (uint64_t i = 0; i < value_count; ++i) {
        zlink_msg_init (&msg);
        rc = zlink::recv_msg_internal (monitor_socket_, &msg, follow_flags);
        if (rc == -1) {
            zlink_msg_close (&msg);
            return -1;
        }
        if (i == 0 && zlink_msg_size (&msg) >= sizeof (uint64_t)) {
            memcpy (&event_->value, zlink_msg_data (&msg), sizeof (uint64_t));
        }
        zlink_msg_close (&msg);
    }

    zlink_msg_init (&msg);
    rc = zlink::recv_msg_internal (monitor_socket_, &msg, follow_flags);
    if (rc == -1) {
        zlink_msg_close (&msg);
        return -1;
    }
    const size_t routing_id_size = zlink_msg_size (&msg);
    const size_t copy_size =
      routing_id_size > sizeof (event_->routing_id.data)
        ? sizeof (event_->routing_id.data)
        : routing_id_size;
    event_->routing_id.size = static_cast<uint8_t> (copy_size);
    if (copy_size > 0) {
        memcpy (event_->routing_id.data, zlink_msg_data (&msg), copy_size);
    }
    zlink_msg_close (&msg);

    zlink_msg_init (&msg);
    rc = zlink::recv_msg_internal (monitor_socket_, &msg, follow_flags);
    if (rc == -1) {
        zlink_msg_close (&msg);
        return -1;
    }
    const size_t local_size = zlink_msg_size (&msg);
    const size_t local_copy =
      local_size >= sizeof (event_->local_addr)
        ? sizeof (event_->local_addr) - 1
        : local_size;
    if (local_copy > 0)
        memcpy (event_->local_addr, zlink_msg_data (&msg), local_copy);
    event_->local_addr[local_copy] = '\0';
    zlink_msg_close (&msg);

    zlink_msg_init (&msg);
    rc = zlink::recv_msg_internal (monitor_socket_, &msg, follow_flags);
    if (rc == -1) {
        zlink_msg_close (&msg);
        return -1;
    }
    const size_t remote_size = zlink_msg_size (&msg);
    const size_t remote_copy =
      remote_size >= sizeof (event_->remote_addr)
        ? sizeof (event_->remote_addr) - 1
        : remote_size;
    if (remote_copy > 0)
        memcpy (event_->remote_addr, zlink_msg_data (&msg), remote_copy);
    event_->remote_addr[remote_copy] = '\0';
    zlink_msg_close (&msg);

    return 0;
}

static int recv_service_monitor_event_unchecked (void *monitor_,
                                                 zlink_service_event_t *event_,
                                                 int flags_)
{
    if (!monitor_ || !event_) {
        errno = EINVAL;
        return -1;
    }

    zlink_msg_t msg;
    zlink_msg_init (&msg);
    const int rc = zlink::recv_msg_internal (monitor_, &msg, flags_);
    if (rc < 0) {
        zlink_msg_close (&msg);
        return -1;
    }
    if (zlink_msg_size (&msg) != sizeof (*event_)) {
        zlink_msg_close (&msg);
        errno = EPROTO;
        return -1;
    }
    memcpy (event_, zlink_msg_data (&msg), sizeof (*event_));
    zlink_msg_close (&msg);
    return 0;
}

static int attach_socket_monitor_handler_state (
  void *monitor_,
  zlink_socket_monitor_handler_fn handler_,
  void *userdata_)
{
    if (!monitor_) {
        errno = EFAULT;
        return -1;
    }
    if (!handler_) {
        errno = EINVAL;
        return -1;
    }

    socket_handle_t handle = as_socket_handle (monitor_);
    if (!handle.socket)
        return -1;

    monitor_handler_state_t *state = find_monitor_handler_state (handle.socket);
    if (!state || state->service) {
        errno = EINVAL;
        return -1;
    }
    if (state->socket_handler.load (std::memory_order_acquire)
        || state->service_handler.load (std::memory_order_acquire)) {
        errno = EBUSY;
        return -1;
    }

    return set_monitor_handler_state (
      handle.socket, handler_, NULL, false,
      state->snapshot_provider.load (std::memory_order_acquire),
      state->snapshot_subject.load (std::memory_order_acquire), userdata_,
      NULL);
}

static int attach_service_monitor_handler_state (
  void *monitor_,
  zlink_service_monitor_handler_fn handler_,
  void *userdata_)
{
    if (!monitor_) {
        errno = EFAULT;
        return -1;
    }
    if (!handler_) {
        errno = EINVAL;
        return -1;
    }

    socket_handle_t handle = as_socket_handle (monitor_);
    if (!handle.socket)
        return -1;

    monitor_handler_state_t *state = find_monitor_handler_state (handle.socket);
    if (!state || !state->service) {
        errno = EINVAL;
        return -1;
    }
    if (state->socket_handler.load (std::memory_order_acquire)
        || state->service_handler.load (std::memory_order_acquire)) {
        errno = EBUSY;
        return -1;
    }

    return set_monitor_handler_state (
      handle.socket, NULL, handler_, true,
      state->snapshot_provider.load (std::memory_order_acquire),
      state->snapshot_subject.load (std::memory_order_acquire), NULL, userdata_);
}

static int require_monitor_recv_model (void *monitor_, bool service_)
{
    if (!monitor_) {
        errno = EFAULT;
        return -1;
    }

    socket_handle_t handle = as_socket_handle (monitor_);
    if (!handle.socket)
        return -1;

    monitor_handler_state_t *state = find_monitor_handler_state (handle.socket);
    if (!state || state->service != service_) {
        errno = EINVAL;
        return -1;
    }

    if (state->socket_handler.load (std::memory_order_acquire)
        || state->service_handler.load (std::memory_order_acquire)) {
        errno = EBUSY;
        return -1;
    }
    return 0;
}

int zlink_monitor_close (void **monitor_p_)
{
    if (!monitor_p_ || !*monitor_p_) {
        errno = EFAULT;
        return -1;
    }
    void *monitor = *monitor_p_;
    socket_handle_t handle = as_socket_handle (monitor);
    if (!handle.socket)
        return -1;

    zlink::socket_base_t *socket = handle.socket;
    const int linger = 0;
    (void) socket->setsockopt (ZLINK_INTERNAL_OPT_LINGER, &linger, sizeof (linger));
    monitor_handler_state_t *monitor_state = find_monitor_handler_state (socket);
    const bool had_dispatch_monitor =
      monitor_state
      && (monitor_state->socket_handler.load (std::memory_order_acquire)
            || monitor_state->service_handler.load (std::memory_order_acquire));
    const bool no_dispatch_monitor =
      monitor_state
      && !monitor_state->socket_handler.load (std::memory_order_acquire)
      && !monitor_state->service_handler.load (std::memory_order_acquire);
    if (monitor_state
        && monitor_state->callback_depth.load (std::memory_order_acquire) > 0
        && g_current_monitor_handler_state != monitor_state) {
        errno = EBUSY;
        return -1;
    }
    if (monitor_state && had_dispatch_monitor) {
        monitor_state->socket_handler.store (NULL, std::memory_order_release);
        monitor_state->service_handler.store (NULL,
                                              std::memory_order_release);
    }
    if (monitor_state && g_current_monitor_handler_state == monitor_state) {
        const int rc = zlink_close (monitor);
        if (rc == 0)
            *monitor_p_ = NULL;
        return rc;
    }
    if (no_dispatch_monitor) {
        // Ignore-handler/direct-poll monitors do not run a dispatch worker.
        socket->stop ();
    } else if (monitor_state
               && monitor_state->callback_depth.load (std::memory_order_acquire)
                    > 0) {
        errno = EBUSY;
        return -1;
    }
    const int rc = zlink_close (monitor);
    if (rc == 0) {
        *monitor_p_ = NULL;
    }
    return rc;
}

void *zlink_socket_monitor_open (
  void *s_, const zlink_socket_monitor_open_options_t *options_)
{
    if (!options_) {
        errno = EINVAL;
        return NULL;
    }
    return open_socket_monitor_with_handler_internal (
      s_, options_->events, &zlink_monitor_ignore_handler, NULL);
}

int zlink_socket_monitor_handler (void *monitor_,
                                  zlink_socket_monitor_handler_fn handler_,
                                  void *userdata_)
{
    return attach_socket_monitor_handler_state (monitor_, handler_, userdata_);
}

int zlink_socket_monitor_recv (void *monitor_,
                               zlink_socket_monitor_event_t *out_)
{
    if (require_monitor_recv_model (monitor_, false) != 0)
        return -1;
    return recv_socket_monitor_event_unchecked (monitor_, out_, 0);
}

int zlink_monitor_snapshot (void *monitor_,
                            zlink_monitor_snapshot_t *out_)
{
    if (!out_) {
        errno = EINVAL;
        return -1;
    }

    socket_handle_t handle = as_socket_handle (monitor_);
    if (!handle.socket)
        return -1;

    monitor_handler_state_t *state = find_monitor_handler_state (handle.socket);
    if (!state) {
        errno = EINVAL;
        return -1;
    }

    monitor_snapshot_provider_fn provider =
      state->snapshot_provider.load (std::memory_order_acquire);
    void *subject = state->snapshot_subject.load (std::memory_order_acquire);
    if (!provider) {
        errno = ENOTSUP;
        return -1;
    }

    return provider (subject, out_);
}

void zlink_multipart_close (zlink_msg_t *parts_, size_t part_count_)
{
    if (!parts_)
        return;
    for (size_t i = 0; i < part_count_; ++i)
        zlink_msg_close (&parts_[i]);
}

// Service Discovery API

void *zlink_registry_new (void *ctx_)
{
    if (!ctx_ || !(static_cast<zlink::ctx_t *> (ctx_))->check_tag ()) {
        errno = EFAULT;
        return NULL;
    }
    zlink::registry_t *registry =
      new (std::nothrow) zlink::registry_t (static_cast<zlink::ctx_t *> (ctx_));
    if (!registry) {
        errno = ENOMEM;
        return NULL;
    }
    return static_cast<void *> (registry);
}

int zlink_registry_bind (void *registry_,
                         const char *pub_endpoint_,
                         const char *router_endpoint_)
{
    if (!registry_)
        return -1;
    zlink::registry_t *registry = static_cast<zlink::registry_t *> (registry_);
    if (!registry->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return registry->bind (pub_endpoint_, router_endpoint_);
}

int zlink_registry_set_id (void *registry_, uint32_t registry_id_)
{
    if (!registry_)
        return -1;
    zlink::registry_t *registry = static_cast<zlink::registry_t *> (registry_);
    if (!registry->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return registry->set_id (registry_id_);
}

int zlink_registry_add_peer (void *registry_, const char *peer_pub_endpoint_)
{
    if (!registry_)
        return -1;
    zlink::registry_t *registry = static_cast<zlink::registry_t *> (registry_);
    if (!registry->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return registry->add_peer (peer_pub_endpoint_);
}

int zlink_registry_set_heartbeat (void *registry_,
                                uint32_t interval_ms_,
                                uint32_t timeout_ms_)
{
    if (!registry_)
        return -1;
    zlink::registry_t *registry = static_cast<zlink::registry_t *> (registry_);
    if (!registry->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return registry->set_heartbeat (interval_ms_, timeout_ms_);
}

int zlink_registry_set_broadcast_interval (void *registry_,
                                         uint32_t interval_ms_)
{
    if (!registry_)
        return -1;
    zlink::registry_t *registry = static_cast<zlink::registry_t *> (registry_);
    if (!registry->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return registry->set_broadcast_interval (interval_ms_);
}

int zlink_registry_destroy (void **registry_p_)
{
    if (!registry_p_ || !*registry_p_) {
        errno = EFAULT;
        return -1;
    }
    zlink::registry_t *registry = static_cast<zlink::registry_t *> (*registry_p_);
    if (!registry->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    if (registry->destroy () != 0)
        return -1;
    delete registry;
    *registry_p_ = NULL;
    return 0;
}

int zlink_registry_topology_snapshot (void *registry_,
                                      zlink_registry_topology_entry_t *entries_,
                                      size_t *count_)
{
    if (!registry_)
        return -1;
    zlink::registry_t *registry = static_cast<zlink::registry_t *> (registry_);
    if (!registry->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return registry->topology_snapshot (entries_, count_);
}

int zlink_registry_status_snapshot (void *registry_,
                                    zlink_registry_status_t *out_)
{
    if (!registry_) {
        errno = EFAULT;
        return -1;
    }
    zlink::registry_t *registry = static_cast<zlink::registry_t *> (registry_);
    if (!registry->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return registry->status_snapshot (out_);
}

int zlink_registry_service_summary_snapshot (
  void *registry_,
  const zlink_registry_service_summary_filter_t *filter_,
  zlink_registry_service_summary_entry_t *entries_,
  size_t *count_)
{
    if (!registry_) {
        errno = EFAULT;
        return -1;
    }
    zlink::registry_t *registry = static_cast<zlink::registry_t *> (registry_);
    if (!registry->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    if (!count_) {
        errno = EINVAL;
        return -1;
    }
    std::vector<zlink_registry_service_summary_entry_t> rows;
    if (registry->service_summary_snapshot (filter_, &rows) != 0)
        return -1;
    if (!entries_) {
        *count_ = rows.size ();
        return 0;
    }
    if (*count_ < rows.size ()) {
        *count_ = rows.size ();
        errno = ENOBUFS;
        return -1;
    }
    for (size_t i = 0; i < rows.size (); ++i)
        entries_[i] = rows[i];
    *count_ = rows.size ();
    return 0;
}

int zlink_registry_topology_query (
  void *registry_,
  const zlink_registry_topology_filter_t *filter_,
  zlink_registry_topology_entry_t *entries_,
  size_t *count_)
{
    if (!registry_)
        return -1;
    zlink::registry_t *registry = static_cast<zlink::registry_t *> (registry_);
    if (!registry->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return registry->topology_query (filter_, entries_, count_);
}

int zlink_registry_gateway_peers_snapshot (
  void *registry_,
  zlink_registry_gateway_peer_entry_t *entries_,
  size_t *count_)
{
    if (!registry_)
        return -1;
    zlink::registry_t *registry = static_cast<zlink::registry_t *> (registry_);
    if (!registry->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return registry->gateway_peers_snapshot (entries_, count_);
}

int zlink_registry_gateway_peers_query (
  void *registry_,
  const zlink_registry_gateway_peer_filter_t *filter_,
  zlink_registry_gateway_peer_entry_t *entries_,
  size_t *count_)
{
    if (!registry_)
        return -1;
    zlink::registry_t *registry = static_cast<zlink::registry_t *> (registry_);
    if (!registry->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return registry->gateway_peers_query (filter_, entries_, count_);
}

void *zlink_registry_query_client_new (void *ctx_)
{
    if (!ctx_ || !(static_cast<zlink::ctx_t *> (ctx_))->check_tag ()) {
        errno = EFAULT;
        return NULL;
    }
    registry_query_client_t *client =
      new (std::nothrow)
        registry_query_client_t (static_cast<zlink::ctx_t *> (ctx_));
    if (!client) {
        errno = ENOMEM;
        return NULL;
    }
    return client;
}

int zlink_registry_query_client_connect (void *client_,
                                         const char *endpoint_)
{
    if (!client_) {
        errno = EFAULT;
        return -1;
    }
    registry_query_client_t *client =
      static_cast<registry_query_client_t *> (client_);
    if (!client->check_tag ()) {
        errno = EFAULT;
        return -1;
    }

    zlink::service_public_api_scope_t admission (client->public_api);
    if (!admission.acquired ())
        return -1;

    zlink::scoped_lock_t lock (client->sync);
    return client->connect_locked (endpoint_);
}

int zlink_registry_query_snapshot (
  void *client_,
  const zlink_registry_topology_filter_t *filter_,
  zlink_registry_topology_entry_t *entries_,
  size_t *count_)
{
    if (!client_) {
        errno = EFAULT;
        return -1;
    }
    registry_query_client_t *client =
      static_cast<registry_query_client_t *> (client_);
    if (!client->check_tag () || !client->dealer) {
        errno = EFAULT;
        return -1;
    }

    zlink::service_public_api_scope_t admission (client->public_api);
    if (!admission.acquired ())
        return -1;

    zlink::scoped_lock_t lock (client->sync);
    if (!client->dealer) {
        errno = EFAULT;
        return -1;
    }

    if (zlink::discovery_protocol::send_u16 (
          static_cast<void *> (client->dealer),
          zlink::discovery_protocol::msg_topology_query, ZLINK_SNDMORE)
        < 0)
        return -1;

    zlink_registry_topology_filter_t filter;
    memset (&filter, 0, sizeof (filter));
    if (filter_)
        filter = *filter_;
    if (zlink::discovery_protocol::send_frame (static_cast<void *> (client->dealer),
                                               &filter, sizeof (filter), 0)
        < 0)
        return -1;

    return recv_topology_reply_frames (static_cast<void *> (client->dealer),
                                       entries_, count_);
}

int zlink_registry_query_gateway_peers_snapshot (
  void *client_,
  const zlink_registry_gateway_peer_filter_t *filter_,
  zlink_registry_gateway_peer_entry_t *entries_,
  size_t *count_)
{
    if (!client_) {
        errno = EFAULT;
        return -1;
    }
    registry_query_client_t *client =
      static_cast<registry_query_client_t *> (client_);
    if (!client->check_tag () || !client->dealer) {
        errno = EFAULT;
        return -1;
    }

    zlink::service_public_api_scope_t admission (client->public_api);
    if (!admission.acquired ())
        return -1;

    zlink::scoped_lock_t lock (client->sync);
    if (!client->dealer) {
        errno = EFAULT;
        return -1;
    }

    if (zlink::discovery_protocol::send_u16 (
          static_cast<void *> (client->dealer),
          zlink::discovery_protocol::msg_gateway_peer_query, ZLINK_SNDMORE)
        < 0) {
        return -1;
    }

    zlink_registry_gateway_peer_filter_t filter;
    memset (&filter, 0, sizeof (filter));
    if (filter_)
        filter = *filter_;
    if (zlink::discovery_protocol::send_frame (static_cast<void *> (client->dealer),
                                               &filter, sizeof (filter), 0)
        < 0) {
        return -1;
    }

    return recv_gateway_peer_reply_frames (static_cast<void *> (client->dealer),
                                           entries_, count_);
}

int zlink_registry_query_destroy (void **client_p_)
{
    if (!client_p_ || !*client_p_) {
        errno = EFAULT;
        return -1;
    }
    registry_query_client_t *client =
      static_cast<registry_query_client_t *> (*client_p_);
    if (!client->check_tag ()) {
        errno = EFAULT;
        return -1;
    }

    if (!client->public_api.begin_close_or_fail_busy ())
        return -1;

    {
        zlink::scoped_lock_t lock (client->sync);
        client->destroy_locked ();
    }
    *client_p_ = NULL;
    delete client;
    return 0;
}

void *zlink_discovery_new (void *ctx_, zlink_service_type_t service_type_)
{
    if (!ctx_ || !(static_cast<zlink::ctx_t *> (ctx_))->check_tag ()) {
        errno = EFAULT;
        return NULL;
    }
    if (service_type_ != ZLINK_SERVICE_TYPE_GATEWAY
        && service_type_ != ZLINK_SERVICE_TYPE_SPOT) {
        errno = EINVAL;
        return NULL;
    }
    uint16_t internal_service_type = 0;
    if (service_type_ == ZLINK_SERVICE_TYPE_GATEWAY)
        internal_service_type =
          zlink::discovery_protocol::service_type_gateway_receiver;
    else if (service_type_ == ZLINK_SERVICE_TYPE_SPOT)
        internal_service_type = zlink::discovery_protocol::service_type_spot_node;

    zlink::discovery_t *discovery = new (std::nothrow)
      zlink::discovery_t (static_cast<zlink::ctx_t *> (ctx_),
                          internal_service_type);
    if (!discovery) {
        errno = ENOMEM;
        return NULL;
    }
    return static_cast<void *> (discovery);
}

int zlink_discovery_connect_registry (void *discovery_,
                                      const char *registry_endpoint_)
{
    if (!discovery_)
        return -1;
    zlink::discovery_t *discovery = static_cast<zlink::discovery_t *> (discovery_);
    if (!discovery->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return discovery->connect_registry (registry_endpoint_);
}

int zlink_discovery_set_tls_client (void *discovery_,
                                    const char *ca_cert_,
                                    const char *hostname_,
                                    int trust_system_)
{
    if (!discovery_)
        return -1;
    zlink::discovery_t *discovery =
      static_cast<zlink::discovery_t *> (discovery_);
    if (!discovery->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return discovery->set_tls_client (ca_cert_, hostname_, trust_system_);
}

int zlink_discovery_set_routing_id (void *discovery_,
                                    const void *data_,
                                    size_t size_)
{
    if (!discovery_)
        return -1;
    zlink::discovery_t *discovery =
      static_cast<zlink::discovery_t *> (discovery_);
    if (!discovery->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return discovery->set_routing_id (data_, size_);
}

int zlink_discovery_routing_id (void *discovery_, zlink_routing_id_t *out_)
{
    if (!discovery_)
        return -1;
    zlink::discovery_t *discovery =
      static_cast<zlink::discovery_t *> (discovery_);
    if (!discovery->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return discovery->routing_id (out_);
}

static bool service_monitor_events_request_pub_facet (uint32_t events_)
{
    const uint32_t pub_events =
      ZLINK_SPOT_MONITOR_EVENT_PUB_QUEUE_FULL
      | ZLINK_SPOT_MONITOR_EVENT_PUB_QUEUE_DRAINED
      | ZLINK_SPOT_MONITOR_EVENT_PUB_DELIVERY_READY_CHANGED
      | ZLINK_SPOT_MONITOR_EVENT_PUB_FIRST_DELIVERY_READY_CHANGED;
    return (events_ & pub_events) != 0;
}

static bool service_monitor_events_request_sub_facet (uint32_t events_)
{
    const uint32_t sub_events =
      ZLINK_SPOT_MONITOR_EVENT_SUB_FILTER_APPLIED
      | ZLINK_SPOT_MONITOR_EVENT_SUBSCRIPTION_READY
      | ZLINK_SPOT_MONITOR_EVENT_SUB_DELIVERY_READY_CHANGED;
    return (events_ & sub_events) != 0;
}

static int infer_spot_monitor_role (uint32_t events_)
{
    const bool want_pub = service_monitor_events_request_pub_facet (events_);
    const bool want_sub = service_monitor_events_request_sub_facet (events_);
    if (want_pub && want_sub) {
        errno = EINVAL;
        return -1;
    }
    if (want_pub)
        return ZLINK_SPOT_ROLE_PUB;
    return ZLINK_SPOT_ROLE_SUB;
}

void *zlink_service_monitor_open (
  void *target_, const zlink_service_monitor_open_options_t *options_)
{
    if (!target_) {
        errno = EFAULT;
        return NULL;
    }
    if (!options_) {
        errno = EINVAL;
        return NULL;
    }

    zlink::discovery_t *discovery =
      static_cast<zlink::discovery_t *> (target_);
    if (discovery->check_tag ()) {
        return open_discovery_service_monitor_internal (
          target_,
          static_cast<zlink_discovery_monitor_event_mask_t> (options_->events),
          NULL, NULL);
    }

    if (is_registered_gateway_handle (target_)) {
        return open_gateway_service_monitor_internal (
          target_,
          static_cast<zlink_gateway_monitor_event_mask_t> (options_->events),
          NULL, NULL);
    }

    if (as_spot_pub_side_handle (target_)) {
        return open_spot_service_monitor_internal (
          target_, ZLINK_SPOT_ROLE_PUB,
          static_cast<zlink_spot_monitor_event_mask_t> (options_->events),
          NULL, NULL);
    }

    if (as_spot_sub_side_handle (target_)) {
        return open_spot_service_monitor_internal (
          target_, ZLINK_SPOT_ROLE_SUB,
          static_cast<zlink_spot_monitor_event_mask_t> (options_->events),
          NULL, NULL);
    }

    if (is_registered_spot_handle (target_)) {
        const int role = infer_spot_monitor_role (options_->events);
        if (role < 0)
            return NULL;
        return open_spot_service_monitor_internal (
          target_, static_cast<zlink_spot_role_t> (role),
          static_cast<zlink_spot_monitor_event_mask_t> (options_->events),
          NULL, NULL);
    }

    if (is_registered_spot_node_handle (target_)) {
        const int role = infer_spot_monitor_role (options_->events);
        if (role < 0)
            return NULL;
        return open_spot_node_service_monitor_internal (
          target_, static_cast<zlink_spot_role_t> (role),
          static_cast<zlink_spot_monitor_event_mask_t> (options_->events),
          NULL, NULL);
    }

    errno = EFAULT;
    return NULL;
}

int zlink_service_monitor_handler (void *monitor_,
                                   zlink_service_monitor_handler_fn handler_,
                                   void *userdata_)
{
    return attach_service_monitor_handler_state (monitor_, handler_, userdata_);
}

int zlink_service_monitor_recv (void *monitor_,
                                zlink_service_monitor_event_t *out_)
{
    if (require_monitor_recv_model (monitor_, true) != 0)
        return -1;
    return recv_service_monitor_event_unchecked (monitor_, out_, 0);
}

static void *open_discovery_service_monitor_internal (
  void *discovery_,
  zlink_discovery_monitor_event_mask_t events_,
  zlink_service_monitor_handler_fn handler_,
  void *userdata_)
{
    if (!discovery_)
        return NULL;
    zlink::discovery_t *discovery =
      static_cast<zlink::discovery_t *> (discovery_);
    if (!discovery->check_tag ()) {
        errno = EFAULT;
        return NULL;
    }
    zlink_service_monitor_handler_fn effective_handler = handler_;
    void *monitor = discovery->monitor_open (events_);
    if (!monitor)
        return NULL;
    socket_handle_t handle = as_socket_handle (monitor);
    if (!handle.socket
        || set_monitor_handler_state (handle.socket, NULL, effective_handler,
                                      true, NULL,
                                      discovery,
                                      NULL, userdata_)
             != 0) {
        const int err = errno;
        zlink_monitor_close (&monitor);
        errno = err;
        return NULL;
    }
    return monitor;
}

int zlink_discovery_destroy (void **discovery_p_)
{
    if (!discovery_p_ || !*discovery_p_) {
        errno = EFAULT;
        return -1;
    }
    zlink::discovery_t *discovery = static_cast<zlink::discovery_t *> (*discovery_p_);
    if (!discovery->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    if (has_open_service_monitor_for_subject (discovery)) {
        errno = EBUSY;
        return -1;
    }
    if (discovery->destroy () != 0)
        return -1;
    delete discovery;
    *discovery_p_ = NULL;
    return 0;
}

void *zlink_gateway_new (void *ctx_, const char *service_name_)
{
    if (!ctx_ || !(static_cast<zlink::ctx_t *> (ctx_))->check_tag ()) {
        errno = EFAULT;
        return NULL;
    }
    if (!service_name_ || service_name_[0] == '\0') {
        errno = EINVAL;
        return NULL;
    }
    zlink::gateway_t *gateway =
      new (std::nothrow) zlink::gateway_t (static_cast<zlink::ctx_t *> (ctx_),
                                           service_name_, NULL);
    if (!gateway) {
        errno = ENOMEM;
        return NULL;
    }
    register_gateway_mode_state (gateway);
    return static_cast<void *> (gateway);
}

int zlink_gateway_attach_discovery (void *gateway_, void *discovery_)
{
    if (!gateway_ || !discovery_)
        return -1;
    zlink::gateway_t *gateway = static_cast<zlink::gateway_t *> (gateway_);
    if (!gateway->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    zlink::discovery_t *disc = static_cast<zlink::discovery_t *> (discovery_);
    if (!disc->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return gateway->attach_discovery (disc);
}

int zlink_gateway_bind (void *gateway_, const char *bind_endpoint_)
{
    if (!gateway_)
        return -1;
    zlink::gateway_t *gateway = static_cast<zlink::gateway_t *> (gateway_);
    if (!gateway->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return gateway->bind (bind_endpoint_);
}

int zlink_gateway_connect (void *gateway_,
                           const char *endpoint_,
                           const zlink_routing_id_t *routing_id_)
{
    if (!gateway_)
        return -1;
    zlink::gateway_t *gateway = static_cast<zlink::gateway_t *> (gateway_);
    if (!gateway->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return gateway->connect (endpoint_, routing_id_);
}

int zlink_gateway_disconnect (void *gateway_, const char *endpoint_)
{
    if (!gateway_)
        return -1;
    zlink::gateway_t *gateway = static_cast<zlink::gateway_t *> (gateway_);
    if (!gateway->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return gateway->disconnect (endpoint_);
}

int zlink_gateway_status_snapshot (void *gateway_,
                                   zlink_gateway_status_t *out_)
{
    if (!gateway_) {
        errno = EFAULT;
        return -1;
    }
    zlink::gateway_t *gateway = static_cast<zlink::gateway_t *> (gateway_);
    if (!gateway->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    zlink::service_public_api_scope_t admission (gateway->public_api_guard ());
    if (!admission.acquired ())
        return -1;
    return gateway->snapshot_status (out_);
}

static int gateway_send_parts (void *gateway_,
                               zlink_msg_t *parts_,
                               size_t part_count_,
                               zlink_send_flags_t flags_)
{
    if (!gateway_)
        return -1;
    zlink::gateway_t *gateway = static_cast<zlink::gateway_t *> (gateway_);
    if (!gateway->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return gateway->send (parts_, part_count_, flags_);
}

static int gateway_send_parts_rid (void *gateway_,
                                   const zlink_routing_id_t *routing_id_,
                                   zlink_msg_t *parts_,
                                   size_t part_count_,
                                   zlink_send_flags_t flags_)
{
    if (!gateway_)
        return -1;
    zlink::gateway_t *gateway = static_cast<zlink::gateway_t *> (gateway_);
    if (!gateway->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return gateway->send_rid (routing_id_, parts_, part_count_, flags_);
}

static int gateway_recv_parts (void *gateway_,
                               zlink_routing_id_t *source_rid_out_,
                               zlink_msg_t **parts_,
                               size_t *part_count_,
                               int flags_)
{
    if (!gateway_) {
        errno = EFAULT;
        return -1;
    }
    if (validate_recv_flags (flags_) != 0)
        return -1;
    zlink::gateway_t *gateway = static_cast<zlink::gateway_t *> (gateway_);
    if (!gateway->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    zlink::service_public_api_scope_t admission (gateway->public_api_guard ());
    if (!admission.acquired ())
        return -1;
    if (gateway_require_recv_model (gateway) != 0)
        return -1;
    zlink::socket_base_t *router =
      zlink::gateway_access_t::router_socket (gateway);
    if (!router) {
        errno = ENOTSUP;
        return -1;
    }
    return recv_gateway_parts (router, source_rid_out_, parts_, part_count_,
                               flags_);
}

int zlink_gateway_set_lb_strategy (void *gateway_,
                                   zlink_gateway_lb_strategy_t strategy_)
{
    if (!gateway_)
        return -1;
    zlink::gateway_t *gateway = static_cast<zlink::gateway_t *> (gateway_);
    if (!gateway->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return gateway->set_lb_strategy (strategy_);
}

static void *open_gateway_service_monitor_internal (
  void *gateway_,
  zlink_gateway_monitor_event_mask_t events_,
  zlink_service_monitor_handler_fn handler_,
  void *userdata_)
{
    if (!gateway_)
        return NULL;
    zlink::gateway_t *gateway = static_cast<zlink::gateway_t *> (gateway_);
    if (!gateway->check_tag ()) {
        errno = EFAULT;
        return NULL;
    }
    zlink_service_monitor_handler_fn effective_handler = handler_;
    void *monitor = gateway->monitor_open (events_);
    if (!monitor)
        return NULL;
    socket_handle_t handle = as_socket_handle (monitor);
    if (!handle.socket
        || set_monitor_handler_state (handle.socket, NULL, effective_handler,
                                      true,
                                      &gateway_monitor_snapshot_provider,
                                      static_cast<void *> (gateway),
                                      NULL, userdata_)
             != 0) {
        const int err = errno;
        zlink_monitor_close (&monitor);
        errno = err;
        return NULL;
    }
    return monitor;
}

int zlink_gateway_update_peer_weight (void *gateway_,
                                      const zlink_routing_id_t *routing_id_,
                                      uint32_t weight_)
{
    if (!gateway_)
        return -1;
    zlink::gateway_t *gateway = static_cast<zlink::gateway_t *> (gateway_);
    if (!gateway->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return gateway->update_peer_weight (routing_id_, weight_);
}

int zlink_gateway_destroy (void **gateway_p_)
{
    if (!gateway_p_ || !*gateway_p_) {
        errno = EFAULT;
        return -1;
    }
    zlink::gateway_t *gateway = static_cast<zlink::gateway_t *> (*gateway_p_);
    if (!gateway->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    if (!gateway->public_api_guard ().begin_close_or_fail_busy ())
        return -1;
    if (has_open_service_monitor_for_subject (gateway)) {
        gateway->public_api_guard ().cancel_close ();
        errno = EBUSY;
        return -1;
    }
    if (gateway->destroy () != 0) {
        gateway->public_api_guard ().cancel_close ();
        return -1;
    }
    erase_gateway_mode_state (gateway);
    delete gateway;
    *gateway_p_ = NULL;
    return 0;
}

void *zlink_spot_node_new (void *ctx_, const char *service_name_)
{
    if (!ctx_ || !(static_cast<zlink::ctx_t *> (ctx_))->check_tag ()) {
        errno = EFAULT;
        return NULL;
    }
    if (!service_name_ || service_name_[0] == '\0') {
        errno = EINVAL;
        return NULL;
    }
    zlink::spot_node_t *node =
      new (std::nothrow)
        zlink::spot_node_t (static_cast<zlink::ctx_t *> (ctx_), service_name_);
    if (!node) {
        errno = ENOMEM;
        return NULL;
    }
    if (!node->check_tag ()) {
        delete node;
        errno = EINVAL;
        return NULL;
    }
    register_spot_node_mode_state (node);
    return static_cast<void *> (node);
}

int zlink_spot_node_destroy (void **node_p_)
{
    if (!node_p_ || !*node_p_) {
        errno = EFAULT;
        return -1;
    }
    zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (*node_p_);
    if (!node->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    if (in_spot_node_send_ready_callback (node)
        || in_spot_node_monitor_callback (node)) {
        errno = EBUSY;
        return -1;
    }
    if (!node->public_api_guard ().begin_close_or_fail_busy ())
        return -1;
    {
        spot_node_handler_registry_t &registry = spot_node_handler_registry ();
        zlink::scoped_lock_t lock (registry.sync);
        registry.handlers.erase (node);
    }
    if (node->destroy () != 0) {
        node->public_api_guard ().cancel_close ();
        return -1;
    }
    erase_spot_node_mode_state (node);
    delete node;
    *node_p_ = NULL;
    return 0;
}

int zlink_spot_node_bind (void *node_, const char *endpoint_)
{
    if (!node_)
        return -1;
    zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (node_);
    if (!node->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return node->bind (endpoint_);
}

int zlink_spot_node_connect_peer (void *node_,
                                  const char *peer_endpoint_)
{
    if (!node_)
        return -1;
    zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (node_);
    if (!node->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return node->connect_peer_pub (peer_endpoint_);
}

int zlink_spot_node_disconnect_peer (void *node_,
                                     const char *peer_endpoint_)
{
    if (!node_)
        return -1;
    zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (node_);
    if (!node->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return node->disconnect_peer_pub (peer_endpoint_);
}

int zlink_spot_node_status_snapshot (void *node_,
                                     zlink_spot_node_status_t *out_)
{
    if (!node_) {
        errno = EFAULT;
        return -1;
    }
    zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (node_);
    if (!node->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    zlink::service_public_api_scope_t admission (node->public_api_guard ());
    if (!admission.acquired ())
        return -1;
    return node->snapshot_status (out_);
}

int zlink_spot_node_peers_snapshot (void *node_,
                                    zlink_spot_node_peer_entry_t *entries_,
                                    size_t *count_)
{
    if (!node_) {
        errno = EFAULT;
        return -1;
    }
    zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (node_);
    if (!node->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    zlink::service_public_api_scope_t admission (node->public_api_guard ());
    if (!admission.acquired ())
        return -1;
    if (!count_) {
        errno = EINVAL;
        return -1;
    }
    std::vector<zlink_spot_node_peer_entry_t> rows;
    if (node->snapshot_peers (NULL, &rows) != 0)
        return -1;
    if (!entries_) {
        *count_ = rows.size ();
        return 0;
    }
    if (*count_ < rows.size ()) {
        *count_ = rows.size ();
        errno = ENOBUFS;
        return -1;
    }
    for (size_t i = 0; i < rows.size (); ++i)
        entries_[i] = rows[i];
    *count_ = rows.size ();
    return 0;
}

int zlink_spot_node_peers_query (void *node_,
                                 const zlink_spot_node_peer_filter_t *filter_,
                                 zlink_spot_node_peer_entry_t *entries_,
                                 size_t *count_)
{
    if (!node_) {
        errno = EFAULT;
        return -1;
    }
    zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (node_);
    if (!node->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    zlink::service_public_api_scope_t admission (node->public_api_guard ());
    if (!admission.acquired ())
        return -1;
    if (!count_) {
        errno = EINVAL;
        return -1;
    }
    std::vector<zlink_spot_node_peer_entry_t> rows;
    if (node->snapshot_peers (filter_, &rows) != 0)
        return -1;
    if (!entries_) {
        *count_ = rows.size ();
        return 0;
    }
    if (*count_ < rows.size ()) {
        *count_ = rows.size ();
        errno = ENOBUFS;
        return -1;
    }
    for (size_t i = 0; i < rows.size (); ++i)
        entries_[i] = rows[i];
    *count_ = rows.size ();
    return 0;
}

int zlink_spot_node_subjects_snapshot (
  void *node_,
  const zlink_spot_node_subject_filter_t *filter_,
  zlink_spot_node_subject_entry_t *entries_,
  size_t *count_)
{
    if (!node_) {
        errno = EFAULT;
        return -1;
    }
    zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (node_);
    if (!node->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    zlink::service_public_api_scope_t admission (node->public_api_guard ());
    if (!admission.acquired ())
        return -1;
    if (!count_) {
        errno = EINVAL;
        return -1;
    }
    std::vector<zlink_spot_node_subject_entry_t> rows;
    if (node->snapshot_subjects (filter_, &rows) != 0)
        return -1;
    if (!entries_) {
        *count_ = rows.size ();
        return 0;
    }
    if (*count_ < rows.size ()) {
        *count_ = rows.size ();
        errno = ENOBUFS;
        return -1;
    }
    for (size_t i = 0; i < rows.size (); ++i)
        entries_[i] = rows[i];
    *count_ = rows.size ();
    return 0;
}

int zlink_spot_node_attach_discovery (void *node_, void *discovery_)
{
    if (!node_ || !discovery_)
        return -1;
    zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (node_);
    if (!node->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    zlink::discovery_t *disc = static_cast<zlink::discovery_t *> (discovery_);
    if (!disc->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return node->attach_discovery (disc);
}

static int spot_node_publish_internal (void *node_,
                                       const char *topic_id_,
                                       zlink_msg_t *parts_,
                                       size_t part_count_,
                                       zlink_send_flags_t flags_)
{
    if (!node_)
        return -1;
    zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (node_);
    if (!node->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    zlink::service_public_api_scope_t admission (node->public_api_guard ());
    if (!admission.acquired ())
        return -1;
    zlink::spot_pub_t *pub = node->ensure_default_pub ();
    if (!pub)
        return -1;
    return pub->publish (topic_id_, parts_, part_count_, flags_);
}

static int spot_node_subscribe_internal (void *node_, const char *topic_id_)
{
    if (!node_)
        return -1;
    zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (node_);
    if (!node->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    zlink::service_public_api_scope_t admission (node->public_api_guard ());
    if (!admission.acquired ())
        return -1;
    zlink::spot_internal_receiver_t *receiver =
      zlink::spot_node_access_t::ensure_internal_receiver (node);
    if (!receiver)
        return -1;
    return receiver->subscribe (topic_id_);
}

static int spot_node_subscribe_pattern_internal (void *node_,
                                                 const char *pattern_)
{
    if (!node_)
        return -1;
    zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (node_);
    if (!node->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    zlink::service_public_api_scope_t admission (node->public_api_guard ());
    if (!admission.acquired ())
        return -1;
    zlink::spot_internal_receiver_t *receiver =
      zlink::spot_node_access_t::ensure_internal_receiver (node);
    if (!receiver)
        return -1;
    return receiver->subscribe_pattern (pattern_);
}

static int spot_node_unsubscribe_internal (void *node_,
                                           const char *topic_id_or_pattern_)
{
    if (!node_)
        return -1;
    zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (node_);
    if (!node->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    zlink::service_public_api_scope_t admission (node->public_api_guard ());
    if (!admission.acquired ())
        return -1;
    zlink::spot_internal_receiver_t *receiver =
      zlink::spot_node_access_t::ensure_internal_receiver (node);
    if (!receiver)
        return -1;
    return receiver->unsubscribe (topic_id_or_pattern_);
}

static void *open_spot_node_service_monitor_internal (
  void *node_,
  zlink_spot_role_t role_,
  zlink_spot_monitor_event_mask_t events_,
  zlink_service_monitor_handler_fn handler_,
  void *userdata_)
{
    if (!node_) {
        errno = EFAULT;
        return NULL;
    }
    zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (node_);
    if (!node->check_tag ()) {
        errno = EFAULT;
        return NULL;
    }
    zlink::service_public_api_scope_t admission (node->public_api_guard ());
    if (!admission.acquired ())
        return NULL;

    if (role_ == ZLINK_SPOT_ROLE_PUB) {
        zlink::spot_pub_t *pub = node->ensure_default_pub ();
        if (!pub)
            return NULL;
        return open_spot_service_monitor (
          pub->monitor_open (events_), handler_,
          &spot_pub_monitor_snapshot_provider,
          static_cast<void *> (pub), userdata_);
    }
    if (role_ == ZLINK_SPOT_ROLE_SUB) {
        zlink::spot_internal_receiver_t *receiver =
          zlink::spot_node_access_t::ensure_internal_receiver (node);
        if (!receiver)
            return NULL;
        return open_spot_service_monitor (
          receiver->monitor_open (events_), handler_,
          &spot_internal_receiver_monitor_snapshot_provider,
          static_cast<void *> (receiver), userdata_);
    }

    errno = EINVAL;
    return NULL;
}

static int spot_publish_internal (void *spot_,
                                  const char *topic_id_,
                                  zlink_msg_t *parts_,
                                  size_t part_count_,
                                  zlink_send_flags_t flags_)
{
    if (zlink::spot_pub_t *pub = as_spot_pub_side_handle (spot_))
        return pub->publish (topic_id_, parts_, part_count_, flags_);

    spot_handle_t *spot = as_spot_handle (spot_);
    if (!spot)
        return -1;
    zlink::service_public_api_scope_t admission (spot->public_api);
    if (!admission.acquired ())
        return -1;
    zlink::spot_pub_t *pub = ensure_spot_pub (spot);
    if (!pub) {
        errno = ENOTSUP;
        return -1;
    }
    return pub->publish (topic_id_, parts_, part_count_, flags_);
}

static int spot_sub_recv_internal (void *sub_,
                                   zlink_routing_id_t *source_rid_out_,
                                   zlink_msg_t **parts_,
                                   size_t *part_count_,
                                   char *topic_id_out_,
                                   size_t *topic_id_len_,
                                   zlink_send_flags_t flags_)
{
    if (zlink::spot_sub_t *sub = as_spot_sub_side_handle (sub_)) {
        if (validate_recv_flags (flags_) != 0)
            return -1;
        return sub->recv (source_rid_out_, parts_, part_count_, flags_,
                          topic_id_out_, topic_id_len_);
    }

    spot_handle_t *spot = as_spot_handle (sub_);
    if (!spot)
        return -1;
    if (validate_recv_flags (flags_) != 0)
        return -1;
    zlink::service_public_api_scope_t admission (spot->public_api);
    if (!admission.acquired ())
        return -1;
    if (spot_require_recv_model (spot) != 0)
        return -1;
    zlink::spot_sub_t *sub = ensure_spot_sub (spot);
    if (!sub) {
        errno = ENOTSUP;
        return -1;
    }
    return sub->recv (source_rid_out_, parts_, part_count_, flags_, topic_id_out_,
                      topic_id_len_);
}

static int spot_node_recv_internal (void *node_,
                                    zlink_routing_id_t *source_rid_out_,
                                    zlink_msg_t **parts_,
                                    size_t *part_count_,
                                    char *topic_id_out_,
                                    size_t *topic_id_len_,
                                    zlink_send_flags_t flags_)
{
    if (!node_) {
        errno = EFAULT;
        return -1;
    }
    if (validate_recv_flags (flags_) != 0)
        return -1;
    zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (node_);
    if (!node->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    zlink::service_public_api_scope_t admission (node->public_api_guard ());
    if (!admission.acquired ())
        return -1;
    if (spot_node_require_recv_model (node) != 0)
        return -1;
    zlink::spot_internal_receiver_t *receiver =
      zlink::spot_node_access_t::ensure_internal_receiver (node);
    if (!receiver || !receiver->impl ()) {
        errno = ENOTSUP;
        return -1;
    }
    return receiver->impl ()->recv (source_rid_out_, parts_, part_count_, flags_,
                                    topic_id_out_, topic_id_len_);
}

int zlink_xpub_recv (void *s_,
                     zlink_routing_id_t *source_rid_out_,
                     int *subscribed_out_,
                     char *topic_id_out_,
                     size_t *topic_id_len_,
                     zlink_send_flags_t flags_)
{
    socket_handle_t handle = as_socket_handle (s_);
    if (!handle.socket)
        return -1;
    if (validate_recv_flags (flags_) != 0)
        return -1;
    if (!subscribed_out_ || !topic_id_len_) {
        errno = EFAULT;
        return -1;
    }
    if (!topic_id_out_ && *topic_id_len_ != 0) {
        errno = EFAULT;
        return -1;
    }
    if (socket_type (handle) != ZLINK_CORE_SOCKET_XPUB) {
        errno = EINVAL;
        return -1;
    }

    zlink_msg_t msg;
    zlink_msg_init (&msg);
    if (zlink::recv_msg_internal (handle.socket, &msg, flags_) < 0) {
        zlink_msg_close (&msg);
        return -1;
    }

    if (source_rid_out_) {
        memset (source_rid_out_, 0, sizeof (*source_rid_out_));
        handle.socket->copy_last_recv_source_rid (source_rid_out_);
    }

    const unsigned char *data =
      static_cast<const unsigned char *> (zlink_msg_data (&msg));
    const size_t size = zlink_msg_size (&msg);
    const size_t topic_len = size > 0 ? size - 1 : 0;
    *subscribed_out_ = size > 0 && data[0] != 0 ? 1 : 0;

    if (*topic_id_len_ < topic_len) {
        *topic_id_len_ = topic_len;
        zlink_msg_close (&msg);
        errno = EMSGSIZE;
        return -1;
    }

    if (topic_id_out_ && topic_len > 0)
        memcpy (topic_id_out_, data + 1, topic_len);
    *topic_id_len_ = topic_len;

    zlink_msg_close (&msg);
    errno = 0;
    return 0;
}

static int spot_subscribe_internal (void *spot_, const char *topic_id_)
{
    if (zlink::spot_sub_t *sub = as_spot_sub_side_handle (spot_))
        return sub->subscribe (topic_id_);

    spot_handle_t *spot = as_spot_handle (spot_);
    if (!spot)
        return -1;
    zlink::service_public_api_scope_t admission (spot->public_api);
    if (!admission.acquired ())
        return -1;
    zlink::spot_sub_t *sub = ensure_spot_sub (spot);
    if (!sub) {
        errno = ENOTSUP;
        return -1;
    }
    return sub->subscribe (topic_id_);
}

static int spot_subscribe_pattern_internal (void *spot_, const char *pattern_)
{
    if (zlink::spot_sub_t *sub = as_spot_sub_side_handle (spot_))
        return sub->subscribe_pattern (pattern_);

    spot_handle_t *spot = as_spot_handle (spot_);
    if (!spot)
        return -1;
    zlink::service_public_api_scope_t admission (spot->public_api);
    if (!admission.acquired ())
        return -1;
    zlink::spot_sub_t *sub = ensure_spot_sub (spot);
    if (!sub) {
        errno = ENOTSUP;
        return -1;
    }
    return sub->subscribe_pattern (pattern_);
}

static int spot_unsubscribe_internal (void *spot_,
                                      const char *topic_id_or_pattern_)
{
    if (zlink::spot_sub_t *sub = as_spot_sub_side_handle (spot_))
        return sub->unsubscribe (topic_id_or_pattern_);

    spot_handle_t *spot = as_spot_handle (spot_);
    if (!spot)
        return -1;
    zlink::service_public_api_scope_t admission (spot->public_api);
    if (!admission.acquired ())
        return -1;
    zlink::spot_sub_t *sub = ensure_spot_sub (spot);
    if (!sub) {
        errno = ENOTSUP;
        return -1;
    }
    return sub->unsubscribe (topic_id_or_pattern_);
}

static int spot_set_pub_option_internal (void *spot_,
                                         zlink_spot_pub_option_t option_,
                                         const void *optval_,
                                         size_t optvallen_)
{
    if (zlink::spot_pub_t *pub = as_spot_pub_side_handle (spot_))
        return pub->set_option (option_, optval_, optvallen_);

    spot_handle_t *spot = as_spot_handle (spot_);
    if (!spot)
        return -1;
    zlink::service_public_api_scope_t admission (spot->public_api);
    if (!admission.acquired ())
        return -1;
    zlink::spot_pub_t *pub = ensure_spot_pub (spot);
    if (!pub) {
        errno = ENOTSUP;
        return -1;
    }
    return pub->set_option (option_, optval_, optvallen_);
}

static int spot_set_sub_option_internal (void *spot_,
                                         zlink_spot_sub_option_t option_,
                                         const void *optval_,
                                         size_t optvallen_)
{
    if (zlink::spot_sub_t *sub = as_spot_sub_side_handle (spot_))
        return sub->set_option (option_, optval_, optvallen_);

    spot_handle_t *spot = as_spot_handle (spot_);
    if (!spot)
        return -1;
    zlink::service_public_api_scope_t admission (spot->public_api);
    if (!admission.acquired ())
        return -1;
    if (spot->sub)
        return spot->sub->set_option (option_, optval_, optvallen_);
    if (validate_spot_sub_option (option_, optval_, optvallen_) != 0)
        return -1;
    store_spot_pending_sub_option (spot, option_, optval_, optvallen_);
    return 0;
}

static void *open_spot_service_monitor (void *monitor_,
                                        zlink_service_monitor_handler_fn handler_,
                                        monitor_snapshot_provider_fn snapshot_provider_,
                                        void *snapshot_subject_,
                                        void *userdata_)
{
    if (!monitor_)
        return NULL;
    zlink_service_monitor_handler_fn effective_handler = handler_;
    socket_handle_t handle = as_socket_handle (monitor_);
    if (!handle.socket) {
        zlink_monitor_close (&monitor_);
        errno = EFAULT;
        return NULL;
    }
    if (set_monitor_handler_state (handle.socket, NULL, effective_handler, true,
                                   snapshot_provider_, snapshot_subject_,
                                   NULL, userdata_)
        != 0) {
        const int err = errno;
        zlink_monitor_close (&monitor_);
        errno = err;
        return NULL;
    }
    return monitor_;
}

static int spot_pub_publish_internal (void *spot_pub_,
                                      const char *topic_id_,
                                      zlink_msg_t *parts_,
                                      size_t part_count_,
                                      zlink_send_flags_t flags_)
{
    zlink::spot_pub_t *pub = as_spot_pub_side_handle (spot_pub_);
    if (!pub) {
        errno = EFAULT;
        return -1;
    }
    return pub->publish (topic_id_, parts_, part_count_, flags_);
}

static int spot_pub_send_ready_handler_internal (
  void *spot_pub_,
  zlink_send_ready_handler_fn handler_,
  void *userdata_)
{
    zlink::spot_pub_t *pub = as_spot_pub_side_handle (spot_pub_);
    if (!pub) {
        errno = EFAULT;
        return -1;
    }
    if (pub->node ()) {
        zlink::service_public_api_scope_t admission (
          pub->node ()->public_api_guard ());
        if (!admission.acquired ())
            return -1;
    }
    void *subject = spot_pub_;
    if (pub->is_node_owned_default () && pub->node ())
        subject = pub->node ();
    return pub->set_send_ready_handler (handler_, subject, userdata_);
}

static int spot_sub_subscribe_internal (void *spot_sub_, const char *topic_id_)
{
    zlink::spot_sub_t *sub = as_spot_sub_side_handle (spot_sub_);
    if (!sub) {
        errno = EFAULT;
        return -1;
    }
    return sub->subscribe (topic_id_);
}

static int spot_sub_subscribe_pattern_internal (void *spot_sub_,
                                                const char *pattern_)
{
    zlink::spot_sub_t *sub = as_spot_sub_side_handle (spot_sub_);
    if (!sub) {
        errno = EFAULT;
        return -1;
    }
    return sub->subscribe_pattern (pattern_);
}

static int spot_sub_unsubscribe_internal (
  void *spot_sub_,
  const char *topic_id_or_pattern_)
{
    zlink::spot_sub_t *sub = as_spot_sub_side_handle (spot_sub_);
    if (!sub) {
        errno = EFAULT;
        return -1;
    }
    return sub->unsubscribe (topic_id_or_pattern_);
}

static void *spot_pub_monitor_open_internal (
  void *spot_pub_,
  zlink_spot_monitor_event_mask_t events_,
  zlink_service_monitor_handler_fn handler_,
  void *userdata_)
{
    zlink::spot_pub_t *pub = as_spot_pub_side_handle (spot_pub_);
    if (!pub) {
        errno = EFAULT;
        return NULL;
    }
    if (pub->node ()) {
        zlink::service_public_api_scope_t admission (
          pub->node ()->public_api_guard ());
        if (!admission.acquired ())
            return NULL;
    }
    return open_spot_service_monitor (
      pub->monitor_open (events_), handler_, &spot_pub_monitor_snapshot_provider,
      static_cast<void *> (pub), userdata_);
}

static void *spot_sub_monitor_open_internal (
  void *spot_sub_,
  zlink_spot_monitor_event_mask_t events_,
  zlink_service_monitor_handler_fn handler_,
  void *userdata_)
{
    zlink::spot_sub_t *sub = as_spot_sub_side_handle (spot_sub_);
    if (!sub) {
        errno = EFAULT;
        return NULL;
    }
    if (sub->node ()) {
        zlink::service_public_api_scope_t admission (
          sub->node ()->public_api_guard ());
        if (!admission.acquired ())
            return NULL;
    }
    return open_spot_service_monitor (
      sub->monitor_open (events_), handler_, &spot_sub_monitor_snapshot_provider,
      static_cast<void *> (sub), userdata_);
}

static void *open_spot_service_monitor_internal (
  void *spot_,
  zlink_spot_role_t role_,
  zlink_spot_monitor_event_mask_t events_,
  zlink_service_monitor_handler_fn handler_,
  void *userdata_)
{
    if (role_ == ZLINK_SPOT_ROLE_PUB && as_spot_pub_side_handle (spot_))
        return spot_pub_monitor_open_internal (spot_, events_, handler_, userdata_);
    if (role_ == ZLINK_SPOT_ROLE_SUB && as_spot_sub_side_handle (spot_))
        return spot_sub_monitor_open_internal (spot_, events_, handler_, userdata_);

    spot_handle_t *spot = as_spot_handle (spot_);
    if (!spot)
        return NULL;
    zlink::service_public_api_scope_t admission (spot->public_api);
    if (!admission.acquired ())
        return NULL;

    if (role_ == ZLINK_SPOT_ROLE_PUB) {
        zlink::spot_pub_t *pub = ensure_spot_pub (spot);
        if (!pub) {
            errno = ENOTSUP;
            return NULL;
        }
        return open_spot_service_monitor (
          pub->monitor_open (events_), handler_,
          &spot_pub_monitor_snapshot_provider,
          static_cast<void *> (pub), userdata_);
    }
    if (role_ == ZLINK_SPOT_ROLE_SUB) {
        zlink::spot_sub_t *sub = ensure_spot_sub (spot);
        if (!sub) {
            errno = ENOTSUP;
            return NULL;
        }
        return open_spot_service_monitor (
          sub->monitor_open (events_), handler_,
          &spot_sub_monitor_snapshot_provider,
          static_cast<void *> (sub), userdata_);
    }

    errno = EINVAL;
    return NULL;
}

static int spot_node_set_pub_option_internal (void *node_,
                                              zlink_spot_pub_option_t option_,
                                              const void *optval_,
                                              size_t optvallen_)
{
    if (!node_)
        return -1;
    zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (node_);
    if (!node->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return node->set_pub_option (option_, optval_, optvallen_);
}

static int spot_node_set_sub_option_internal (void *node_,
                                              zlink_spot_sub_option_t option_,
                                              const void *optval_,
                                              size_t optvallen_)
{
    if (!node_)
        return -1;
    zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (node_);
    if (!node->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return node->set_sub_option (option_, optval_, optvallen_);
}

struct spot_subscription_less_t
{
    bool operator() (
      const zlink::spot_sub_t::subject_descriptor_t &lhs_,
      const zlink::spot_sub_t::subject_descriptor_t &rhs_) const
    {
        if (lhs_.subject != rhs_.subject)
            return lhs_.subject < rhs_.subject;
        return lhs_.subject_kind < rhs_.subject_kind;
    }
};

static int map_common_to_spot_pub_option (zlink_option_t option_)
{
    switch (option_) {
        case ZLINK_OPT_SNDHWM:
            return ZLINK_SPOT_PUB_OPT_SNDHWM;
        case ZLINK_OPT_SNDTIMEO:
            return ZLINK_SPOT_PUB_OPT_SNDTIMEO;
        case ZLINK_OPT_LINGER:
            return ZLINK_SPOT_PUB_OPT_LINGER;
        case ZLINK_OPT_SNDBUF:
            return ZLINK_SPOT_PUB_OPT_SNDBUF;
        case ZLINK_OPT_RCVBUF:
            return ZLINK_SPOT_PUB_OPT_RCVBUF;
        default:
            return -1;
    }
}

static int map_common_to_socket_option (zlink_option_t option_)
{
    switch (option_) {
        case ZLINK_OPT_SNDHWM:
            return ZLINK_INTERNAL_OPT_SNDHWM;
        case ZLINK_OPT_RCVHWM:
            return ZLINK_INTERNAL_OPT_RCVHWM;
        case ZLINK_OPT_LINGER:
            return ZLINK_INTERNAL_OPT_LINGER;
        case ZLINK_OPT_SNDTIMEO:
            return ZLINK_INTERNAL_OPT_SNDTIMEO;
        case ZLINK_OPT_RCVTIMEO:
            return ZLINK_INTERNAL_OPT_RCVTIMEO;
        case ZLINK_OPT_SNDBUF:
            return ZLINK_INTERNAL_OPT_SNDBUF;
        case ZLINK_OPT_RCVBUF:
            return ZLINK_INTERNAL_OPT_RCVBUF;
        default:
            return -1;
    }
}

static int map_pub_to_socket_option (zlink_pub_option_t option_)
{
    switch (option_) {
        case ZLINK_PUB_OPT_NODROP:
            return ZLINK_INTERNAL_OPT_XPUB_NODROP;
        case ZLINK_PUB_OPT_TOPICS_COUNT:
            return ZLINK_INTERNAL_OPT_TOPICS_COUNT;
        default:
            return -1;
    }
}

static int map_sub_to_socket_option (zlink_sub_option_t option_)
{
    switch (option_) {
        case ZLINK_SUB_OPT_TOPICS_COUNT:
            return ZLINK_INTERNAL_OPT_TOPICS_COUNT;
        default:
            return -1;
    }
}

static int map_common_to_spot_sub_option (zlink_option_t option_)
{
    switch (option_) {
        case ZLINK_OPT_RCVHWM:
            return ZLINK_SPOT_SUB_OPT_RCVHWM;
        case ZLINK_OPT_LINGER:
            return ZLINK_SPOT_SUB_OPT_LINGER;
        case ZLINK_OPT_SNDBUF:
            return ZLINK_SPOT_SUB_OPT_SNDBUF;
        case ZLINK_OPT_RCVBUF:
            return ZLINK_SPOT_SUB_OPT_RCVBUF;
        case ZLINK_OPT_RCVTIMEO:
            return ZLINK_SPOT_SUB_OPT_RCVTIMEO;
        default:
            return -1;
    }
}

static int copy_subscription_subject (
  const zlink::spot_sub_t::subject_descriptor_t &subject_,
  char *filter_out_,
  size_t *filter_len_inout_,
  int *is_pattern_out_)
{
    if (!filter_len_inout_) {
        errno = EFAULT;
        return -1;
    }
    if (!filter_out_ && *filter_len_inout_ != 0) {
        errno = EFAULT;
        return -1;
    }
    if (*filter_len_inout_ < subject_.subject.size ()) {
        *filter_len_inout_ = subject_.subject.size ();
        errno = EINVAL;
        return -1;
    }
    if (filter_out_ && !subject_.subject.empty ())
        memcpy (filter_out_, subject_.subject.data (), subject_.subject.size ());
    *filter_len_inout_ = subject_.subject.size ();
    if (is_pattern_out_)
        *is_pattern_out_ =
          subject_.subject_kind == ZLINK_SERVICE_EVENT_SUBJECT_PATTERN ? 1 : 0;
    return 0;
}

int zlink_spot_subject_set_common_option_internal (void *handle_,
                                                   zlink_option_t option_,
                                                   const void *optval_,
                                                   size_t optvallen_)
{
    const int pub_option = map_common_to_spot_pub_option (option_);
    const int sub_option = map_common_to_spot_sub_option (option_);
    if (pub_option < 0 && sub_option < 0) {
        errno = EINVAL;
        return -1;
    }

    if (is_registered_spot_handle (handle_)) {
        spot_handle_t *spot = static_cast<spot_handle_t *> (handle_);
        zlink::service_public_api_scope_t admission (spot->public_api);
        if (!admission.acquired ())
            return -1;
        if (pub_option >= 0) {
            zlink::spot_pub_t *pub = ensure_spot_pub (spot);
            if (!pub)
                return -1;
            if (pub->set_option (pub_option, optval_, optvallen_) != 0)
                return -1;
        }
        if (sub_option >= 0) {
            zlink::spot_sub_t *sub = ensure_spot_sub (spot);
            if (!sub)
                return -1;
            if (sub->set_option (sub_option, optval_, optvallen_) != 0)
                return -1;
        }
        return 0;
    }

    if (is_registered_spot_node_handle (handle_)) {
        zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (handle_);
        zlink::service_public_api_scope_t admission (node->public_api_guard ());
        if (!admission.acquired ())
            return -1;
        if (pub_option >= 0
            && node->set_pub_option (pub_option, optval_, optvallen_) != 0)
            return -1;
        if (sub_option >= 0
            && node->set_sub_option (sub_option, optval_, optvallen_) != 0)
            return -1;
        return 0;
    }

    errno = EFAULT;
    return -1;
}

int zlink_spot_subject_get_common_option_internal (void *handle_,
                                                   zlink_option_t option_,
                                                   void *optval_,
                                                   size_t *optvallen_)
{
    const int socket_option = map_common_to_socket_option (option_);
    const int pub_option = map_common_to_spot_pub_option (option_);
    const int sub_option = map_common_to_spot_sub_option (option_);
    if (socket_option < 0 || (pub_option < 0 && sub_option < 0)) {
        errno = EINVAL;
        return -1;
    }

    if (is_registered_spot_handle (handle_)) {
        spot_handle_t *spot = static_cast<spot_handle_t *> (handle_);
        zlink::service_public_api_scope_t admission (spot->public_api);
        if (!admission.acquired ())
            return -1;
        if (pub_option >= 0) {
            zlink::spot_pub_t *pub = ensure_spot_pub (spot);
            if (!pub || !pub->poller_socket ()) {
                errno = EFAULT;
                return -1;
            }
            return pub->poller_socket ()->getsockopt (socket_option, optval_,
                                                      optvallen_);
        }
        zlink::spot_sub_t *sub = ensure_spot_sub (spot);
        if (!sub || !sub->poller_socket ()) {
            errno = EFAULT;
            return -1;
        }
        return sub->poller_socket ()->getsockopt (socket_option, optval_, optvallen_);
    }

    if (is_registered_spot_node_handle (handle_)) {
        zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (handle_);
        zlink::service_public_api_scope_t admission (node->public_api_guard ());
        if (!admission.acquired ())
            return -1;
        if (pub_option >= 0) {
            zlink::spot_pub_t *pub = node->ensure_default_pub ();
            if (!pub || !pub->poller_socket ()) {
                errno = EFAULT;
                return -1;
            }
            return pub->poller_socket ()->getsockopt (socket_option, optval_,
                                                      optvallen_);
        }
        zlink::spot_sub_t *sub = node->ensure_default_sub ();
        if (!sub || !sub->poller_socket ()) {
            errno = EFAULT;
            return -1;
        }
        return sub->poller_socket ()->getsockopt (socket_option, optval_, optvallen_);
    }

    errno = EFAULT;
    return -1;
}

int zlink_spot_subject_set_pub_option_internal (void *handle_,
                                                zlink_pub_option_t option_,
                                                const void *optval_,
                                                size_t optvallen_)
{
    switch (option_) {
        case ZLINK_PUB_OPT_NODROP:
            break;
        default:
            errno = EINVAL;
            return -1;
    }

    if (is_registered_spot_handle (handle_))
        return spot_set_pub_option_internal (handle_, ZLINK_SPOT_PUB_OPT_NODROP,
                                             optval_, optvallen_);
    if (is_registered_spot_node_handle (handle_))
        return spot_node_set_pub_option_internal (
          handle_, ZLINK_SPOT_PUB_OPT_NODROP, optval_, optvallen_);

    errno = EFAULT;
    return -1;
}

int zlink_spot_subject_get_pub_option_internal (void *handle_,
                                                zlink_pub_option_t option_,
                                                void *optval_,
                                                size_t *optvallen_)
{
    const int socket_option = map_pub_to_socket_option (option_);
    if (socket_option < 0) {
        errno = EINVAL;
        return -1;
    }
    if (is_registered_spot_handle (handle_)) {
        spot_handle_t *spot = static_cast<spot_handle_t *> (handle_);
        zlink::service_public_api_scope_t admission (spot->public_api);
        if (!admission.acquired ())
            return -1;
        zlink::spot_pub_t *pub = ensure_spot_pub (spot);
        if (!pub || !pub->poller_socket ()) {
            errno = EFAULT;
            return -1;
        }
        return pub->poller_socket ()->getsockopt (socket_option, optval_, optvallen_);
    }

    if (is_registered_spot_node_handle (handle_)) {
        zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (handle_);
        zlink::service_public_api_scope_t admission (node->public_api_guard ());
        if (!admission.acquired ())
            return -1;
        zlink::spot_pub_t *pub = node->ensure_default_pub ();
        if (!pub || !pub->poller_socket ()) {
            errno = EFAULT;
            return -1;
        }
        return pub->poller_socket ()->getsockopt (socket_option, optval_, optvallen_);
    }

    errno = EFAULT;
    return -1;
}

int zlink_spot_subject_set_sub_option_internal (void *handle_,
                                                zlink_sub_option_t option_,
                                                const void *optval_,
                                                size_t optvallen_)
{
    if (option_ != ZLINK_SUB_OPT_TOPICS_COUNT) {
        errno = EINVAL;
        return -1;
    }
    if (optval_ || optvallen_ != 0) {
        errno = EINVAL;
        return -1;
    }
    errno = EINVAL;
    return -1;
}

int zlink_spot_subject_get_sub_option_internal (void *handle_,
                                                zlink_sub_option_t option_,
                                                void *optval_,
                                                size_t *optvallen_)
{
    if (option_ == ZLINK_SUB_OPT_TOPICS_COUNT) {
        if (!optval_ || !optvallen_ || *optvallen_ < sizeof (int)) {
            if (optvallen_)
                *optvallen_ = sizeof (int);
            errno = EINVAL;
            return -1;
        }

        std::vector<zlink::spot_sub_t::subject_descriptor_t> subjects;
        if (is_registered_spot_handle (handle_)) {
            spot_handle_t *spot = static_cast<spot_handle_t *> (handle_);
            zlink::service_public_api_scope_t admission (spot->public_api);
            if (!admission.acquired ())
                return -1;
            zlink::spot_sub_t *sub = ensure_spot_sub (spot);
            if (!sub) {
                errno = ENOTSUP;
                return -1;
            }
            sub->append_all_subjects (&subjects);
        } else if (is_registered_spot_node_handle (handle_)) {
            zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (handle_);
            zlink::service_public_api_scope_t admission (node->public_api_guard ());
            if (!admission.acquired ())
                return -1;
            node->snapshot_subscription_subjects (&subjects);
        } else {
            errno = EFAULT;
            return -1;
        }

        *static_cast<int *> (optval_) = static_cast<int> (subjects.size ());
        *optvallen_ = sizeof (int);
        return 0;
    }

    const int socket_option = map_sub_to_socket_option (option_);
    if (socket_option < 0) {
        errno = EINVAL;
        return -1;
    }

    if (is_registered_spot_handle (handle_)) {
        spot_handle_t *spot = static_cast<spot_handle_t *> (handle_);
        zlink::service_public_api_scope_t admission (spot->public_api);
        if (!admission.acquired ())
            return -1;
        zlink::spot_sub_t *sub = ensure_spot_sub (spot);
        if (!sub || !sub->poller_socket ()) {
            errno = EFAULT;
            return -1;
        }
        return sub->poller_socket ()->getsockopt (socket_option, optval_, optvallen_);
    }

    if (is_registered_spot_node_handle (handle_)) {
        zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (handle_);
        zlink::service_public_api_scope_t admission (node->public_api_guard ());
        if (!admission.acquired ())
            return -1;
        zlink::spot_sub_t *sub = node->ensure_default_sub ();
        if (!sub || !sub->poller_socket ()) {
            errno = EFAULT;
            return -1;
        }
        return sub->poller_socket ()->getsockopt (socket_option, optval_, optvallen_);
    }

    errno = EFAULT;
    return -1;
}

int zlink_spot_subject_set_routing_id_internal (void *handle_,
                                                const void *data_,
                                                size_t size_)
{
    if (is_registered_spot_handle (handle_)) {
        spot_handle_t *spot = static_cast<spot_handle_t *> (handle_);
        zlink::service_public_api_scope_t admission (spot->public_api);
        if (!admission.acquired ())
            return -1;
        zlink::spot_pub_t *pub = ensure_spot_pub (spot);
        return pub ? pub->set_routing_id (data_, size_) : -1;
    }
    if (is_registered_spot_node_handle (handle_)) {
        zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (handle_);
        zlink::service_public_api_scope_t admission (node->public_api_guard ());
        if (!admission.acquired ())
            return -1;
        zlink::spot_pub_t *pub = node->ensure_default_pub ();
        return pub ? pub->set_routing_id (data_, size_) : -1;
    }
    errno = EFAULT;
    return -1;
}

int zlink_spot_subject_get_routing_id_internal (void *handle_,
                                                zlink_routing_id_t *out_)
{
    if (is_registered_spot_handle (handle_)) {
        spot_handle_t *spot = static_cast<spot_handle_t *> (handle_);
        zlink::service_public_api_scope_t admission (spot->public_api);
        if (!admission.acquired ())
            return -1;
        zlink::spot_pub_t *pub = ensure_spot_pub (spot);
        return pub ? pub->routing_id (out_) : -1;
    }
    if (is_registered_spot_node_handle (handle_)) {
        zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (handle_);
        zlink::service_public_api_scope_t admission (node->public_api_guard ());
        if (!admission.acquired ())
            return -1;
        zlink::spot_pub_t *pub = node->ensure_default_pub ();
        return pub ? pub->routing_id (out_) : -1;
    }
    errno = EFAULT;
    return -1;
}

int zlink_spot_subject_set_tls_server_internal (void *handle_,
                                                const char *cert_,
                                                const char *key_,
                                                int require_client_cert_)
{
    LIBZLINK_UNUSED (require_client_cert_);
    if (is_registered_spot_handle (handle_)) {
        spot_handle_t *spot = static_cast<spot_handle_t *> (handle_);
        if (!spot->node) {
            errno = EFAULT;
            return -1;
        }
        return spot->node->set_tls_server (cert_, key_);
    }
    if (is_registered_spot_node_handle (handle_))
        return static_cast<zlink::spot_node_t *> (handle_)->set_tls_server (
          cert_, key_);
    errno = EFAULT;
    return -1;
}

int zlink_spot_subject_set_tls_client_internal (void *handle_,
                                                const char *ca_cert_,
                                                const char *hostname_,
                                                int trust_system_)
{
    if (is_registered_spot_handle (handle_)) {
        spot_handle_t *spot = static_cast<spot_handle_t *> (handle_);
        if (!spot->node) {
            errno = EFAULT;
            return -1;
        }
        return spot->node->set_tls_client (ca_cert_, hostname_, trust_system_);
    }
    if (is_registered_spot_node_handle (handle_))
        return static_cast<zlink::spot_node_t *> (handle_)->set_tls_client (
          ca_cert_, hostname_, trust_system_);
    errno = EFAULT;
    return -1;
}

int zlink_spot_subject_set_subscription_internal (void *handle_,
                                                  const char *filter_)
{
    std::string raw_filter;
    bool is_pattern = false;
    if (!is_valid_pubsub_filter (filter_, &raw_filter, &is_pattern)
        || raw_filter.empty ()) {
        errno = EINVAL;
        return -1;
    }

    if (is_registered_spot_handle (handle_))
        return is_pattern ? spot_subscribe_pattern_internal (handle_, filter_)
                          : spot_subscribe_internal (handle_, filter_);
    if (is_registered_spot_node_handle (handle_))
        return is_pattern ? spot_node_subscribe_pattern_internal (handle_,
                                                                  filter_)
                          : spot_node_subscribe_internal (handle_, filter_);

    errno = EFAULT;
    return -1;
}

int zlink_spot_subject_unset_subscription_internal (void *handle_,
                                                    const char *filter_)
{
    std::string raw_filter;
    bool is_pattern = false;
    if (!is_valid_pubsub_filter (filter_, &raw_filter, &is_pattern)
        || raw_filter.empty ()) {
        errno = EINVAL;
        return -1;
    }

    if (is_registered_spot_handle (handle_))
        return spot_unsubscribe_internal (handle_, filter_);
    if (is_registered_spot_node_handle (handle_))
        return spot_node_unsubscribe_internal (handle_, filter_);

    errno = EFAULT;
    return -1;
}

int zlink_spot_subject_subscription_at_internal (void *handle_,
                                                 size_t index_,
                                                 char *filter_out_,
                                                 size_t *filter_len_inout_,
                                                 int *is_pattern_out_)
{
    std::vector<zlink::spot_sub_t::subject_descriptor_t> subjects;

    if (is_registered_spot_handle (handle_)) {
        spot_handle_t *spot = static_cast<spot_handle_t *> (handle_);
        zlink::service_public_api_scope_t admission (spot->public_api);
        if (!admission.acquired ())
            return -1;
        zlink::spot_sub_t *sub = ensure_spot_sub (spot);
        if (!sub) {
            errno = ENOTSUP;
            return -1;
        }
        sub->append_all_subjects (&subjects);
    } else if (is_registered_spot_node_handle (handle_)) {
        zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (handle_);
        zlink::service_public_api_scope_t admission (node->public_api_guard ());
        if (!admission.acquired ())
            return -1;
        node->snapshot_subscription_subjects (&subjects);
    } else {
        errno = EFAULT;
        return -1;
    }

    std::sort (subjects.begin (), subjects.end (), spot_subscription_less_t ());
    if (index_ >= subjects.size ()) {
        errno = ENOENT;
        return -1;
    }
    return copy_subscription_subject (subjects[index_], filter_out_,
                                      filter_len_inout_, is_pattern_out_);
}

static void spot_node_sub_handler_adapter (
  const zlink_routing_id_t *source_rid_,
  const char *topic_,
  size_t topic_len_,
  zlink_msg_t *parts_,
  size_t part_count_,
  void *userdata_)
{
    zlink::spot_node_t *node =
      static_cast<zlink::spot_node_t *> (userdata_);
    if (!node || !node->check_tag ()) {
        close_spot_parts (parts_, part_count_);
        return;
    }

    zlink_subscribe_handler_fn handler = NULL;
    void *userdata = NULL;
    {
        spot_node_handler_registry_t &registry = spot_node_handler_registry ();
        zlink::scoped_lock_t lock (registry.sync);
        std::map<zlink::spot_node_t *, spot_node_handler_entry_t>::iterator it =
          registry.handlers.find (node);
        if (it != registry.handlers.end ()) {
            handler = it->second.handler;
            userdata = it->second.userdata;
        }
    }

    if (!handler) {
        close_spot_parts (parts_, part_count_);
        return;
    }
    g_current_spot_dispatch_handle = node;
    g_current_spot_dispatch_is_node = true;
    handler (source_rid_, topic_, topic_len_, parts_, part_count_, userdata);
    g_current_spot_dispatch_handle = NULL;
    g_current_spot_dispatch_is_node = false;
}

static void spot_sub_handler_adapter (const zlink_routing_id_t *source_rid_,
                                      const char *topic_,
                                      size_t topic_len_,
                                      zlink_msg_t *parts_,
                                      size_t part_count_,
                                      void *userdata_)
{
    spot_handle_t *spot = static_cast<spot_handle_t *> (userdata_);
    if (!spot || !spot->check_tag () || !spot->handler) {
        close_spot_parts (parts_, part_count_);
        return;
    }
    g_current_spot_dispatch_handle = spot;
    g_current_spot_dispatch_is_node = false;
    spot->handler (source_rid_, topic_, topic_len_, parts_, part_count_,
                   spot->handler_userdata);
    g_current_spot_dispatch_handle = NULL;
    g_current_spot_dispatch_is_node = false;
}

static void release_poller_registration (const poller_registration_t &registration_)
{
    switch (registration_.subject_kind) {
        case poller_subject_gateway:
            decrement_gateway_poller_ref (
              static_cast<zlink::gateway_t *> (registration_.subject),
              registration_.events);
            break;
        case poller_subject_spot_pub:
        case poller_subject_spot_sub:
            decrement_spot_poller_ref (
              static_cast<spot_handle_t *> (registration_.subject),
              registration_.events);
            break;
        case poller_subject_spot_node_pub:
        case poller_subject_spot_node_sub:
            decrement_spot_node_poller_ref (
              static_cast<zlink::spot_node_t *> (registration_.subject),
              registration_.events);
            break;
        default:
            break;
    }
}

static int poller_add_registration (poller_handle_t *poller_,
                                    zlink::socket_base_t *socket_,
                                    void *user_data_,
                                    short events_,
                                    void *subject_,
                                    poller_subject_kind_t subject_kind_)
{
    if (!poller_ || !socket_) {
        errno = EFAULT;
        return -1;
    }
    if (poller_->poller.add (socket_, user_data_, events_) != 0)
        return -1;

    poller_registration_t registration;
    registration.socket = static_cast<void *> (socket_);
    registration.subject = subject_;
    registration.subject_kind = subject_kind_;
    registration.events = events_;
    poller_->registrations.push_back (registration);
    return 0;
}

static int poller_find_registration_index (poller_handle_t *poller_,
                                           void *subject_)
{
    if (!poller_)
        return -1;
    for (size_t i = 0; i < poller_->registrations.size (); ++i) {
        if (poller_->registrations[i].subject == subject_)
            return static_cast<int> (i);
    }
    return -1;
}

static int poller_find_registration_index (poller_handle_t *poller_,
                                           void *subject_,
                                           poller_subject_kind_t subject_kind_)
{
    if (!poller_)
        return -1;
    for (size_t i = 0; i < poller_->registrations.size (); ++i) {
        if (poller_->registrations[i].subject == subject_
            && poller_->registrations[i].subject_kind == subject_kind_) {
            return static_cast<int> (i);
        }
    }
    return -1;
}

static int poller_remove_registration_at (poller_handle_t *poller_, int index_)
{
    if (!poller_ || index_ < 0
        || static_cast<size_t> (index_) >= poller_->registrations.size ()) {
        errno = EINVAL;
        return -1;
    }

    zlink::socket_base_t *socket = static_cast<zlink::socket_base_t *> (
      poller_->registrations[static_cast<size_t> (index_)].socket);
    const int rc = poller_->poller.remove (socket);
    if (rc == 0) {
        release_poller_registration (
          poller_->registrations[static_cast<size_t> (index_)]);
        poller_->registrations.erase (poller_->registrations.begin () + index_);
    }
    return rc;
}

static int poller_remove_all_registrations_for_subject (poller_handle_t *poller_,
                                                        void *subject_)
{
    if (!poller_) {
        errno = EFAULT;
        return -1;
    }

    bool removed = false;
    while (true) {
        const int index = poller_find_registration_index (poller_, subject_);
        if (index < 0)
            break;
        if (poller_remove_registration_at (poller_, index) != 0)
            return -1;
        removed = true;
    }

    if (!removed) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

static zlink::spot_pub_t *resolve_spot_pub_subject (void *spot_or_node_);
static zlink::spot_sub_t *resolve_spot_sub_subject (void *spot_or_node_);
static int increment_spot_subject_poller_ref (void *spot_or_node_,
                                              short events_);
static poller_subject_kind_t poller_spot_pub_kind_for_subject (void *spot_or_node_);
static poller_subject_kind_t poller_spot_sub_kind_for_subject (void *spot_or_node_);

static int validate_spot_generic_poller_events (short events_,
                                                bool *is_pub_out_)
{
    if (events_ == ZLINK_POLLOUT) {
        if (validate_spot_pub_poller_events (events_) != 0)
            return -1;
        *is_pub_out_ = true;
        return 0;
    }
    if (events_ == ZLINK_POLLIN) {
        if (validate_spot_sub_poller_events (events_) != 0)
            return -1;
        *is_pub_out_ = false;
        return 0;
    }

    errno = EINVAL;
    return -1;
}

static int poller_add_gateway_registration (poller_handle_t *poller_,
                                            void *gateway_,
                                            void *user_data_,
                                            short events_)
{
    if (!is_registered_gateway_handle (gateway_)) {
        errno = EFAULT;
        return -1;
    }
    zlink::gateway_t *gateway = static_cast<zlink::gateway_t *> (gateway_);
    if (validate_gateway_poller_events (events_) != 0)
        return -1;
    if (increment_gateway_poller_ref (gateway, events_) != 0)
        return -1;
    zlink::socket_base_t *socket = zlink::gateway_access_t::router_socket (gateway);
    if (!socket
        || poller_add_registration (poller_, socket, user_data_, events_,
                                    gateway_, poller_subject_gateway)
             != 0) {
        decrement_gateway_poller_ref (gateway, events_);
        if (!socket)
            errno = ENOTSUP;
        return -1;
    }
    return 0;
}

static int poller_modify_gateway_registration (poller_handle_t *poller_,
                                               void *gateway_,
                                               short events_)
{
    if (!is_registered_gateway_handle (gateway_)) {
        errno = EFAULT;
        return -1;
    }
    if (validate_gateway_poller_events (events_) != 0)
        return -1;
    const int index = poller_find_registration_index (poller_, gateway_);
    if (index < 0) {
        errno = EINVAL;
        return -1;
    }
    zlink::gateway_t *gateway = static_cast<zlink::gateway_t *> (gateway_);
    const short old_events = poller_->registrations[index].events;
    if (increment_gateway_poller_ref (gateway, events_) != 0)
        return -1;
    if (poller_->poller.modify (
          static_cast<zlink::socket_base_t *> (
            poller_->registrations[index].socket),
          events_)
        != 0) {
        decrement_gateway_poller_ref (gateway, events_);
        return -1;
    }
    poller_->registrations[index].events = events_;
    decrement_gateway_poller_ref (gateway, old_events);
    return 0;
}

int zlink_poll (zlink_pollitem_t *items_, int nitems_, long timeout_)
{
    if (nitems_ < 0 || (nitems_ > 0 && !items_)) {
        errno = EINVAL;
        return -1;
    }
    if (nitems_ == 0)
        return 0;

    zlink::socket_poller_t poller;
    for (int i = 0; i < nitems_; ++i) {
        items_[i].revents = 0;
        if (items_[i].socket) {
            socket_handle_t handle = as_socket_handle (items_[i].socket);
            if (!handle.socket)
                return -1;
            if (validate_socket_callback_poller_events (handle,
                                                        items_[i].events)
                != 0)
                return -1;
            if (poller.add (handle.socket, NULL, items_[i].events) != 0)
                return -1;
        } else if (poller.add_fd (items_[i].fd, NULL, items_[i].events) != 0) {
            return -1;
        }
    }

    std::vector<zlink::socket_poller_t::event_t> events (
      static_cast<size_t> (nitems_));
    const int rc = poller.wait (events.data (), nitems_, timeout_);
    if (rc <= 0)
        return rc;

    for (int i = 0; i < rc; ++i) {
        for (int j = 0; j < nitems_; ++j) {
            if ((items_[j].socket && items_[j].socket == events[i].socket)
                || (!items_[j].socket && items_[j].fd == events[i].fd)) {
                items_[j].revents = events[i].events;
                break;
            }
        }
    }
    return rc;
}

void *zlink_poller_new (void)
{
    poller_handle_t *poller = new (std::nothrow) poller_handle_t;
    if (!poller) {
        errno = ENOMEM;
        return NULL;
    }
    return static_cast<void *> (poller);
}

int zlink_poller_destroy (void **poller_p_)
{
    if (!poller_p_ || !*poller_p_) {
        errno = EFAULT;
        return -1;
    }
    poller_handle_t *poller = as_poller_handle (*poller_p_);
    if (!poller)
        return -1;
    for (size_t i = 0; i < poller->registrations.size (); ++i)
        release_poller_registration (poller->registrations[i]);
    poller->tag = 0xdeadbeef;
    delete poller;
    *poller_p_ = NULL;
    return 0;
}

int zlink_poller_size (void *poller_)
{
    poller_handle_t *poller = as_poller_handle (poller_);
    if (!poller)
        return -1;
    return poller->poller.size ();
}

int zlink_poller_add (void *poller_,
                      void *socket_,
                      void *user_data_,
                      short events_)
{
    poller_handle_t *poller = as_poller_handle (poller_);
    if (!poller)
        return -1;
    if (is_registered_gateway_handle (socket_))
        return poller_add_gateway_registration (poller, socket_, user_data_,
                                                events_);
    if (zlink::spot_pub_t *pub = as_spot_pub_side_handle (socket_)) {
        bool is_pub = false;
        if (validate_spot_generic_poller_events (events_, &is_pub) != 0)
            return -1;
        if (!is_pub) {
            errno = EINVAL;
            return -1;
        }
        return poller_add_registration (poller, pub->poller_socket (),
                                        user_data_, events_, socket_,
                                        poller_subject_none);
    }
    if (zlink::spot_sub_t *sub = as_spot_sub_side_handle (socket_)) {
        bool is_pub = false;
        if (validate_spot_generic_poller_events (events_, &is_pub) != 0)
            return -1;
        if (is_pub) {
            errno = EINVAL;
            return -1;
        }
        return poller_add_registration (poller, sub->poller_socket (),
                                        user_data_, events_, socket_,
                                        poller_subject_none);
    }
    if (is_registered_spot_handle (socket_)
        || is_registered_spot_node_handle (socket_)) {
        bool is_pub = false;
        if (validate_spot_generic_poller_events (events_, &is_pub) != 0)
            return -1;
        if (increment_spot_subject_poller_ref (socket_, events_) != 0)
            return -1;

        if (is_pub) {
            zlink::spot_pub_t *pub = resolve_spot_pub_subject (socket_);
            if (!pub
                || poller_add_registration (
                     poller, pub->poller_socket (), user_data_, events_, socket_,
                     poller_spot_pub_kind_for_subject (socket_))
                     != 0) {
                poller_registration_t registration;
                registration.subject = socket_;
                registration.subject_kind =
                  poller_spot_pub_kind_for_subject (socket_);
                registration.events = events_;
                release_poller_registration (registration);
                return -1;
            }
            return 0;
        }

        zlink::spot_sub_t *sub = resolve_spot_sub_subject (socket_);
        if (!sub
            || poller_add_registration (
                 poller, sub->poller_socket (), user_data_, events_, socket_,
                 poller_spot_sub_kind_for_subject (socket_))
                 != 0) {
            poller_registration_t registration;
            registration.subject = socket_;
            registration.subject_kind =
              poller_spot_sub_kind_for_subject (socket_);
            registration.events = events_;
            release_poller_registration (registration);
            return -1;
        }
        return 0;
    }
    socket_handle_t handle = as_socket_handle (socket_);
    if (!handle.socket)
        return -1;
    if (validate_socket_callback_poller_events (handle, events_) != 0)
        return -1;
    return poller_add_registration (poller, handle.socket, user_data_, events_,
                                    socket_, poller_subject_none);
}

int zlink_poller_modify (void *poller_, void *socket_, short events_)
{
    poller_handle_t *poller = as_poller_handle (poller_);
    if (!poller)
        return -1;
    if (is_registered_gateway_handle (socket_))
        return poller_modify_gateway_registration (poller, socket_, events_);
    if (as_spot_pub_side_handle (socket_) || as_spot_sub_side_handle (socket_)) {
        bool is_pub = false;
        if (validate_spot_generic_poller_events (events_, &is_pub) != 0)
            return -1;
        const int index = poller_find_registration_index (poller, socket_);
        if (index < 0) {
            errno = EINVAL;
            return -1;
        }
        return poller->poller.modify (
          static_cast<zlink::socket_base_t *> (poller->registrations[index].socket),
          events_);
    }
    if (is_registered_spot_handle (socket_)
        || is_registered_spot_node_handle (socket_)) {
        bool is_pub = false;
        if (validate_spot_generic_poller_events (events_, &is_pub) != 0)
            return -1;
        const int index = poller_find_registration_index (
          poller, socket_, is_pub ? poller_spot_pub_kind_for_subject (socket_)
                                  : poller_spot_sub_kind_for_subject (socket_));
        if (index < 0) {
            errno = EINVAL;
            return -1;
        }
        if (increment_spot_subject_poller_ref (socket_, events_) != 0)
            return -1;
        const short old_events = poller->registrations[index].events;
        zlink::socket_base_t *socket =
          static_cast<zlink::socket_base_t *> (poller->registrations[index].socket);
        if (poller->poller.modify (socket, events_) != 0) {
            if (is_registered_spot_handle (socket_))
                decrement_spot_poller_ref (
                  static_cast<spot_handle_t *> (socket_), events_);
            else
                decrement_spot_node_poller_ref (
                  static_cast<zlink::spot_node_t *> (socket_), events_);
            return -1;
        }
        if (is_registered_spot_handle (socket_))
            decrement_spot_poller_ref (
              static_cast<spot_handle_t *> (socket_), old_events);
        else
            decrement_spot_node_poller_ref (
              static_cast<zlink::spot_node_t *> (socket_), old_events);
        poller->registrations[index].events = events_;
        return 0;
    }
    socket_handle_t handle = as_socket_handle (socket_);
    if (!handle.socket)
        return -1;
    if (validate_socket_callback_poller_events (handle, events_) != 0)
        return -1;
    const int index = poller_find_registration_index (poller, socket_);
    if (index < 0) {
        errno = EINVAL;
        return -1;
    }
    return poller->poller.modify (
      static_cast<zlink::socket_base_t *> (poller->registrations[index].socket),
      events_);
}

int zlink_poller_remove (void *poller_, void *socket_)
{
    poller_handle_t *poller = as_poller_handle (poller_);
    if (!poller)
        return -1;
    if (as_spot_pub_side_handle (socket_) || as_spot_sub_side_handle (socket_))
        return poller_remove_all_registrations_for_subject (poller, socket_);
    if (is_registered_gateway_handle (socket_)
        || is_registered_spot_handle (socket_)
        || is_registered_spot_node_handle (socket_))
        return poller_remove_all_registrations_for_subject (poller, socket_);

    socket_handle_t handle = as_socket_handle (socket_);
    if (!handle.socket)
        return -1;
    return poller_remove_all_registrations_for_subject (poller, socket_);
}

int zlink_poller_add_fd (void *poller_,
                         zlink_fd_t fd_,
                         void *user_data_,
                         short events_)
{
    poller_handle_t *poller = as_poller_handle (poller_);
    if (!poller)
        return -1;
    return poller->poller.add_fd (fd_, user_data_, events_);
}

int zlink_poller_modify_fd (void *poller_, zlink_fd_t fd_, short events_)
{
    poller_handle_t *poller = as_poller_handle (poller_);
    if (!poller)
        return -1;
    return poller->poller.modify_fd (fd_, events_);
}

int zlink_poller_remove_fd (void *poller_, zlink_fd_t fd_)
{
    poller_handle_t *poller = as_poller_handle (poller_);
    if (!poller)
        return -1;
    return poller->poller.remove_fd (fd_);
}

int zlink_poller_wait (void *poller_,
                       zlink_poller_event_t *event_,
                       long timeout_)
{
    poller_handle_t *poller = as_poller_handle (poller_);
    if (!poller || !event_) {
        if (poller && !event_)
            errno = EINVAL;
        return -1;
    }
    zlink::socket_poller_t::event_t native_event;
    const int rc = poller->poller.wait (&native_event, 1, timeout_);
    if (rc <= 0)
        return rc;
    event_->socket = native_event.socket;
    event_->fd = native_event.fd;
    event_->user_data = native_event.user_data;
    event_->events = native_event.events;
    return rc;
}

int zlink_poller_wait_all (void *poller_,
                           zlink_poller_event_t *events_,
                           int n_events_,
                           long timeout_)
{
    poller_handle_t *poller = as_poller_handle (poller_);
    if (!poller)
        return -1;
    if (n_events_ < 0 || (n_events_ > 0 && !events_)) {
        errno = EINVAL;
        return -1;
    }
    return poller->poller.wait (
      reinterpret_cast<zlink::socket_poller_t::event_t *> (events_), n_events_,
      timeout_);
}

static zlink::spot_pub_t *resolve_spot_pub_subject (void *spot_or_node_)
{
    if (is_registered_spot_handle (spot_or_node_)) {
        spot_handle_t *spot = static_cast<spot_handle_t *> (spot_or_node_);
        zlink::service_public_api_scope_t admission (spot->public_api);
        if (!admission.acquired ())
            return NULL;
        return ensure_spot_pub (spot);
    }
    if (is_registered_spot_node_handle (spot_or_node_)) {
        zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (spot_or_node_);
        zlink::service_public_api_scope_t admission (node->public_api_guard ());
        if (!admission.acquired ())
            return NULL;
        return node->ensure_default_pub ();
    }
    errno = EFAULT;
    return NULL;
}

static zlink::spot_sub_t *resolve_spot_sub_subject (void *spot_or_node_)
{
    if (is_registered_spot_handle (spot_or_node_)) {
        spot_handle_t *spot = static_cast<spot_handle_t *> (spot_or_node_);
        zlink::service_public_api_scope_t admission (spot->public_api);
        if (!admission.acquired ())
            return NULL;
        return ensure_spot_sub (spot);
    }
    if (is_registered_spot_node_handle (spot_or_node_)) {
        zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (spot_or_node_);
        zlink::service_public_api_scope_t admission (node->public_api_guard ());
        if (!admission.acquired ())
            return NULL;
        zlink::spot_internal_receiver_t *receiver =
          zlink::spot_node_access_t::ensure_internal_receiver (node);
        if (receiver && receiver->impl ())
            return receiver->impl ();
        return node->ensure_default_sub ();
    }
    errno = EFAULT;
    return NULL;
}

static int increment_spot_subject_poller_ref (void *spot_or_node_,
                                              short events_)
{
    if (is_registered_spot_handle (spot_or_node_))
        return increment_spot_poller_ref (
          static_cast<spot_handle_t *> (spot_or_node_), events_);
    if (is_registered_spot_node_handle (spot_or_node_))
        return increment_spot_node_poller_ref (
          static_cast<zlink::spot_node_t *> (spot_or_node_), events_);
    errno = EFAULT;
    return -1;
}

static poller_subject_kind_t poller_spot_pub_kind_for_subject (void *spot_or_node_)
{
    if (is_registered_spot_handle (spot_or_node_))
        return poller_subject_spot_pub;
    if (is_registered_spot_node_handle (spot_or_node_))
        return poller_subject_spot_node_pub;
    return poller_subject_none;
}

static poller_subject_kind_t poller_spot_sub_kind_for_subject (void *spot_or_node_)
{
    if (is_registered_spot_handle (spot_or_node_))
        return poller_subject_spot_sub;
    if (is_registered_spot_node_handle (spot_or_node_))
        return poller_subject_spot_node_sub;
    return poller_subject_none;
}

int zlink_bind (void *s_, const char *addr_)
{
    socket_handle_t handle = as_socket_handle (s_);
    if (!handle.socket)
        return -1;
    return handle.socket->bind (addr_);
}

int zlink_connect (void *s_, const char *addr_)
{
    socket_handle_t handle = as_socket_handle (s_);
    if (!handle.socket)
        return -1;
    return handle.socket->connect (addr_);
}

int zlink_unbind (void *s_, const char *addr_)
{
    socket_handle_t handle = as_socket_handle (s_);
    if (!handle.socket)
        return -1;
    return handle.socket->term_endpoint (addr_);
}

int zlink_disconnect (void *s_, const char *addr_)
{
    socket_handle_t handle = as_socket_handle (s_);
    if (!handle.socket)
        return -1;
    return handle.socket->term_endpoint (addr_);
}

// Sending functions.

static inline int s_sendmsg (socket_handle_t handle_,
                             zlink_msg_t *msg_,
                             zlink_send_flags_t flags_)
{
    size_t sz = zlink_msg_size (msg_);
    int rc = handle_.socket->send (reinterpret_cast<zlink::msg_t *> (msg_),
                                   flags_);
    if (unlikely (rc < 0))
        return -1;

    size_t max_msgsz = INT_MAX;
    return static_cast<int> (sz < max_msgsz ? sz : max_msgsz);
}

static int validate_send_flags (int flags_)
{
    if (flags_ != 0 && flags_ != ZLINK_DONTWAIT) {
        errno = ENOTSUP;
        return -1;
    }
    return 0;
}

static int send_stream_message (socket_handle_t handle_,
                                const zlink_routing_id_t *rid_,
                                zlink_msg_t *msg_,
                                zlink_send_flags_t flags_);

static int send_socket_parts (socket_handle_t handle_,
                              const zlink_routing_id_t *target_rid_,
                              zlink_msg_t *parts_,
                              size_t part_count_,
                              zlink_send_flags_t flags_)
{
    if (!handle_.socket) {
        errno = EFAULT;
        return -1;
    }
    if (validate_send_flags (flags_) != 0)
        return -1;
    if ((!parts_ && part_count_ > 0) || part_count_ == 0) {
        errno = EFAULT;
        return -1;
    }

    const int type = socket_type (handle_);
    const int base_flags = flags_ & ZLINK_DONTWAIT;

    if (target_rid_) {
        if (type == ZLINK_CORE_SOCKET_STREAM) {
            if (part_count_ != 1) {
                errno = ENOTSUP;
                return -1;
            }
            const int rc =
              send_stream_message (handle_, target_rid_, &parts_[0], flags_);
            if (rc < 0)
                return -1;
            errno = 0;
            return 0;
        }

        if (type != ZLINK_CORE_SOCKET_ROUTER) {
            errno = ENOTSUP;
            return -1;
        }

        zlink_msg_t rid_msg;
        if (zlink_msg_init_size (&rid_msg, target_rid_->size) != 0)
            return -1;
        if (target_rid_->size > 0) {
            memcpy (zlink_msg_data (&rid_msg), target_rid_->data,
                    target_rid_->size);
        }
        if (s_sendmsg (handle_, &rid_msg, base_flags | ZLINK_SNDMORE) < 0) {
            const int err = errno;
            zlink_msg_close (&rid_msg);
            errno = err;
            return -1;
        }
    }

    if (type == ZLINK_CORE_SOCKET_PUB || type == ZLINK_CORE_SOCKET_SUB || type == ZLINK_CORE_SOCKET_XSUB
        || type == ZLINK_CORE_SOCKET_XPUB) {
        errno = ENOTSUP;
        return -1;
    }

    for (size_t i = 0; i < part_count_; ++i) {
        const bool more = i + 1 < part_count_;
        if (s_sendmsg (handle_, &parts_[i], base_flags | (more ? ZLINK_SNDMORE : 0))
            < 0) {
            return -1;
        }
    }

    errno = 0;
    return 0;
}

static int publish_socket_parts (socket_handle_t handle_,
                                 const char *topic_id_,
                                 zlink_msg_t *parts_,
                                 size_t part_count_,
                                 zlink_send_flags_t flags_)
{
    if (!handle_.socket) {
        errno = EFAULT;
        return -1;
    }
    if (validate_send_flags (flags_) != 0)
        return -1;
    if ((!parts_ && part_count_ > 0) || part_count_ == 0) {
        errno = EFAULT;
        return -1;
    }

    const int type = socket_type (handle_);
    if (type != ZLINK_CORE_SOCKET_PUB && type != ZLINK_CORE_SOCKET_XPUB) {
        errno = ENOTSUP;
        return -1;
    }
    const int base_flags = flags_ & ZLINK_DONTWAIT;
    if (topic_id_ != NULL) {
        const size_t topic_len = strlen (topic_id_);
        zlink_msg_t topic_msg;
        if (zlink_msg_init_size (&topic_msg, topic_len) != 0)
            return -1;
        if (topic_len > 0)
            memcpy (zlink_msg_data (&topic_msg), topic_id_, topic_len);
        if (s_sendmsg (
              handle_, &topic_msg,
              base_flags | (part_count_ > 0 ? ZLINK_SNDMORE : 0))
            < 0) {
            const int err = errno;
            zlink_msg_close (&topic_msg);
            errno = err;
            return -1;
        }
    }

    for (size_t i = 0; i < part_count_; ++i) {
        const bool more = i + 1 < part_count_;
        if (s_sendmsg (handle_, &parts_[i], base_flags | (more ? ZLINK_SNDMORE : 0))
            < 0) {
            return -1;
        }
    }

    errno = 0;
    return 0;
}

static int recv_socket_subscribe_parts (socket_handle_t handle_,
                                        zlink_routing_id_t *source_rid_out_,
                                        zlink_msg_t **parts_out_,
                                        size_t *part_count_out_,
                                        char *topic_id_out_,
                                        size_t *topic_id_len_out_,
                                        zlink_send_flags_t flags_)
{
    if (!handle_.socket) {
        errno = EFAULT;
        return -1;
    }
    if (!parts_out_ || !part_count_out_ || !topic_id_len_out_) {
        errno = EFAULT;
        return -1;
    }
    if (validate_recv_flags (flags_) != 0)
        return -1;

    *parts_out_ = NULL;
    *part_count_out_ = 0;
    if (source_rid_out_)
        memset (source_rid_out_, 0, sizeof (*source_rid_out_));

    const int type = socket_type (handle_);
    if (type != ZLINK_CORE_SOCKET_SUB && type != ZLINK_CORE_SOCKET_XSUB) {
        errno = ENOTSUP;
        return -1;
    }

    std::vector<zlink_msg_t> frames;
    zlink_msg_t first;
    zlink_msg_init (&first);
    if (zlink::recv_msg_internal (handle_.socket, &first, flags_) < 0) {
        zlink_msg_close (&first);
        return -1;
    }
    frames.push_back (first);

    while (frame_has_more (frames.back ())) {
        zlink_msg_t frame;
        zlink_msg_init (&frame);
        if (zlink::recv_msg_internal (handle_.socket, &frame, 0) < 0) {
            zlink_msg_close (&frame);
            close_spot_parts (frames.data (), frames.size ());
            return -1;
        }
        frames.push_back (frame);
    }

    if (copy_topic_to_output (
          static_cast<const char *> (zlink_msg_data (&frames[0])),
          zlink_msg_size (&frames[0]), topic_id_out_, topic_id_len_out_)
        != 0) {
        close_spot_parts (frames.data (), frames.size ());
        return -1;
    }

    const size_t payload_count = frames.size () - 1;
    if (payload_count == 0) {
        close_spot_parts (frames.data (), frames.size ());
        errno = 0;
        return 0;
    }

    zlink_msg_t *parts = static_cast<zlink_msg_t *> (
      malloc (payload_count * sizeof (zlink_msg_t)));
    if (!parts) {
        close_spot_parts (frames.data (), frames.size ());
        errno = ENOMEM;
        return -1;
    }
    memset (parts, 0, payload_count * sizeof (zlink_msg_t));

    for (size_t i = 0; i < payload_count; ++i) {
        zlink::msg_t *dst = reinterpret_cast<zlink::msg_t *> (&parts[i]);
        if (dst->init () != 0
            || dst->move (*reinterpret_cast<zlink::msg_t *> (&frames[i + 1]))
                 != 0) {
            for (size_t j = 0; j <= i; ++j)
                zlink_msg_close (&parts[j]);
            free (parts);
            close_spot_parts (frames.data (), frames.size ());
            errno = EFAULT;
            return -1;
        }
    }

    zlink_msg_close (&frames[0]);
    *parts_out_ = parts;
    *part_count_out_ = payload_count;
    errno = 0;
    return 0;
}

static int subscribe_socket_filter (socket_handle_t handle_,
                                    int option_,
                                    const char *filter_)
{
    if (!handle_.socket) {
        errno = EFAULT;
        return -1;
    }

    const int type = socket_type (handle_);
    if (type != ZLINK_CORE_SOCKET_SUB && type != ZLINK_CORE_SOCKET_XSUB) {
        errno = ENOTSUP;
        return -1;
    }

    std::string raw_filter;
    bool is_pattern = false;
    if (!is_valid_pubsub_filter (filter_, &raw_filter, &is_pattern)
        || raw_filter.empty ()) {
        errno = EINVAL;
        return -1;
    }

    return handle_.socket->setsockopt (option_, raw_filter.data (),
                                       raw_filter.size ());
}

static int recv_socket_parts (socket_handle_t handle_,
                              zlink_routing_id_t *source_rid_out_,
                              zlink_msg_t **parts_out_,
                              size_t *part_count_out_,
                              zlink_send_flags_t flags_)
{
    if (!handle_.socket) {
        errno = EFAULT;
        return -1;
    }
    if (!parts_out_ || !part_count_out_) {
        errno = EFAULT;
        return -1;
    }
    if (validate_recv_flags (flags_) != 0)
        return -1;

    *parts_out_ = NULL;
    *part_count_out_ = 0;
    if (source_rid_out_)
        memset (source_rid_out_, 0, sizeof (*source_rid_out_));

    const int type = socket_type (handle_);
    if (type == ZLINK_CORE_SOCKET_PUB) {
        errno = ENOTSUP;
        return -1;
    }
    if (type == ZLINK_CORE_SOCKET_XPUB) {
        errno = ENOTSUP;
        return -1;
    }

    std::vector<zlink_msg_t> frames;
    zlink_msg_t first;
    zlink_msg_init (&first);
    if (zlink::recv_msg_internal (handle_.socket, &first, flags_) < 0) {
        zlink_msg_close (&first);
        return -1;
    }
    frames.push_back (first);

    while (frame_has_more (frames.back ())) {
        zlink_msg_t frame;
        zlink_msg_init (&frame);
        if (zlink::recv_msg_internal (handle_.socket, &frame, 0) < 0) {
            zlink_msg_close (&frame);
            close_spot_parts (frames.data (), frames.size ());
            return -1;
        }
        frames.push_back (frame);
    }

    size_t payload_offset = 0;
    size_t payload_count = frames.size ();
    if (type == ZLINK_CORE_SOCKET_ROUTER && source_rid_out_) {
        const size_t routing_id_size = zlink_msg_size (&frames[0]);
        const size_t routing_id_copy =
          routing_id_size > sizeof (source_rid_out_->data)
            ? sizeof (source_rid_out_->data)
            : routing_id_size;
        source_rid_out_->size = static_cast<uint8_t> (routing_id_copy);
        if (routing_id_copy > 0) {
            memcpy (source_rid_out_->data, zlink_msg_data (&frames[0]),
                    routing_id_copy);
        }
    } else if (type == ZLINK_CORE_SOCKET_STREAM && source_rid_out_) {
        handle_.socket->copy_last_recv_source_rid (source_rid_out_);
    }

    const bool strip_recv_routing_id =
      (type == ZLINK_CORE_SOCKET_STREAM)
      || (type == ZLINK_CORE_SOCKET_ROUTER && source_rid_out_ != NULL);
    if (strip_recv_routing_id) {
        if (frames.empty ()) {
            errno = EFAULT;
            return -1;
        }

        if (type == ZLINK_CORE_SOCKET_STREAM && source_rid_out_) {
            const size_t routing_id_size = zlink_msg_size (&frames[0]);
            const size_t routing_id_copy =
              routing_id_size > sizeof (source_rid_out_->data)
                ? sizeof (source_rid_out_->data)
                : routing_id_size;
            source_rid_out_->size = static_cast<uint8_t> (routing_id_copy);
            if (routing_id_copy > 0) {
                memcpy (source_rid_out_->data, zlink_msg_data (&frames[0]),
                        routing_id_copy);
            }
        }

        payload_offset = 1;
        payload_count = frames.size () > payload_offset
                          ? frames.size () - payload_offset
                          : 0;
        zlink_msg_close (&frames[0]);
        if (payload_count == 0) {
            errno = 0;
            return 0;
        }
    }

    zlink_msg_t *parts = static_cast<zlink_msg_t *> (
      malloc (payload_count * sizeof (zlink_msg_t)));
    if (!parts) {
        close_spot_parts (frames.data (), frames.size ());
        errno = ENOMEM;
        return -1;
    }
    memset (parts, 0, payload_count * sizeof (zlink_msg_t));

    for (size_t i = 0; i < payload_count; ++i) {
        zlink::msg_t *dst = reinterpret_cast<zlink::msg_t *> (&parts[i]);
        if (dst->init () != 0
            || dst->move (*reinterpret_cast<zlink::msg_t *> (
                 &frames[i + payload_offset]))
                 != 0) {
            for (size_t j = 0; j <= i; ++j)
                zlink_msg_close (&parts[j]);
            free (parts);
            close_spot_parts (frames.data (), frames.size ());
            errno = EFAULT;
            return -1;
        }
    }

    *parts_out_ = parts;
    *part_count_out_ = payload_count;
    errno = 0;
    return 0;
}

int zlink_send (void *s_,
                zlink_msg_t *parts_,
                size_t part_count_,
                zlink_send_flags_t flags_)
{
    if (!s_) {
        errno = EFAULT;
        return -1;
    }
    if (is_registered_gateway_handle (s_))
        return gateway_send_parts (s_, parts_, part_count_, flags_);

    socket_handle_t handle = as_socket_handle (s_);
    if (!handle.socket)
        return -1;

    return send_socket_parts (handle, NULL, parts_, part_count_, flags_);
}

int zlink_publish (void *subject_,
                   const char *topic_id_,
                   zlink_msg_t *parts_,
                   size_t part_count_,
                   zlink_send_flags_t flags_)
{
    if (!subject_) {
        errno = EFAULT;
        return -1;
    }

    if (is_registered_spot_handle (subject_)) {
        if (!topic_id_) {
            errno = EINVAL;
            return -1;
        }
        return spot_publish_internal (subject_, topic_id_, parts_, part_count_,
                                      flags_);
    }

    if (as_spot_pub_side_handle (subject_)) {
        if (!topic_id_) {
            errno = EINVAL;
            return -1;
        }
        return spot_pub_publish_internal (subject_, topic_id_, parts_,
                                          part_count_, flags_);
    }

    if (is_registered_spot_node_handle (subject_)) {
        if (!topic_id_) {
            errno = EINVAL;
            return -1;
        }
        return spot_node_publish_internal (subject_, topic_id_, parts_,
                                           part_count_, flags_);
    }

    socket_handle_t handle = as_socket_handle (subject_);
    if (!handle.socket)
        return -1;

    return publish_socket_parts (handle, topic_id_, parts_, part_count_, flags_);
}

int zlink_send_rid (void *s_,
                    const zlink_routing_id_t *target_rid_,
                    zlink_msg_t *parts_,
                    size_t part_count_,
                    zlink_send_flags_t flags_)
{
    if (!s_) {
        errno = EFAULT;
        return -1;
    }
    if (is_registered_gateway_handle (s_))
        return gateway_send_parts_rid (s_, target_rid_, parts_, part_count_,
                                       flags_);

    socket_handle_t handle = as_socket_handle (s_);
    if (!handle.socket)
        return -1;

    return send_socket_parts (handle, target_rid_, parts_, part_count_, flags_);
}

// Receiving functions.

int zlink_recv (void *s_,
                zlink_routing_id_t *source_rid_out_,
                zlink_msg_t **parts_out_,
                size_t *part_count_out_,
                zlink_send_flags_t flags_)
{
    if (!s_) {
        errno = EFAULT;
        return -1;
    }
    if (is_registered_gateway_handle (s_))
        return gateway_recv_parts (s_, source_rid_out_, parts_out_,
                                   part_count_out_, flags_);

    if (is_registered_spot_handle (s_) || is_registered_spot_node_handle (s_)
        || as_spot_pub_side_handle (s_) || as_spot_sub_side_handle (s_)) {
        errno = ENOTSUP;
        return -1;
    }

    socket_handle_t handle = as_socket_handle (s_);
    if (!handle.socket)
        return -1;

    return recv_socket_parts (handle, source_rid_out_, parts_out_,
                              part_count_out_, flags_);
}

int zlink_subscribe (void *subject_,
                     zlink_routing_id_t *source_rid_out_,
                     zlink_msg_t **parts_out_,
                     size_t *part_count_out_,
                     char *topic_id_out_,
                     size_t *topic_id_len_out_,
                     zlink_send_flags_t flags_)
{
    if (!subject_) {
        errno = EFAULT;
        return -1;
    }

    if (as_spot_sub_side_handle (subject_))
        return spot_sub_recv_internal (subject_, source_rid_out_, parts_out_,
                                       part_count_out_, topic_id_out_,
                                       topic_id_len_out_, flags_);

    if (is_registered_spot_handle (subject_))
        return spot_sub_recv_internal (subject_, source_rid_out_, parts_out_,
                                       part_count_out_, topic_id_out_,
                                       topic_id_len_out_, flags_);

    if (is_registered_spot_node_handle (subject_))
        return spot_node_recv_internal (subject_, source_rid_out_, parts_out_,
                                        part_count_out_, topic_id_out_,
                                        topic_id_len_out_, flags_);

    socket_handle_t handle = as_socket_handle (subject_);
    if (!handle.socket)
        return -1;

    return recv_socket_subscribe_parts (handle, source_rid_out_, parts_out_,
                                        part_count_out_, topic_id_out_,
                                        topic_id_len_out_, flags_);
}

int zlink_subscription_event (void *subject_,
                              zlink_routing_id_t *source_rid_out_,
                              int *subscribed_out_,
                              char *topic_id_out_,
                              size_t *topic_id_len_out_,
                              zlink_send_flags_t flags_)
{
    return zlink_xpub_recv (subject_, source_rid_out_, subscribed_out_,
                            topic_id_out_, topic_id_len_out_, flags_);
}

int zlink_stream_attach_raw (void *s_, zlink_stream_on_raw_fn on_raw_)
{
    socket_handle_t handle = as_socket_handle (s_);
    if (!handle.socket)
        return -1;
    if (!on_raw_) {
        errno = EINVAL;
        return -1;
    }
    if (!is_stream_type (handle)) {
        errno = EINVAL;
        return -1;
    }
    if (handle.socket->stream_dispatch_in_callback ()) {
        errno = EBUSY;
        return -1;
    }

    stream_api_lock_t api_lock (handle);
    return handle.socket->stream_dispatch_start_raw (on_raw_);
}

int zlink_stream_detach (void *s_)
{
    socket_handle_t handle = as_socket_handle (s_);
    if (!handle.socket)
        return -1;
    if (!is_stream_type (handle)) {
        errno = EINVAL;
        return -1;
    }
    if (handle.socket->stream_dispatch_in_callback ()) {
        errno = EBUSY;
        return -1;
    }

    stream_api_lock_t api_lock (handle);
    return handle.socket->stream_dispatch_stop ();
}

static int send_stream_message (socket_handle_t handle_,
                                const zlink_routing_id_t *rid_,
                                zlink_msg_t *msg_,
                                zlink_send_flags_t flags_)
{
    if (!handle_.socket)
        return -1;

    if (!msg_) {
        errno = EINVAL;
        return -1;
    }

    zlink::msg_t *core_msg = reinterpret_cast<zlink::msg_t *> (msg_);
    if (!core_msg->check ()) {
        errno = EFAULT;
        return -1;
    }

    if (!is_stream_type (handle_)) {
        errno = EINVAL;
        release_stream_send_msg (core_msg);
        return -1;
    }

    uint32_t routing_id = 0;
    if (!parse_stream_routing_id (rid_, &routing_id)) {
        const int err = errno;
        release_stream_send_msg (core_msg);
        errno = err;
        return -1;
    }

    const size_t payload_size = core_msg->size ();
    stream_api_lock_t api_lock (handle_);
    if (core_msg->set_routing_id (routing_id) != 0) {
        const int err = errno;
        release_stream_send_msg (core_msg);
        errno = err;
        return -1;
    }

    const int base_flags = flags_ & ZLINK_DONTWAIT;
    const int send_rc = s_sendmsg (handle_, msg_, base_flags);
    if (send_rc < 0) {
        const int err = errno;
        release_stream_send_msg (core_msg);
        errno = err;
        return -1;
    }

    errno = 0;
    return stream_payload_result (payload_size);
}

// Message manipulators.

int zlink_msg_init (zlink_msg_t *msg_)
{
    return (reinterpret_cast<zlink::msg_t *> (msg_))->init ();
}

int zlink_msg_init_size (zlink_msg_t *msg_, size_t size_)
{
    return (reinterpret_cast<zlink::msg_t *> (msg_))->init_size (size_);
}

int zlink_msg_init_buffer (zlink_msg_t *msg_, const void *buf_, size_t size_)
{
    return (reinterpret_cast<zlink::msg_t *> (msg_))->init_buffer (buf_, size_);
}

int zlink_msg_init_data (
  zlink_msg_t *msg_, void *data_, size_t size_, zlink_free_fn *ffn_, void *hint_)
{
    return (reinterpret_cast<zlink::msg_t *> (msg_))
      ->init_data (data_, size_, ffn_, hint_);
}

int zlink_msg_close (zlink_msg_t *msg_)
{
    return (reinterpret_cast<zlink::msg_t *> (msg_))->close ();
}

int zlink_msg_move (zlink_msg_t *dest_, zlink_msg_t *src_)
{
    return (reinterpret_cast<zlink::msg_t *> (dest_))
      ->move (*reinterpret_cast<zlink::msg_t *> (src_));
}

int zlink_msg_copy (zlink_msg_t *dest_, zlink_msg_t *src_)
{
    return (reinterpret_cast<zlink::msg_t *> (dest_))
      ->copy (*reinterpret_cast<zlink::msg_t *> (src_));
}

void *zlink_msg_data (zlink_msg_t *msg_)
{
    return (reinterpret_cast<zlink::msg_t *> (msg_))->data ();
}

size_t zlink_msg_size (const zlink_msg_t *msg_)
{
    return ((zlink::msg_t *) msg_)->size ();
}

int zlink_msg_refcnt (const zlink_msg_t *msg_)
{
    const zlink::msg_t *msg = reinterpret_cast<const zlink::msg_t *> (msg_);
    return static_cast<int> (msg->refcnt_value ());
}

const char *zlink_msg_gets (const zlink_msg_t *msg_, const char *property_)
{
    const zlink::metadata_t *metadata =
      reinterpret_cast<const zlink::msg_t *> (msg_)->metadata ();
    const char *value = NULL;
    if (metadata)
        value = metadata->get (std::string (property_));
    if (value)
        return value;

    errno = EINVAL;
    return NULL;
}

int zlink_proxy (void *frontend_, void *backend_, void *capture_)
{
    if (!frontend_ || !backend_) {
        errno = EFAULT;
        return -1;
    }

    socket_handle_t frontend = as_socket_handle (frontend_);
    if (!frontend.socket)
        return -1;
    socket_handle_t backend = as_socket_handle (backend_);
    if (!backend.socket)
        return -1;

    zlink::socket_base_t *capture_socket = NULL;
    if (capture_) {
        socket_handle_t capture = as_socket_handle (capture_);
        if (!capture.socket)
            return -1;
        capture_socket = capture.socket;
    }

    return zlink::proxy (frontend.socket, backend.socket, capture_socket);
}

int zlink_proxy_steerable (void *frontend_,
                         void *backend_,
                         void *capture_,
                         void *control_)
{
    if (!frontend_ || !backend_) {
        errno = EFAULT;
        return -1;
    }

    socket_handle_t frontend = as_socket_handle (frontend_);
    if (!frontend.socket)
        return -1;
    socket_handle_t backend = as_socket_handle (backend_);
    if (!backend.socket)
        return -1;

    zlink::socket_base_t *capture_socket = NULL;
    if (capture_) {
        socket_handle_t capture = as_socket_handle (capture_);
        if (!capture.socket)
            return -1;
        capture_socket = capture.socket;
    }

    zlink::socket_base_t *control_socket = NULL;
    if (control_) {
        socket_handle_t control = as_socket_handle (control_);
        if (!control.socket)
            return -1;
        control_socket = control.socket;
    }

    return zlink::proxy_steerable (frontend.socket, backend.socket,
                                 capture_socket, control_socket);
}

int zlink_has (const char *capability_)
{
    // TCP is always available as a core transport
    if (strcmp (capability_, "tcp") == 0)
        return true;
#if defined(ZLINK_HAVE_IPC)
    if (strcmp (capability_, zlink::protocol_name::ipc) == 0)
        return true;
#endif
#if defined(ZLINK_HAVE_TLS)
    if (strcmp (capability_, "tls") == 0)
        return true;
#endif
#if defined(ZLINK_HAVE_WS)
    if (strcmp (capability_, "ws") == 0)
        return true;
#endif
#if defined(ZLINK_HAVE_WSS)
    if (strcmp (capability_, "wss") == 0)
        return true;
#endif
    return false;
}
