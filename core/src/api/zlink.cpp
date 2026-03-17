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

static void discard_xpub_event (int, const uint8_t *, size_t, void *)
{
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

void zlink_service_monitor_ignore_handler (const zlink_service_event_t *,
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
    zlink_spot_handler_fn handler;
    void *userdata;
};

struct spot_node_handler_registry_t
{
    zlink::mutex_t sync;
    std::map<zlink::spot_node_t *, spot_node_handler_entry_t> handlers;
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
    zlink_spot_handler_fn handler;
    void *handler_userdata;
    zlink::spot_node_t::pub_defaults_t pending_pub_defaults;
    zlink::spot_node_t::sub_defaults_t pending_sub_defaults;
};

enum service_io_model_t
{
    service_io_model_recv = 0,
    service_io_model_callback = 1
};

struct service_mode_state_t
{
    service_mode_state_t () :
        model (service_io_model_recv),
        callback_installed (false),
        poller_refs (0)
    {
    }

    service_io_model_t model;
    bool callback_installed;
    int poller_refs;
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
    poller_registration_t () : socket (NULL), subject (NULL), subject_kind (poller_subject_none) {}

    void *socket;
    void *subject;
    poller_subject_kind_t subject_kind;
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
static int ensure_service_recv_model (service_mode_state_t *state_);
static int ensure_service_callback_model (service_mode_state_t *state_);

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

static int gateway_transition_to_callback_mode (zlink::gateway_t *gateway_)
{
    if (!gateway_) {
        errno = EFAULT;
        return -1;
    }
    gateway_mode_registry_t &registry = gateway_mode_registry ();
    zlink::scoped_lock_t lock (registry.sync);
    return transition_service_to_callback_mode (&registry.states[gateway_]);
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

static void gateway_revert_callback_transition (zlink::gateway_t *gateway_)
{
    if (!gateway_)
        return;
    gateway_mode_registry_t &registry = gateway_mode_registry ();
    zlink::scoped_lock_t lock (registry.sync);
    service_mode_state_t &state = registry.states[gateway_];
    state.callback_installed = false;
    state.model = service_io_model_recv;
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
    service_mode_state_t &state = registry.states[node_];
    state.callback_installed = false;
    state.model = service_io_model_recv;
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
    service_mode_state_t &state = registry.states[spot_];
    state.callback_installed = false;
    state.model = service_io_model_recv;
}

static int gateway_require_callback_model (zlink::gateway_t *gateway_)
{
    if (!gateway_) {
        errno = EFAULT;
        return -1;
    }
    gateway_mode_registry_t &registry = gateway_mode_registry ();
    zlink::scoped_lock_t lock (registry.sync);
    return ensure_service_callback_model (&registry.states[gateway_]);
}

static int spot_node_require_callback_model (zlink::spot_node_t *node_)
{
    if (!node_) {
        errno = EFAULT;
        return -1;
    }
    spot_node_mode_registry_t &registry = spot_node_mode_registry ();
    zlink::scoped_lock_t lock (registry.sync);
    return ensure_service_callback_model (&registry.states[node_]);
}

static int spot_require_callback_model (spot_handle_t *spot_)
{
    if (!spot_) {
        errno = EFAULT;
        return -1;
    }
    spot_mode_registry_t &registry = spot_mode_registry ();
    zlink::scoped_lock_t lock (registry.sync);
    return ensure_service_callback_model (&registry.states[spot_]);
}

static int increment_gateway_poller_ref (zlink::gateway_t *gateway_)
{
    if (!gateway_) {
        errno = EFAULT;
        return -1;
    }
    gateway_mode_registry_t &registry = gateway_mode_registry ();
    zlink::scoped_lock_t lock (registry.sync);
    service_mode_state_t &state = registry.states[gateway_];
    if (state.model == service_io_model_callback) {
        errno = EBUSY;
        return -1;
    }
    ++state.poller_refs;
    return 0;
}

static int increment_spot_node_poller_ref (zlink::spot_node_t *node_)
{
    if (!node_) {
        errno = EFAULT;
        return -1;
    }
    spot_node_mode_registry_t &registry = spot_node_mode_registry ();
    zlink::scoped_lock_t lock (registry.sync);
    service_mode_state_t &state = registry.states[node_];
    if (state.model == service_io_model_callback) {
        errno = EBUSY;
        return -1;
    }
    ++state.poller_refs;
    return 0;
}

static int increment_spot_poller_ref (spot_handle_t *spot_)
{
    if (!spot_) {
        errno = EFAULT;
        return -1;
    }
    spot_mode_registry_t &registry = spot_mode_registry ();
    zlink::scoped_lock_t lock (registry.sync);
    service_mode_state_t &state = registry.states[spot_];
    if (state.model == service_io_model_callback) {
        errno = EBUSY;
        return -1;
    }
    ++state.poller_refs;
    return 0;
}

static void decrement_gateway_poller_ref (zlink::gateway_t *gateway_)
{
    if (!gateway_)
        return;
    gateway_mode_registry_t &registry = gateway_mode_registry ();
    zlink::scoped_lock_t lock (registry.sync);
    service_mode_state_t &state = registry.states[gateway_];
    if (state.poller_refs > 0)
        --state.poller_refs;
}

static void decrement_spot_node_poller_ref (zlink::spot_node_t *node_)
{
    if (!node_)
        return;
    spot_node_mode_registry_t &registry = spot_node_mode_registry ();
    zlink::scoped_lock_t lock (registry.sync);
    service_mode_state_t &state = registry.states[node_];
    if (state.poller_refs > 0)
        --state.poller_refs;
}

static void decrement_spot_poller_ref (spot_handle_t *spot_)
{
    if (!spot_)
        return;
    spot_mode_registry_t &registry = spot_mode_registry ();
    zlink::scoped_lock_t lock (registry.sync);
    service_mode_state_t &state = registry.states[spot_];
    if (state.poller_refs > 0)
        --state.poller_refs;
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
    if (state_->callback_installed) {
        errno = EBUSY;
        return -1;
    }
    if (state_->poller_refs > 0) {
        errno = EBUSY;
        return -1;
    }
    state_->callback_installed = true;
    state_->model = service_io_model_callback;
    return 0;
}

static int ensure_service_recv_model (service_mode_state_t *state_)
{
    if (!state_) {
        errno = EFAULT;
        return -1;
    }
    if (state_->model == service_io_model_callback) {
        errno = EBUSY;
        return -1;
    }
    return 0;
}

static int ensure_service_callback_model (service_mode_state_t *state_)
{
    if (!state_) {
        errno = EFAULT;
        return -1;
    }
    if (state_->model != service_io_model_callback) {
        errno = EBUSY;
        return -1;
    }
    return 0;
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

    while (zlink_msg_more (&frames.back ())) {
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
        dealer = ctx->create_socket (ZLINK_DEALER);
        if (!dealer)
            return -1;
        unsigned char rid[5];
        rid[0] = 0;
        uint32_t rid_word = zlink::generate_random ();
        if (rid_word == 0)
            rid_word = 1;
        memcpy (rid + 1, &rid_word, sizeof (rid_word));
        dealer->setsockopt (ZLINK_ROUTING_ID, rid, sizeof (rid));
        const int linger = 0;
        const int sndtimeo_ms = 1000;
        const int rcvtimeo_ms = 1000;
        dealer->setsockopt (ZLINK_LINGER, &linger, sizeof (linger));
        dealer->setsockopt (ZLINK_SNDTIMEO, &sndtimeo_ms,
                            sizeof (sndtimeo_ms));
        dealer->setsockopt (ZLINK_RCVTIMEO, &rcvtimeo_ms,
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
        errno = ENOTSOCK;
        return handle;
    }

    zlink::socket_base_t *s = static_cast<zlink::socket_base_t *> (s_);
    if (!s->check_tag ()) {
        errno = ENOTSOCK;
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
    if (!spot)
        return NULL;
    return &spot->public_api;
}

void zlink::destroy_spot_handle_for_testing (void *spot_)
{
    spot_handle_t *spot = as_spot_handle (spot_);
    if (!spot)
        return;

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
    if (handle_.socket->getsockopt (ZLINK_TYPE, &type, &type_len) != 0)
        return false;
    return type == ZLINK_STREAM;
}

static int socket_type (socket_handle_t handle_)
{
    if (!handle_.socket)
        return -1;

    int type = -1;
    size_t type_len = sizeof (type);
    if (handle_.socket->getsockopt (ZLINK_TYPE, &type, &type_len) != 0)
        return -1;
    return type;
}

static int legacy_socket_type (int type_)
{
    switch (type_) {
        case ZLINK_SOCKET_PAIR:
            return ZLINK_PAIR;
        case ZLINK_SOCKET_PUB:
            return ZLINK_PUB;
        case ZLINK_SOCKET_SUB:
            return ZLINK_SUB;
        case ZLINK_SOCKET_DEALER:
            return ZLINK_DEALER;
        case ZLINK_SOCKET_ROUTER:
            return ZLINK_ROUTER;
        case ZLINK_SOCKET_XPUB:
            return ZLINK_XPUB;
        case ZLINK_SOCKET_XSUB:
            return ZLINK_XSUB;
        case ZLINK_SOCKET_STREAM:
            return ZLINK_STREAM;
        default:
            return type_;
    }
}

static int public_socket_type (int legacy_type_)
{
    switch (legacy_type_) {
        case ZLINK_PAIR:
            return ZLINK_SOCKET_PAIR;
        case ZLINK_PUB:
            return ZLINK_SOCKET_PUB;
        case ZLINK_SUB:
            return ZLINK_SOCKET_SUB;
        case ZLINK_DEALER:
            return ZLINK_SOCKET_DEALER;
        case ZLINK_ROUTER:
            return ZLINK_SOCKET_ROUTER;
        case ZLINK_XPUB:
            return ZLINK_SOCKET_XPUB;
        case ZLINK_XSUB:
            return ZLINK_SOCKET_XSUB;
        case ZLINK_STREAM:
            return ZLINK_SOCKET_STREAM;
        default:
            return legacy_type_;
    }
}

static int legacy_socket_option (int option_)
{
    switch (option_) {
        case ZLINK_SOCKOPT_AFFINITY:
            return ZLINK_AFFINITY;
        case ZLINK_SOCKOPT_ROUTING_ID:
            return ZLINK_ROUTING_ID;
        case ZLINK_SOCKOPT_SUBSCRIBE:
            return ZLINK_SUBSCRIBE;
        case ZLINK_SOCKOPT_UNSUBSCRIBE:
            return ZLINK_UNSUBSCRIBE;
        case ZLINK_SOCKOPT_RATE:
            return ZLINK_RATE;
        case ZLINK_SOCKOPT_RECOVERY_IVL:
            return ZLINK_RECOVERY_IVL;
        case ZLINK_SOCKOPT_SNDBUF:
            return ZLINK_SNDBUF;
        case ZLINK_SOCKOPT_RCVBUF:
            return ZLINK_RCVBUF;
        case ZLINK_SOCKOPT_RCVMORE:
            return ZLINK_RCVMORE;
        case ZLINK_SOCKOPT_FD:
            return ZLINK_SOCKOPT_FD;
        case ZLINK_SOCKOPT_EVENTS:
            return ZLINK_SOCKOPT_EVENTS;
        case ZLINK_SOCKOPT_TYPE:
            return ZLINK_SOCKOPT_TYPE;
        case ZLINK_SOCKOPT_LINGER:
            return ZLINK_LINGER;
        case ZLINK_SOCKOPT_RECONNECT_IVL:
            return ZLINK_RECONNECT_IVL;
        case ZLINK_SOCKOPT_BACKLOG:
            return ZLINK_BACKLOG;
        case ZLINK_SOCKOPT_RECONNECT_IVL_MAX:
            return ZLINK_RECONNECT_IVL_MAX;
        case ZLINK_SOCKOPT_MAXMSGSIZE:
            return ZLINK_MAXMSGSIZE;
        case ZLINK_SOCKOPT_SNDHWM:
            return ZLINK_SNDHWM;
        case ZLINK_SOCKOPT_RCVHWM:
            return ZLINK_RCVHWM;
        case ZLINK_SOCKOPT_MULTICAST_HOPS:
            return ZLINK_MULTICAST_HOPS;
        case ZLINK_SOCKOPT_RCVTIMEO:
            return ZLINK_RCVTIMEO;
        case ZLINK_SOCKOPT_SNDTIMEO:
            return ZLINK_SNDTIMEO;
        case ZLINK_SOCKOPT_LAST_ENDPOINT:
            return ZLINK_SOCKOPT_LAST_ENDPOINT;
        case ZLINK_SOCKOPT_ROUTER_MANDATORY:
            return ZLINK_ROUTER_MANDATORY;
        case ZLINK_SOCKOPT_TCP_KEEPALIVE:
            return ZLINK_TCP_KEEPALIVE;
        case ZLINK_SOCKOPT_TCP_KEEPALIVE_CNT:
            return ZLINK_TCP_KEEPALIVE_CNT;
        case ZLINK_SOCKOPT_TCP_KEEPALIVE_IDLE:
            return ZLINK_TCP_KEEPALIVE_IDLE;
        case ZLINK_SOCKOPT_TCP_KEEPALIVE_INTVL:
            return ZLINK_TCP_KEEPALIVE_INTVL;
        case ZLINK_SOCKOPT_IMMEDIATE:
            return ZLINK_IMMEDIATE;
        case ZLINK_SOCKOPT_XPUB_VERBOSE:
            return ZLINK_XPUB_VERBOSE;
        case ZLINK_SOCKOPT_IPV6:
            return ZLINK_IPV6;
        case ZLINK_SOCKOPT_PROBE_ROUTER:
            return ZLINK_PROBE_ROUTER;
        case ZLINK_SOCKOPT_CONFLATE:
            return ZLINK_CONFLATE;
        case ZLINK_SOCKOPT_ROUTER_HANDOVER:
            return ZLINK_ROUTER_HANDOVER;
        case ZLINK_SOCKOPT_TOS:
            return ZLINK_TOS;
        case ZLINK_SOCKOPT_CONNECT_ROUTING_ID:
            return ZLINK_CONNECT_ROUTING_ID;
        case ZLINK_SOCKOPT_HANDSHAKE_IVL:
            return ZLINK_HANDSHAKE_IVL;
        case ZLINK_SOCKOPT_XPUB_NODROP:
            return ZLINK_XPUB_NODROP;
        case ZLINK_SOCKOPT_BLOCKY:
            return ZLINK_BLOCKY;
        case ZLINK_SOCKOPT_XPUB_MANUAL:
            return ZLINK_XPUB_MANUAL;
        case ZLINK_SOCKOPT_XPUB_WELCOME_MSG:
            return ZLINK_XPUB_WELCOME_MSG;
        case ZLINK_SOCKOPT_STREAM_NOTIFY:
            return ZLINK_STREAM_NOTIFY;
        case ZLINK_SOCKOPT_INVERT_MATCHING:
            return ZLINK_INVERT_MATCHING;
        case ZLINK_SOCKOPT_HEARTBEAT_IVL:
            return ZLINK_HEARTBEAT_IVL;
        case ZLINK_SOCKOPT_HEARTBEAT_TTL:
            return ZLINK_HEARTBEAT_TTL;
        case ZLINK_SOCKOPT_HEARTBEAT_TIMEOUT:
            return ZLINK_HEARTBEAT_TIMEOUT;
        case ZLINK_SOCKOPT_XPUB_VERBOSER:
            return ZLINK_XPUB_VERBOSER;
        case ZLINK_SOCKOPT_CONNECT_TIMEOUT:
            return ZLINK_CONNECT_TIMEOUT;
        case ZLINK_SOCKOPT_TCP_MAXRT:
            return ZLINK_TCP_MAXRT;
        case ZLINK_SOCKOPT_MULTICAST_MAXTPDU:
            return ZLINK_MULTICAST_MAXTPDU;
        case ZLINK_SOCKOPT_BINDTODEVICE:
            return ZLINK_BINDTODEVICE;
        case ZLINK_SOCKOPT_TLS_CERT:
            return ZLINK_TLS_CERT;
        case ZLINK_SOCKOPT_TLS_KEY:
            return ZLINK_TLS_KEY;
        case ZLINK_SOCKOPT_TLS_CA:
            return ZLINK_TLS_CA;
        case ZLINK_SOCKOPT_TLS_VERIFY:
            return ZLINK_TLS_VERIFY;
        case ZLINK_SOCKOPT_XPUB_MANUAL_LAST_VALUE:
            return ZLINK_XPUB_MANUAL_LAST_VALUE;
        case ZLINK_SOCKOPT_TLS_REQUIRE_CLIENT_CERT:
            return ZLINK_TLS_REQUIRE_CLIENT_CERT;
        case ZLINK_SOCKOPT_TLS_HOSTNAME:
            return ZLINK_TLS_HOSTNAME;
        case ZLINK_SOCKOPT_TLS_TRUST_SYSTEM:
            return ZLINK_TLS_TRUST_SYSTEM;
        case ZLINK_SOCKOPT_TLS_PASSWORD:
            return ZLINK_TLS_PASSWORD;
        case ZLINK_SOCKOPT_ONLY_FIRST_SUBSCRIBE:
            return ZLINK_ONLY_FIRST_SUBSCRIBE;
        case ZLINK_SOCKOPT_TOPICS_COUNT:
            return ZLINK_TOPICS_COUNT;
        case ZLINK_SOCKOPT_ZMP_METADATA:
            return ZLINK_ZMP_METADATA;
        case ZLINK_SOCKOPT_TCP_NODELAY:
            return ZLINK_TCP_NODELAY;
        default:
            return option_;
    }
}

static bool is_send_only_socket_type (int type_)
{
    return type_ == ZLINK_PUB;
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

static void discard_xpub_handler (int, const uint8_t *, size_t, void *)
{
}

static int install_spot_node_handler (zlink::spot_node_t *node_,
                                      zlink_spot_handler_fn handler_,
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

    const int legacy_type = legacy_socket_type (type_);
    if (!is_send_only_socket_type (legacy_type)) {
        switch (legacy_type) {
            case ZLINK_PAIR:
            case ZLINK_DEALER:
            case ZLINK_ROUTER:
            case ZLINK_STREAM:
            case ZLINK_SUB:
            case ZLINK_XSUB:
            case ZLINK_XPUB:
                break;
            default:
                errno = EINVAL;
                return NULL;
        }
    }

    zlink::ctx_t *ctx = static_cast<zlink::ctx_t *> (ctx_);
    zlink::socket_base_t *socket = ctx->create_socket (legacy_type);
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
    if (is_registered_gateway_handle (s_)) {
        zlink::gateway_t *gateway = static_cast<zlink::gateway_t *> (s_);
        if (!handler_) {
            errno = EINVAL;
            return -1;
        }
        zlink::service_public_api_scope_t admission (gateway->public_api_guard ());
        if (!admission.acquired ())
            return -1;
        if (gateway_transition_to_callback_mode (gateway) != 0)
            return -1;
        const int rc = gateway->set_handler (handler_, userdata_);
        if (rc != 0)
            gateway_revert_callback_transition (gateway);
        return rc;
    }

    socket_handle_t handle = as_socket_handle (s_);
    if (!handle.socket)
        return -1;

    if (!handler_) {
        errno = EINVAL;
        return -1;
    }

    if (handler_ == &discard_socket_parts) {
        errno = EINVAL;
        return -1;
    }

    const int type = socket_type (handle);
    switch (type) {
        case ZLINK_PAIR:
        case ZLINK_DEALER:
        case ZLINK_ROUTER:
            return handle.socket->socket_set_msg_handler_with_userdata (
              handler_, NULL, userdata_);
        case ZLINK_STREAM:
            return handle.socket->stream_set_msg_handler_with_userdata (
              handler_, userdata_);
        default:
            errno = EINVAL;
            return -1;
    }
}

int zlink_recv_spot_handler (void *s_,
                             zlink_spot_handler_fn handler_,
                             void *userdata_)
{
    if (is_registered_spot_handle (s_)) {
        spot_handle_t *spot = static_cast<spot_handle_t *> (s_);
        if (!handler_) {
            errno = EINVAL;
            return -1;
        }
        if (handler_ == &discard_spot_parts) {
            errno = EINVAL;
            return -1;
        }
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
        if (!handler_) {
            errno = EINVAL;
            return -1;
        }
        if (handler_ == &discard_spot_parts) {
            errno = EINVAL;
            return -1;
        }
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

    if (!handler_) {
        errno = EINVAL;
        return -1;
    }

    if (handler_ == &discard_spot_parts) {
        errno = EINVAL;
        return -1;
    }

    const int type = socket_type (handle);
    if (type != ZLINK_SUB && type != ZLINK_XSUB) {
        errno = EINVAL;
        return -1;
    }

    return handle.socket->socket_set_spot_handler_with_userdata (
      handler_, userdata_);
}

int zlink_recv_xpub_handler (void *s_,
                             zlink_xpub_handler_fn handler_,
                             void *userdata_)
{
    socket_handle_t handle = as_socket_handle (s_);
    if (!handle.socket)
        return -1;

    if (!handler_) {
        errno = EINVAL;
        return -1;
    }

    if (handler_ == &discard_xpub_event) {
        errno = EINVAL;
        return -1;
    }

    const int type = socket_type (handle);
    if (type != ZLINK_XPUB) {
        errno = EINVAL;
        return -1;
    }

    return handle.socket->socket_set_xpub_handler_with_userdata (
      handler_, userdata_);
}

int zlink_socket_send_ready_handler (
  void *s_,
  zlink_send_ready_handler_fn handler_,
  void *userdata_)
{
    socket_handle_t handle = as_socket_handle (s_);
    if (!handle.socket)
        return -1;

    const int type = socket_type (handle);
    if (type == ZLINK_SUB || type == ZLINK_XSUB) {
        errno = EINVAL;
        return -1;
    }

    return handle.socket->socket_set_send_ready_handler_with_userdata (
      handler_, NULL, userdata_);
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
        (void) raw_monitor_source->monitor (NULL, 0, 3, ZLINK_PAIR);
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

int zlink_setsockopt (void *s_,
                      zlink_socket_option_t option_,
                      const void *optval_,
                      size_t optvallen_)
{
    socket_handle_t handle = as_socket_handle (s_);
    if (!handle.socket)
        return -1;
    return handle.socket->setsockopt (legacy_socket_option (option_), optval_,
                                      optvallen_);
}

int zlink_getsockopt (void *s_,
                      zlink_socket_option_t option_,
                      void *optval_,
                      size_t *optvallen_)
{
    socket_handle_t handle = as_socket_handle (s_);
    if (!handle.socket)
        return -1;
    const int legacy_option = legacy_socket_option (option_);
    const int rc = handle.socket->getsockopt (legacy_option, optval_, optvallen_);
    if (rc != 0)
        return rc;

    if (legacy_option == ZLINK_SOCKOPT_TYPE && optval_ && optvallen_
        && *optvallen_ >= sizeof (int)) {
        int type = 0;
        memcpy (&type, optval_, sizeof (type));
        type = public_socket_type (type);
        memcpy (optval_, &type, sizeof (type));
    }

    return 0;
}

void *zlink_socket_monitor_open (void *s_,
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
      handle.socket->monitor (endpoint, events_, 3, ZLINK_PAIR);
    if (monitor_rc != 0)
        return NULL;

    zlink::socket_base_t *monitor_socket_base =
      handle.socket->get_ctx ()->create_socket (ZLINK_PAIR);
    void *monitor_socket = static_cast<void *> (monitor_socket_base);
    if (!monitor_socket) {
        handle.socket->monitor (NULL, 0, 3, ZLINK_PAIR);
        return NULL;
    }

    if (zlink_connect (monitor_socket, endpoint) != 0) {
        zlink_close (monitor_socket);
        handle.socket->monitor (NULL, 0, 3, ZLINK_PAIR);
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
        handle.socket->monitor (NULL, 0, 3, ZLINK_PAIR);
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

int zlink_service_monitor_close (void **monitor_p_)
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
    (void) socket->setsockopt (ZLINK_LINGER, &linger, sizeof (linger));
    monitor_handler_state_t *monitor_state = find_monitor_handler_state (socket);
    const bool had_dispatch_monitor =
      monitor_state
      && (monitor_state->socket_handler.load (std::memory_order_acquire)
            || monitor_state->service_handler.load (std::memory_order_acquire));
    const bool no_dispatch_monitor =
      monitor_state
      && !monitor_state->socket_handler.load (std::memory_order_acquire)
      && !monitor_state->service_handler.load (std::memory_order_acquire);
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
    free (parts_);
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

int zlink_registry_setsockopt (void *registry_,
                               zlink_registry_socket_role_t socket_role_,
                               zlink_socket_option_t option_,
                               const void *optval_,
                               size_t optvallen_)
{
    if (!registry_)
        return -1;
    zlink::registry_t *registry = static_cast<zlink::registry_t *> (registry_);
    if (!registry->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return registry->set_socket_option (socket_role_,
                                        legacy_socket_option (option_), optval_,
                                        optvallen_);
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

void *zlink_discovery_monitor_open (
  void *discovery_,
  zlink_discovery_monitor_event_mask_t events_,
  zlink_service_monitor_handler_fn handler_,
  void *userdata_)
{
    if (!discovery_)
        return NULL;
    if (!handler_) {
        errno = EINVAL;
        return NULL;
    }
    zlink::discovery_t *discovery =
      static_cast<zlink::discovery_t *> (discovery_);
    if (!discovery->check_tag ()) {
        errno = EFAULT;
        return NULL;
    }
    zlink_service_monitor_handler_fn effective_handler = handler_;
    if (handler_ == &zlink_service_monitor_ignore_handler)
        effective_handler = NULL;

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
        zlink_service_monitor_close (&monitor);
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

int zlink_gateway_send_ready_handler (
  void *gateway_,
  zlink_send_ready_handler_fn handler_,
  void *userdata_)
{
    if (!gateway_)
        return -1;
    zlink::gateway_t *gateway = static_cast<zlink::gateway_t *> (gateway_);
    if (!gateway->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    zlink::service_public_api_scope_t admission (gateway->public_api_guard ());
    if (!admission.acquired ())
        return -1;
    if (gateway_require_callback_model (gateway) != 0)
        return -1;
    return gateway->set_send_ready_handler (handler_, userdata_);
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

int zlink_gateway_send (void *gateway_,
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

int zlink_gateway_send_rid (void *gateway_,
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

int zlink_gateway_recv (void *gateway_,
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

int zlink_gateway_set_option (void *gateway_,
                              zlink_gateway_option_t option_,
                              const void *optval_,
                              size_t optvallen_)
{
    if (!gateway_)
        return -1;
    zlink::gateway_t *gateway = static_cast<zlink::gateway_t *> (gateway_);
    if (!gateway->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return gateway->set_option (option_, optval_, optvallen_);
}

int zlink_gateway_set_routing_id (void *gateway_,
                                  const void *data_,
                                  size_t size_)
{
    if (!gateway_)
        return -1;
    zlink::gateway_t *gateway = static_cast<zlink::gateway_t *> (gateway_);
    if (!gateway->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return gateway->set_routing_id (data_, size_);
}

int zlink_gateway_routing_id (void *gateway_, zlink_routing_id_t *out_)
{
    if (!gateway_)
        return -1;
    zlink::gateway_t *gateway = static_cast<zlink::gateway_t *> (gateway_);
    if (!gateway->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return gateway->routing_id (out_);
}

int zlink_gateway_set_tls_client (void *gateway_,
                                const char *ca_cert_,
                                const char *hostname_,
                                int trust_system_)
{
    if (!gateway_)
        return -1;
    zlink::gateway_t *gateway = static_cast<zlink::gateway_t *> (gateway_);
    if (!gateway->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return gateway->set_tls_client (ca_cert_, hostname_, trust_system_);
}

int zlink_gateway_set_tls_server (void *gateway_,
                                  const char *cert_,
                                  const char *key_)
{
    if (!gateway_)
        return -1;
    zlink::gateway_t *gateway = static_cast<zlink::gateway_t *> (gateway_);
    if (!gateway->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return gateway->set_tls_server (cert_, key_);
}

int zlink_gateway_last_endpoint (void *gateway_,
                                 char *endpoint_,
                                 size_t *size_)
{
    if (!gateway_)
        return -1;
    zlink::gateway_t *gateway = static_cast<zlink::gateway_t *> (gateway_);
    if (!gateway->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return gateway->last_endpoint (endpoint_, size_);
}

void *zlink_gateway_monitor_open (
  void *gateway_,
  zlink_gateway_monitor_event_mask_t events_,
  zlink_service_monitor_handler_fn handler_,
  void *userdata_)
{
    if (!gateway_)
        return NULL;
    if (!handler_) {
        errno = EINVAL;
        return NULL;
    }
    zlink::gateway_t *gateway = static_cast<zlink::gateway_t *> (gateway_);
    if (!gateway->check_tag ()) {
        errno = EFAULT;
        return NULL;
    }
    zlink_service_monitor_handler_fn effective_handler = handler_;
    if (handler_ == &zlink_service_monitor_ignore_handler)
        effective_handler = NULL;

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
        zlink_service_monitor_close (&monitor);
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

int zlink_spot_node_connect_peer_pub (void *node_,
                                    const char *peer_pub_endpoint_)
{
    if (!node_)
        return -1;
    zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (node_);
    if (!node->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return node->connect_peer_pub (peer_pub_endpoint_);
}

int zlink_spot_node_disconnect_peer_pub (void *node_,
                                       const char *peer_pub_endpoint_)
{
    if (!node_)
        return -1;
    zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (node_);
    if (!node->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return node->disconnect_peer_pub (peer_pub_endpoint_);
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

int zlink_spot_node_set_tls_server (void *node_,
                                  const char *cert_,
                                  const char *key_)
{
    if (!node_)
        return -1;
    zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (node_);
    if (!node->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return node->set_tls_server (cert_, key_);
}

int zlink_spot_node_set_tls_client (void *node_,
                                  const char *ca_cert_,
                                  const char *hostname_,
                                  int trust_system_)
{
    if (!node_)
        return -1;
    zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (node_);
    if (!node->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return node->set_tls_client (ca_cert_, hostname_, trust_system_);
}

int zlink_spot_node_publish (void *node_,
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

int zlink_spot_node_subscribe (void *node_, const char *topic_id_)
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

int zlink_spot_node_subscribe_pattern (void *node_, const char *pattern_)
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

int zlink_spot_node_unsubscribe (void *node_,
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

int zlink_spot_node_send_ready_handler (
  void *node_,
  zlink_send_ready_handler_fn handler_,
  void *userdata_)
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
    if (spot_node_require_callback_model (node) != 0)
        return -1;
    return node->set_send_ready_handler (handler_, userdata_);
}

void *zlink_spot_node_monitor_open (void *node_,
                                    zlink_spot_role_t role_,
                                    zlink_spot_monitor_event_mask_t events_,
                                    zlink_service_monitor_handler_fn handler_,
                                    void *userdata_)
{
    if (!node_) {
        errno = EFAULT;
        return NULL;
    }
    if (!handler_) {
        errno = EINVAL;
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

void *zlink_spot_new (void *spot_node_)
{
    if (!spot_node_) {
        errno = EFAULT;
        return NULL;
    }

    zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (spot_node_);
    if (!node->check_tag ()) {
        errno = EFAULT;
        return NULL;
    }
    zlink::service_public_api_scope_t admission (node->public_api_guard ());
    if (!admission.acquired ())
        return NULL;
    if (node->ensure_healthy () != 0)
        return NULL;

    spot_handle_t *spot = new (std::nothrow) spot_handle_t;
    if (!spot) {
        errno = ENOMEM;
        return NULL;
    }
    spot->node = node;
    register_spot_mode_state (spot);
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
    if (!spot->public_api.begin_close_or_fail_busy ())
        return -1;
    if ((spot->pub && has_open_service_monitor_for_subject (spot->pub))
        || (spot->sub && has_open_service_monitor_for_subject (spot->sub))) {
        spot->public_api.cancel_close ();
        errno = EBUSY;
        return -1;
    }

    int rc = 0;
    if (spot->sub) {
        rc = spot->sub->destroy ();
        if (rc == 0)
            delete spot->sub;
    }
    if (rc == 0 && spot->pub) {
        rc = spot->pub->destroy ();
        if (rc == 0)
            delete spot->pub;
    }
    if (rc != 0) {
        spot->public_api.cancel_close ();
        return -1;
    }
    spot->tag = 0xdeadbeef;
    erase_spot_mode_state (spot);
    delete spot;
    *spot_p_ = NULL;
    return 0;
}

int zlink_spot_publish (void *spot_,
                        const char *topic_id_,
                        zlink_msg_t *parts_,
                        size_t part_count_,
                        zlink_send_flags_t flags_)
{
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

int zlink_spot_sub_recv (void *sub_,
                         zlink_msg_t **parts_,
                         size_t *part_count_,
                         int flags_,
                         char *topic_id_out_,
                         size_t *topic_id_len_)
{
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
    return sub->recv (parts_, part_count_, flags_, topic_id_out_,
                      topic_id_len_);
}

int zlink_spot_node_recv (void *node_,
                          zlink_msg_t **parts_,
                          size_t *part_count_,
                          int flags_,
                          char *topic_id_out_,
                          size_t *topic_id_len_)
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
    return receiver->impl ()->recv (parts_, part_count_, flags_, topic_id_out_,
                                    topic_id_len_);
}

int zlink_spot_subscribe (void *spot_, const char *topic_id_)
{
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

int zlink_spot_subscribe_pattern (void *spot_, const char *pattern_)
{
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

int zlink_spot_unsubscribe (void *spot_, const char *topic_id_or_pattern_)
{
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

int zlink_spot_send_ready_handler (void *spot_,
                                       zlink_send_ready_handler_fn handler_,
                                       void *userdata_)
{
    spot_handle_t *spot = as_spot_handle (spot_);
    if (!spot)
        return -1;
    zlink::service_public_api_scope_t admission (spot->public_api);
    if (!admission.acquired ())
        return -1;
    if (spot_require_callback_model (spot) != 0)
        return -1;
    zlink::spot_pub_t *pub = ensure_spot_pub (spot);
    if (!pub) {
        errno = ENOTSUP;
        return -1;
    }
    return pub->set_send_ready_handler (handler_, spot, userdata_);
}

int zlink_spot_set_pub_option (void *spot_,
                               zlink_spot_pub_option_t option_,
                               const void *optval_,
                               size_t optvallen_)
{
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

int zlink_spot_set_sub_option (void *spot_,
                               zlink_spot_sub_option_t option_,
                               const void *optval_,
                               size_t optvallen_)
{
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
    if (!handler_) {
        errno = EINVAL;
        zlink_service_monitor_close (&monitor_);
        return NULL;
    }

    zlink_service_monitor_handler_fn effective_handler = handler_;
    if (handler_ == &zlink_service_monitor_ignore_handler)
        effective_handler = NULL;

    socket_handle_t handle = as_socket_handle (monitor_);
    if (!handle.socket) {
        zlink_service_monitor_close (&monitor_);
        errno = EFAULT;
        return NULL;
    }
    if (set_monitor_handler_state (handle.socket, NULL, effective_handler, true,
                                   snapshot_provider_, snapshot_subject_,
                                   NULL, userdata_)
        != 0) {
        const int err = errno;
        zlink_service_monitor_close (&monitor_);
        errno = err;
        return NULL;
    }
    return monitor_;
}

void *zlink_spot_monitor_open (void *spot_,
                               zlink_spot_role_t role_,
                               zlink_spot_monitor_event_mask_t events_,
                               zlink_service_monitor_handler_fn handler_,
                               void *userdata_)
{
    spot_handle_t *spot = as_spot_handle (spot_);
    if (!spot)
        return NULL;
    if (!handler_) {
        errno = EINVAL;
        return NULL;
    }
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

int zlink_spot_node_set_pub_option (void *node_,
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

int zlink_spot_node_set_sub_option (void *node_,
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

    zlink_spot_handler_fn handler = NULL;
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
              static_cast<zlink::gateway_t *> (registration_.subject));
            break;
        case poller_subject_spot_pub:
        case poller_subject_spot_sub:
            decrement_spot_poller_ref (
              static_cast<spot_handle_t *> (registration_.subject));
            break;
        case poller_subject_spot_node_pub:
        case poller_subject_spot_node_sub:
            decrement_spot_node_poller_ref (
              static_cast<zlink::spot_node_t *> (registration_.subject));
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
    socket_handle_t handle = as_socket_handle (socket_);
    if (!handle.socket)
        return -1;
    return poller->poller.add (handle.socket, user_data_, events_);
}

int zlink_poller_modify (void *poller_, void *socket_, short events_)
{
    poller_handle_t *poller = as_poller_handle (poller_);
    if (!poller)
        return -1;
    socket_handle_t handle = as_socket_handle (socket_);
    if (!handle.socket)
        return -1;
    return poller->poller.modify (handle.socket, events_);
}

int zlink_poller_remove (void *poller_, void *socket_)
{
    poller_handle_t *poller = as_poller_handle (poller_);
    if (!poller)
        return -1;
    socket_handle_t handle = as_socket_handle (socket_);
    if (!handle.socket)
        return -1;
    return poller->poller.remove (handle.socket);
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

int zlink_poller_add_gateway (void *poller_,
                              void *gateway_,
                              void *user_data_,
                              short events_)
{
    poller_handle_t *poller = as_poller_handle (poller_);
    if (!poller)
        return -1;
    if (!is_registered_gateway_handle (gateway_)) {
        errno = EFAULT;
        return -1;
    }
    zlink::gateway_t *gateway = static_cast<zlink::gateway_t *> (gateway_);
    if (validate_gateway_poller_events (events_) != 0)
        return -1;
    if (increment_gateway_poller_ref (gateway) != 0)
        return -1;
    zlink::socket_base_t *socket = zlink::gateway_access_t::router_socket (gateway);
    if (!socket
        || poller_add_registration (poller, socket, user_data_, events_, gateway_,
                                    poller_subject_gateway)
             != 0) {
        decrement_gateway_poller_ref (gateway);
        if (!socket)
            errno = ENOTSUP;
        return -1;
    }
    return 0;
}

int zlink_poller_modify_gateway (void *poller_, void *gateway_, short events_)
{
    poller_handle_t *poller = as_poller_handle (poller_);
    if (!poller)
        return -1;
    if (!is_registered_gateway_handle (gateway_)) {
        errno = EFAULT;
        return -1;
    }
    zlink::gateway_t *gateway = static_cast<zlink::gateway_t *> (gateway_);
    if (validate_gateway_poller_events (events_) != 0)
        return -1;
    const int index = poller_find_registration_index (poller, gateway_);
    if (index < 0) {
        errno = EINVAL;
        return -1;
    }
    return poller->poller.modify (
      static_cast<zlink::socket_base_t *> (poller->registrations[index].socket),
      events_);
}

int zlink_poller_remove_gateway (void *poller_, void *gateway_)
{
    poller_handle_t *poller = as_poller_handle (poller_);
    if (!poller)
        return -1;
    if (!is_registered_gateway_handle (gateway_)) {
        errno = EFAULT;
        return -1;
    }
    zlink::gateway_t *gateway = static_cast<zlink::gateway_t *> (gateway_);
    const int index = poller_find_registration_index (poller, gateway_);
    if (index < 0) {
        errno = EINVAL;
        return -1;
    }
    zlink::socket_base_t *socket =
      static_cast<zlink::socket_base_t *> (poller->registrations[index].socket);
    const int rc = poller->poller.remove (socket);
    if (rc == 0) {
        release_poller_registration (poller->registrations[index]);
        poller->registrations.erase (poller->registrations.begin () + index);
    }
    return rc;
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
        return node->ensure_default_sub ();
    }
    errno = EFAULT;
    return NULL;
}

static int increment_spot_subject_poller_ref (void *spot_or_node_)
{
    if (is_registered_spot_handle (spot_or_node_))
        return increment_spot_poller_ref (
          static_cast<spot_handle_t *> (spot_or_node_));
    if (is_registered_spot_node_handle (spot_or_node_))
        return increment_spot_node_poller_ref (
          static_cast<zlink::spot_node_t *> (spot_or_node_));
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

int zlink_poller_add_spot_pub (void *poller_,
                               void *spot_or_node_,
                               void *user_data_,
                               short events_)
{
    poller_handle_t *poller = as_poller_handle (poller_);
    if (!poller)
        return -1;
    if (validate_spot_pub_poller_events (events_) != 0)
        return -1;
    if (increment_spot_subject_poller_ref (spot_or_node_) != 0)
        return -1;
    zlink::spot_pub_t *pub = resolve_spot_pub_subject (spot_or_node_);
    if (!pub
        || poller_add_registration (
             poller, pub->poller_socket (), user_data_, events_, spot_or_node_,
             poller_spot_pub_kind_for_subject (spot_or_node_))
             != 0) {
        poller_registration_t registration;
        registration.subject = spot_or_node_;
        registration.subject_kind =
          poller_spot_pub_kind_for_subject (spot_or_node_);
        release_poller_registration (registration);
        return -1;
    }
    return 0;
}

int zlink_poller_modify_spot_pub (void *poller_,
                                  void *spot_or_node_,
                                  short events_)
{
    poller_handle_t *poller = as_poller_handle (poller_);
    if (!poller)
        return -1;
    if (validate_spot_pub_poller_events (events_) != 0)
        return -1;
    const int index = poller_find_registration_index (
      poller, spot_or_node_, poller_spot_pub_kind_for_subject (spot_or_node_));
    if (index < 0) {
        errno = EINVAL;
        return -1;
    }
    return poller->poller.modify (
      static_cast<zlink::socket_base_t *> (poller->registrations[index].socket),
      events_);
}

int zlink_poller_remove_spot_pub (void *poller_, void *spot_or_node_)
{
    poller_handle_t *poller = as_poller_handle (poller_);
    if (!poller)
        return -1;
    const int index = poller_find_registration_index (
      poller, spot_or_node_, poller_spot_pub_kind_for_subject (spot_or_node_));
    if (index < 0) {
        errno = EINVAL;
        return -1;
    }
    zlink::socket_base_t *socket =
      static_cast<zlink::socket_base_t *> (poller->registrations[index].socket);
    const int rc = poller->poller.remove (socket);
    if (rc == 0) {
        release_poller_registration (poller->registrations[index]);
        poller->registrations.erase (poller->registrations.begin () + index);
    }
    return rc;
}

int zlink_poller_add_spot_sub (void *poller_,
                               void *spot_or_node_,
                               void *user_data_,
                               short events_)
{
    poller_handle_t *poller = as_poller_handle (poller_);
    if (!poller)
        return -1;
    if (validate_spot_sub_poller_events (events_) != 0)
        return -1;
    if (increment_spot_subject_poller_ref (spot_or_node_) != 0)
        return -1;
    zlink::spot_sub_t *sub = resolve_spot_sub_subject (spot_or_node_);
    if (!sub
        || poller_add_registration (
             poller, sub->poller_socket (), user_data_, events_, spot_or_node_,
             poller_spot_sub_kind_for_subject (spot_or_node_))
             != 0) {
        poller_registration_t registration;
        registration.subject = spot_or_node_;
        registration.subject_kind =
          poller_spot_sub_kind_for_subject (spot_or_node_);
        release_poller_registration (registration);
        return -1;
    }
    return 0;
}

int zlink_poller_modify_spot_sub (void *poller_,
                                  void *spot_or_node_,
                                  short events_)
{
    poller_handle_t *poller = as_poller_handle (poller_);
    if (!poller)
        return -1;
    if (validate_spot_sub_poller_events (events_) != 0)
        return -1;
    const int index = poller_find_registration_index (
      poller, spot_or_node_, poller_spot_sub_kind_for_subject (spot_or_node_));
    if (index < 0) {
        errno = EINVAL;
        return -1;
    }
    return poller->poller.modify (
      static_cast<zlink::socket_base_t *> (poller->registrations[index].socket),
      events_);
}

int zlink_poller_remove_spot_sub (void *poller_, void *spot_or_node_)
{
    poller_handle_t *poller = as_poller_handle (poller_);
    if (!poller)
        return -1;
    const int index = poller_find_registration_index (
      poller, spot_or_node_, poller_spot_sub_kind_for_subject (spot_or_node_));
    if (index < 0) {
        errno = EINVAL;
        return -1;
    }
    zlink::socket_base_t *socket =
      static_cast<zlink::socket_base_t *> (poller->registrations[index].socket);
    const int rc = poller->poller.remove (socket);
    if (rc == 0) {
        release_poller_registration (poller->registrations[index]);
        poller->registrations.erase (poller->registrations.begin () + index);
    }
    return rc;
}

int zlink_poller_add_monitor (void *poller_,
                              void *monitor_,
                              void *user_data_,
                              short events_)
{
    poller_handle_t *poller = as_poller_handle (poller_);
    if (!poller)
        return -1;
    if (validate_monitor_poller_events (events_) != 0)
        return -1;
    socket_handle_t handle = as_socket_handle (monitor_);
    if (!handle.socket)
        return -1;
    return poller->poller.add (handle.socket, user_data_, events_);
}

int zlink_poller_modify_monitor (void *poller_, void *monitor_, short events_)
{
    poller_handle_t *poller = as_poller_handle (poller_);
    if (!poller)
        return -1;
    if (validate_monitor_poller_events (events_) != 0)
        return -1;
    socket_handle_t handle = as_socket_handle (monitor_);
    if (!handle.socket)
        return -1;
    return poller->poller.modify (handle.socket, events_);
}

int zlink_poller_remove_monitor (void *poller_, void *monitor_)
{
    poller_handle_t *poller = as_poller_handle (poller_);
    if (!poller)
        return -1;
    socket_handle_t handle = as_socket_handle (monitor_);
    if (!handle.socket)
        return -1;
    return poller->poller.remove (handle.socket);
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

int zlink_send (void *s_,
                const void *buf_,
                size_t len_,
                zlink_send_flags_t flags_)
{
    socket_handle_t handle = as_socket_handle (s_);
    if (!handle.socket)
        return -1;
    zlink_msg_t msg;
    int rc = zlink_msg_init_buffer (&msg, buf_, len_);
    if (unlikely (rc < 0))
        return -1;

    rc = s_sendmsg (handle, &msg, flags_);
    if (unlikely (rc < 0)) {
        const int err = errno;
        zlink_msg_close (&msg);
        errno = err;
        return -1;
    }
    return rc;
}

// Receiving functions.

int zlink_msg_recv (zlink_msg_t *msg_, void *s_, zlink_send_flags_t flags_)
{
    socket_handle_t handle = as_socket_handle (s_);
    if (!handle.socket)
        return -1;
    return zlink::recv_msg_internal (handle.socket, msg_, flags_);
}

int zlink_recv (void *s_, void *buf_, size_t len_, zlink_send_flags_t flags_)
{
    socket_handle_t handle = as_socket_handle (s_);
    if (!handle.socket)
        return -1;
    return zlink::recv_buffer_internal (handle.socket, buf_, len_, flags_);
}

int zlink_stream_send (void *s_,
                       const zlink_routing_id_t *rid_,
                       const void *data_,
                       size_t size_,
                       zlink_send_flags_t flags_)
{
    socket_handle_t handle = as_socket_handle (s_);
    if (!handle.socket)
        return -1;

    if (!is_stream_type (handle) || !rid_ || rid_->size == 0
        || rid_->size > sizeof (rid_->data)) {
        errno = EINVAL;
        return -1;
    }

    if (!data_ && size_ > 0) {
        errno = EINVAL;
        return -1;
    }

    // STREAM routing ids are 32-bit internally.
    if (rid_->size != 4) {
        errno = EINVAL;
        return -1;
    }

    const uint32_t routing_id =
      (static_cast<uint32_t> (rid_->data[0]) << 24)
      | (static_cast<uint32_t> (rid_->data[1]) << 16)
      | (static_cast<uint32_t> (rid_->data[2]) << 8)
      | static_cast<uint32_t> (rid_->data[3]);

    stream_api_lock_t api_lock (handle);

    zlink_msg_t msg;
    if (zlink_msg_init_size (&msg, size_) != 0)
        return -1;

    unsigned char *dst = static_cast<unsigned char *> (zlink_msg_data (&msg));
    if (size_ > 0)
        memcpy (dst, data_, size_);

    zlink::msg_t *core_msg = reinterpret_cast<zlink::msg_t *> (&msg);
    if (core_msg->set_routing_id (routing_id) != 0) {
        const int err = errno;
        (void) zlink_msg_close (&msg);
        errno = err;
        return -1;
    }

    const int base_flags = flags_ & ZLINK_DONTWAIT;
    const int send_rc = s_sendmsg (handle, &msg, base_flags);
    if (send_rc < 0) {
        const int err = errno;
        (void) zlink_msg_close (&msg);
        errno = err;
        return -1;
    }

    (void) zlink_msg_close (&msg);
    errno = 0;
    return static_cast<int> (size_ < static_cast<size_t> (INT_MAX) ? size_
                                                                    : INT_MAX);
}

int zlink_stream_send_msg (void *s_,
                           const zlink_routing_id_t *rid_,
                           zlink_msg_t *msg_,
                           zlink_send_flags_t flags_)
{
    socket_handle_t handle = as_socket_handle (s_);
    if (!handle.socket)
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

    if (!is_stream_type (handle)) {
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
    stream_api_lock_t api_lock (handle);
    if (core_msg->set_routing_id (routing_id) != 0) {
        const int err = errno;
        release_stream_send_msg (core_msg);
        errno = err;
        return -1;
    }

    const int base_flags = flags_ & ZLINK_DONTWAIT;
    const int send_rc = s_sendmsg (handle, msg_, base_flags);
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

int zlink_msg_send (zlink_msg_t *msg_,
                    void *s_,
                    zlink_send_flags_t flags_)
{
    socket_handle_t handle = as_socket_handle (s_);
    if (!handle.socket)
        return -1;
    return s_sendmsg (handle, msg_, flags_);
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

int zlink_msg_more (const zlink_msg_t *msg_)
{
    return (((zlink::msg_t *) msg_)->flags () & zlink::msg_t::more) ? 1 : 0;
}

int zlink_msg_get (const zlink_msg_t *msg_, int property_)
{
    switch (property_) {
        case ZLINK_MORE:
            return (((zlink::msg_t *) msg_)->flags () & zlink::msg_t::more) ? 1 : 0;
        case ZLINK_SHARED:
            return (((zlink::msg_t *) msg_)->is_cmsg ())
                       || (((zlink::msg_t *) msg_)->flags () & zlink::msg_t::shared)
                     ? 1
                     : 0;
        default:
            errno = EINVAL;
            return -1;
    }
}

int zlink_msg_set (zlink_msg_t *, int, int)
{
    errno = EINVAL;
    return -1;
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
