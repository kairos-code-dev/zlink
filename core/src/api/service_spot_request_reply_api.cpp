/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "api/request_timeout_scheduler_internal.hpp"
#include "api/request_reply_protocol_internal.hpp"
#include "api/service_api_internal.hpp"
#include "api/socket_api_internal.hpp"
#include "core/multipart_send_txn.hpp"
#include "core/recv_internal.hpp"
#include "core/recv_tls_view.hpp"
#include "services/spot/spot_data_plane_internal.hpp"
#include "services/spot/spot_node_access.hpp"
#include "services/spot/spot_runtime.hpp"
#include "services/spot/spot_pub.hpp"
#include "utils/random.hpp"

namespace
{
enum : uint8_t
{
    zmp_spot_routed_protocol_id = 0x02,
    zmp_protocol_version = 0x01,
    zmp_spot_class = 0x01,
    zmp_router_class = 0x02
};

const size_t spot_routed_control_part_count = 8;

struct routing_pair_t
{
    std::string node_rid;
    std::string spot_rid;
};

struct pending_spot_key_t
{
    uint8_t source_class;
    std::string source_rid;
    std::string source_spot_rid;
    uint64_t request_seq;

    bool operator< (const pending_spot_key_t &other_) const
    {
        if (request_seq != other_.request_seq)
            return request_seq < other_.request_seq;
        if (source_class != other_.source_class)
            return source_class < other_.source_class;
        if (source_rid != other_.source_rid)
            return source_rid < other_.source_rid;
        return source_spot_rid < other_.source_spot_rid;
    }
};

struct pending_reply_t
{
    pending_spot_key_t key;
    zlink_reply_handler_fn handler;
    void *userdata;
    std::shared_ptr<zlink::request_timeout::task_t> timeout_task;
};

struct internal_pair_queue_t
{
    internal_pair_queue_t () : rx (NULL), tx (NULL) {}

    zlink::socket_base_t *rx;
    zlink::socket_base_t *tx;
    std::string endpoint;
};

struct parsed_spot_envelope_t
{
    uint8_t source_class;
    std::string source_node_rid;
    std::string source_endpoint_rid;
    uint8_t destination_class;
    std::string destination_node_rid;
    std::string destination_endpoint_rid;
    zlink_msg_t *payload_parts;
    size_t payload_part_count;
};

struct local_spot_request_reply_state_t
{
    explicit local_spot_request_reply_state_t (void *owner_) :
        owner (owner_),
        default_timeout_ms (zlink::request_reply::default_timeout_ms),
        next_request_seq (1),
        request_handler (NULL),
        request_handler_userdata (NULL),
        dispatch_event_handler (NULL),
        dispatch_event_handler_userdata (NULL),
        route_ingress_tx (NULL)
    {
    }

    void *owner;
    std::mutex mutex;
    uint32_t default_timeout_ms;
    uint64_t next_request_seq;
    std::set<uint64_t> pending_sequences;
    std::map<pending_spot_key_t, pending_reply_t> pending_replies;
    internal_pair_queue_t recv_queue;
    zlink_spot_handler_fn request_handler;
    void *request_handler_userdata;
    zlink_spot_dispatch_event_handler_fn dispatch_event_handler;
    void *dispatch_event_handler_userdata;
    zlink::socket_base_t *route_ingress_tx;
    std::string route_ingress_endpoint;
};

struct local_router_spot_state_t
{
    explicit local_router_spot_state_t (void *owner_) :
        owner (owner_),
        default_timeout_ms (zlink::request_reply::default_timeout_ms),
        next_request_seq (1),
        handler (NULL),
        handler_userdata (NULL)
    {
    }

    void *owner;
    std::string router_rid;
    std::mutex mutex;
    uint32_t default_timeout_ms;
    uint64_t next_request_seq;
    std::set<uint64_t> pending_sequences;
    std::map<uint64_t, pending_reply_t> pending_replies;
    internal_pair_queue_t recv_queue;
    zlink_router_spot_handler_fn handler;
    void *handler_userdata;
};

typedef std::map<void *, std::shared_ptr<local_spot_request_reply_state_t> >
  local_spot_state_map_t;
typedef std::map<void *, std::shared_ptr<local_router_spot_state_t> >
  local_router_state_map_t;

std::mutex g_local_spot_states_mutex;
local_spot_state_map_t g_local_spot_states;
local_router_state_map_t g_local_router_states;
thread_local zlink_routing_id_t g_spot_recv_source_rid;
thread_local zlink_routing_id_t g_spot_recv_spot_rid;

int enqueue_runtime_route_ingress_once (zlink::spot_runtime_t *runtime_,
                                        std::vector<zlink_msg_t> *parts_);

void close_internal_pair_queue (internal_pair_queue_t *queue_)
{
    if (!queue_)
        return;

    if (queue_->tx) {
        queue_->tx->stop ();
        queue_->tx->close ();
        queue_->tx = NULL;
    }
    if (queue_->rx) {
        queue_->rx->stop ();
        queue_->rx->close ();
        queue_->rx = NULL;
    }
    queue_->endpoint.clear ();
}

void set_internal_pair_socket_defaults (zlink::socket_base_t *socket_)
{
    if (!socket_)
        return;

    const int linger = 0;
    socket_->setsockopt (ZLINK_INTERNAL_OPT_LINGER, &linger, sizeof (linger));
}

bool recv_internal_pair_handshake (zlink::socket_base_t *socket_,
                                   long timeout_ms_)
{
    if (!socket_)
        return false;

    const int timeout = static_cast<int> (timeout_ms_);
    if (socket_->setsockopt (ZLINK_INTERNAL_OPT_RCVTIMEO, &timeout,
                             sizeof (timeout))
        != 0)
        return false;

    zlink::msg_t msg;
    if (msg.init () != 0)
        return false;

    const int rc = socket_->recv (&msg, 0);
    msg.close ();

    const int blocking = -1;
    (void) socket_->setsockopt (ZLINK_INTERNAL_OPT_RCVTIMEO, &blocking,
                                sizeof (blocking));
    return rc == 0;
}

bool handshake_internal_pair (zlink::socket_base_t *rx_,
                              zlink::socket_base_t *tx_)
{
    if (!rx_ || !tx_)
        return false;

    const unsigned char hello = 0x11;
    const unsigned char ack = 0x22;

    zlink::msg_t msg;
    if (msg.init_size (sizeof (hello)) != 0)
        return false;
    memcpy (msg.data (), &hello, sizeof (hello));
    if (tx_->send (&msg, 0) != 0) {
        msg.close ();
        return false;
    }
    msg.close ();

    if (!recv_internal_pair_handshake (rx_, 100))
        return false;

    if (msg.init_size (sizeof (ack)) != 0)
        return false;
    memcpy (msg.data (), &ack, sizeof (ack));
    if (rx_->send (&msg, 0) != 0) {
        msg.close ();
        return false;
    }
    msg.close ();

    if (!recv_internal_pair_handshake (tx_, 100))
        return false;

    return true;
}

int ensure_internal_pair_queue (zlink::ctx_t *ctx_,
                                const char *prefix_,
                                internal_pair_queue_t *queue_)
{
    if (!ctx_ || !prefix_ || !queue_) {
        errno = EFAULT;
        return -1;
    }
    if (queue_->rx && queue_->tx)
        return 0;

    char endpoint[128];
    snprintf (endpoint, sizeof (endpoint), "inproc://%s-%p-%u", prefix_,
              static_cast<void *> (queue_), zlink::generate_random ());

    zlink::socket_base_t *rx = ctx_->create_socket (ZLINK_CORE_SOCKET_PAIR);
    zlink::socket_base_t *tx = ctx_->create_socket (ZLINK_CORE_SOCKET_PAIR);
    if (!rx || !tx) {
        if (tx) {
            tx->stop ();
            tx->close ();
        }
        if (rx) {
            rx->stop ();
            rx->close ();
        }
        return -1;
    }

    set_internal_pair_socket_defaults (rx);
    set_internal_pair_socket_defaults (tx);

    if (rx->bind (endpoint) != 0 || tx->connect (endpoint) != 0) {
        const int saved_errno = errno;
        tx->stop ();
        tx->close ();
        rx->stop ();
        rx->close ();
        errno = saved_errno;
        return -1;
    }

    if (!handshake_internal_pair (rx, tx)) {
        const int saved_errno = errno != 0 ? errno : EPROTO;
        tx->stop ();
        tx->close ();
        rx->stop ();
        rx->close ();
        errno = saved_errno;
        return -1;
    }

    queue_->rx = rx;
    queue_->tx = tx;
    queue_->endpoint = endpoint;
    errno = 0;
    return 0;
}

int validate_request_parts (zlink_msg_t *parts_, size_t part_count_)
{
    if ((!parts_ && part_count_ > 0) || part_count_ == 0) {
        errno = EFAULT;
        return -1;
    }

    return 0;
}

bool frame_has_more_local (const zlink_msg_t &msg_)
{
    return (reinterpret_cast<const zlink::msg_t *> (&msg_)->flags ()
            & zlink::msg_t::more)
           != 0;
}

int export_followup_sequence_from_reserved_first_local (
  zlink::socket_base_t *socket_,
  zlink_msg_t **parts_out_,
  size_t *part_count_out_)
{
    if (!socket_ || !parts_out_ || !part_count_out_) {
        errno = EFAULT;
        return -1;
    }

    while (true) {
        zlink::recv_tls_view::storage_t &tls = zlink::recv_tls_view::storage ();
        const bool more = frame_has_more_local (tls.parts[tls.count - 1]);
        if (!more)
            return zlink::recv_tls_view::commit (parts_out_, part_count_out_);

        zlink_msg_t next;
        zlink_msg_init (&next);
        if (zlink::recv_followup_msg_socket (socket_, &next) < 0) {
            zlink_msg_close (&next);
            zlink::recv_tls_view::abort ();
            return -1;
        }

        if (zlink::recv_tls_view::push (&next) != 0) {
            const int saved_errno = errno;
            zlink_msg_close (&next);
            zlink::recv_tls_view::abort ();
            errno = saved_errno;
            return -1;
        }
    }
}

int init_buffer_frame_local (zlink_msg_t *msg_,
                             const void *data_,
                             size_t size_)
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

int send_buffer_frame_local (zlink::socket_base_t *socket_,
                             const void *data_,
                             size_t size_,
                             int flags_)
{
    if (!socket_) {
        errno = EFAULT;
        return -1;
    }

    zlink::msg_t msg;
    if (msg.init_size (size_) != 0)
        return -1;
    if (size_ > 0 && data_)
        memcpy (msg.data (), data_, size_);
    const int rc = socket_->send (&msg, flags_);
    const int saved_errno = errno;
    msg.close ();
    errno = saved_errno;
    return rc;
}

int recv_followup_with_retry_local (zlink::socket_base_t *socket_,
                                    zlink_msg_t *msg_,
                                    int flags_)
{
    if (!socket_ || !msg_) {
        errno = EFAULT;
        return -1;
    }

    while (socket_->recv (reinterpret_cast<zlink::msg_t *> (msg_), flags_) != 0) {
        const int saved_errno = errno;
        if ((flags_ & ZLINK_DONTWAIT) != 0 || saved_errno != EAGAIN) {
            errno = saved_errno;
            return -1;
        }
        if (zlink::wait_socket_events_internal (socket_, ZLINK_POLLIN, -1)
            <= 0) {
            errno = saved_errno;
            return -1;
        }
    }
    return 0;
}

zlink::ctx_t *resolve_spot_ctx (void *spot_)
{
    spot_handle_t *spot = as_spot_handle (spot_);
    if (!spot || !spot->node) {
        errno = EFAULT;
        return NULL;
    }
    return zlink::spot_node_access_t::ctx (spot->node);
}

zlink::spot_runtime_t *resolve_spot_runtime (void *spot_)
{
    spot_handle_t *spot = as_spot_handle (spot_);
    if (!spot || !spot->node) {
        errno = EFAULT;
        return NULL;
    }
    return zlink::spot_node_access_t::runtime (spot->node);
}

zlink::spot_runtime_t *resolve_active_spot_runtime (void *spot_)
{
    zlink::spot_runtime_t *runtime = resolve_spot_runtime (spot_);
    if (!runtime || !runtime->data_plane_running || !runtime->route_ingress
        || !runtime->node_router)
        return NULL;
    return runtime;
}

bool has_valid_routing_id (const zlink_routing_id_t *peer_rid_)
{
    return peer_rid_ && peer_rid_->size > 0
           && peer_rid_->size <= sizeof (peer_rid_->data);
}

std::shared_ptr<local_spot_request_reply_state_t> try_find_spot_state (void *spot_)
{
    std::lock_guard<std::mutex> lock (g_local_spot_states_mutex);
    local_spot_state_map_t::iterator it = g_local_spot_states.find (spot_);
    if (it == g_local_spot_states.end ())
        return std::shared_ptr<local_spot_request_reply_state_t> ();
    return it->second;
}

void maybe_dispatch_spot_event (local_spot_request_reply_state_t *state_,
                                zlink_spot_dispatch_event_t event_)
{
    zlink_spot_dispatch_event_handler_fn handler = NULL;
    void *userdata = NULL;
    void *owner = NULL;
    {
        std::lock_guard<std::mutex> lock (state_->mutex);
        handler = state_->dispatch_event_handler;
        userdata = state_->dispatch_event_handler_userdata;
        owner = state_->owner;
    }

    if (handler)
        handler (owner, event_, userdata);
}

void notify_spot_dispatch_event (void *spot_,
                                 zlink_spot_dispatch_event_t event_)
{
    std::shared_ptr<local_spot_request_reply_state_t> state =
      try_find_spot_state (spot_);
    if (!state)
        return;
    maybe_dispatch_spot_event (state.get (), event_);
}

extern "C" void zlink_spot_notify_dispatch_event (
  void *spot_,
  zlink_spot_dispatch_event_t event_)
{
    notify_spot_dispatch_event (spot_, event_);
}

int queue_spot_message (
  local_spot_request_reply_state_t *state_,
  const zlink_routing_id_t *source_rid_,
  const zlink_routing_id_t *spot_rid_,
  uint64_t request_seq_,
  zlink_msg_t *parts_,
  size_t part_count_)
{
    if (!state_ || !parts_) {
        errno = EFAULT;
        return -1;
    }

    if (ensure_internal_pair_queue (resolve_spot_ctx (state_->owner),
                                    "zlink.spot.routed.recv",
                                    &state_->recv_queue)
        != 0)
        return -1;

    unsigned char seq_buf[8];
    zlink::request_reply::encode_u64_be (request_seq_, seq_buf);
    const void *source_data =
      has_valid_routing_id (source_rid_) ? source_rid_->data : NULL;
    const size_t source_size =
      has_valid_routing_id (source_rid_) ? source_rid_->size : 0;
    const void *spot_data =
      has_valid_routing_id (spot_rid_) ? spot_rid_->data : NULL;
    const size_t spot_size = has_valid_routing_id (spot_rid_) ? spot_rid_->size
                                                              : 0;
    if (send_buffer_frame_local (state_->recv_queue.tx, source_data, source_size,
                                 ZLINK_SNDMORE)
        != 0
        || send_buffer_frame_local (state_->recv_queue.tx, spot_data, spot_size,
                                    ZLINK_SNDMORE)
             != 0
        || send_buffer_frame_local (state_->recv_queue.tx, seq_buf,
                                    sizeof (seq_buf), ZLINK_SNDMORE)
             != 0) {
        zlink::request_reply::consume_send_frames_from (parts_, 0, part_count_);
        return -1;
    }
    for (size_t i = 0; i < part_count_; ++i) {
        const int flags = (i + 1 < part_count_) ? ZLINK_SNDMORE : 0;
        if (state_->recv_queue.tx->send (
              reinterpret_cast<zlink::msg_t *> (&parts_[i]), flags)
            != 0) {
            zlink::request_reply::consume_send_frames_from (parts_, i,
                                                            part_count_);
            return -1;
        }
    }

    maybe_dispatch_spot_event (state_, ZLINK_SPOT_DISPATCH_EVENT_ROUTED_READABLE);
    return 0;
}

int recv_internal_spot_queue (internal_pair_queue_t *queue_,
                              const zlink_routing_id_t **source_rid_out_,
                              const zlink_routing_id_t **spot_rid_out_,
                              uint64_t *request_seq_out_,
                              zlink_msg_t **parts_out_,
                              size_t *part_count_out_,
                              int flags_)
{
    if (!queue_ || !queue_->rx || !source_rid_out_ || !spot_rid_out_
        || !request_seq_out_ || !parts_out_ || !part_count_out_) {
        errno = EFAULT;
        return -1;
    }

    zlink_msg_t source_frame;
    zlink_msg_t spot_frame;
    zlink_msg_t seq_frame;
    zlink_msg_t *first_payload = NULL;
    zlink_msg_init (&source_frame);
    zlink_msg_init (&spot_frame);
    zlink_msg_init (&seq_frame);

    if (zlink::recv_tls_view::begin_with_first_slot (
          parts_out_, part_count_out_, &first_payload)
        != 0)
        return -1;

    while (queue_->rx->recv (reinterpret_cast<zlink::msg_t *> (&source_frame),
                             flags_)
           != 0) {
        const int saved_errno = errno;
        zlink_msg_close (&source_frame);
        if ((flags_ & ZLINK_DONTWAIT) != 0 || saved_errno != EAGAIN) {
            zlink::recv_tls_view::abort ();
            errno = saved_errno;
            return -1;
        }
        if (zlink::wait_socket_events_internal (queue_->rx, ZLINK_POLLIN, -1)
            <= 0) {
            zlink::recv_tls_view::abort ();
            errno = saved_errno;
            return -1;
        }
        zlink_msg_init (&source_frame);
    }
    if (recv_followup_with_retry_local (queue_->rx, &spot_frame, flags_) != 0) {
        const int saved_errno = errno;
        zlink_msg_close (&source_frame);
        zlink_msg_close (&spot_frame);
        zlink_msg_close (&seq_frame);
        zlink::recv_tls_view::abort ();
        errno = saved_errno;
        return -1;
    }
    if (recv_followup_with_retry_local (queue_->rx, &seq_frame, flags_) != 0) {
        const int saved_errno = errno;
        zlink_msg_close (&source_frame);
        zlink_msg_close (&spot_frame);
        zlink_msg_close (&seq_frame);
        zlink::recv_tls_view::abort ();
        errno = saved_errno;
        return -1;
    }
    if (zlink_msg_size (&seq_frame) != 8) {
        zlink_msg_close (&source_frame);
        zlink_msg_close (&spot_frame);
        zlink_msg_close (&seq_frame);
        zlink::recv_tls_view::abort ();
        errno = EPROTO;
        return -1;
    }
    if (recv_followup_with_retry_local (queue_->rx, first_payload, flags_) != 0) {
        const int saved_errno = errno;
        zlink_msg_close (&source_frame);
        zlink_msg_close (&spot_frame);
        zlink_msg_close (&seq_frame);
        zlink::recv_tls_view::abort ();
        errno = saved_errno;
        return -1;
    }

    g_spot_recv_source_rid.size =
      static_cast<uint8_t> (std::min (zlink_msg_size (&source_frame),
                                      sizeof (g_spot_recv_source_rid.data)));
    if (g_spot_recv_source_rid.size > 0) {
        memcpy (g_spot_recv_source_rid.data, zlink_msg_data (&source_frame),
                g_spot_recv_source_rid.size);
    }
    g_spot_recv_spot_rid.size =
      static_cast<uint8_t> (std::min (zlink_msg_size (&spot_frame),
                                      sizeof (g_spot_recv_spot_rid.data)));
    if (g_spot_recv_spot_rid.size > 0) {
        memcpy (g_spot_recv_spot_rid.data, zlink_msg_data (&spot_frame),
                g_spot_recv_spot_rid.size);
    }
    *source_rid_out_ = &g_spot_recv_source_rid;
    *spot_rid_out_ = g_spot_recv_spot_rid.size > 0 ? &g_spot_recv_spot_rid
                                                    : NULL;
    *request_seq_out_ = zlink::request_reply::decode_u64_be (
      static_cast<const unsigned char *> (zlink_msg_data (&seq_frame)));
    zlink_msg_close (&source_frame);
    zlink_msg_close (&spot_frame);
    zlink_msg_close (&seq_frame);

    if (!frame_has_more_local (*first_payload))
        return zlink::recv_tls_view::commit_reserved_single (parts_out_,
                                                             part_count_out_);
    return export_followup_sequence_from_reserved_first_local (
      queue_->rx, parts_out_, part_count_out_);
}

int dispatch_spot_message (local_spot_request_reply_state_t *state_,
                           const zlink_routing_id_t *source_rid_,
                           const zlink_routing_id_t *spot_rid_,
                           uint64_t request_seq_,
                           zlink_msg_t *parts_,
                           size_t part_count_)
{
    zlink_spot_handler_fn handler = NULL;
    void *handler_userdata = NULL;
    {
        std::lock_guard<std::mutex> lock (state_->mutex);
        handler = state_->request_handler;
        handler_userdata = state_->request_handler_userdata;
    }

    if (handler) {
        handler (source_rid_, spot_rid_, request_seq_, parts_, part_count_,
                 handler_userdata);
        return 0;
    }

    if (queue_spot_message (state_, source_rid_, spot_rid_, request_seq_,
                            parts_, part_count_)
        != 0) {
        zlink::request_reply::close_request_reply_parts (parts_, part_count_);
        return -1;
    }
    return 0;
}

int dispatch_router_spot_message (local_router_spot_state_t *state_,
                                  const zlink_routing_id_t *source_node_rid_,
                                  const zlink_routing_id_t *source_spot_rid_,
                                  uint64_t request_seq_,
                                  zlink_msg_t *parts_,
                                  size_t part_count_)
{
    zlink_router_spot_handler_fn handler = NULL;
    void *handler_userdata = NULL;
    {
        std::lock_guard<std::mutex> lock (state_->mutex);
        handler = state_->handler;
        handler_userdata = state_->handler_userdata;
    }

    if (handler) {
        handler (source_node_rid_, source_spot_rid_, request_seq_, parts_,
                 part_count_, handler_userdata);
        return 0;
    }

    if (ensure_internal_pair_queue (
          state_->owner ? as_socket_handle (state_->owner).socket->get_ctx ()
                        : NULL,
          "zlink.router.spot.recv", &state_->recv_queue)
        != 0) {
        zlink::request_reply::close_request_reply_parts (parts_, part_count_);
        return -1;
    }

    unsigned char seq_buf[8];
    zlink::request_reply::encode_u64_be (request_seq_, seq_buf);
    const void *source_data =
      has_valid_routing_id (source_node_rid_) ? source_node_rid_->data : NULL;
    const size_t source_size =
      has_valid_routing_id (source_node_rid_) ? source_node_rid_->size : 0;
    const void *spot_data =
      has_valid_routing_id (source_spot_rid_) ? source_spot_rid_->data : NULL;
    const size_t spot_size =
      has_valid_routing_id (source_spot_rid_) ? source_spot_rid_->size : 0;
    if (send_buffer_frame_local (state_->recv_queue.tx, source_data, source_size,
                                 ZLINK_SNDMORE)
        != 0
        || send_buffer_frame_local (state_->recv_queue.tx, spot_data, spot_size,
                                    ZLINK_SNDMORE)
             != 0
        || send_buffer_frame_local (state_->recv_queue.tx, seq_buf,
                                    sizeof (seq_buf), ZLINK_SNDMORE)
             != 0) {
        zlink::request_reply::close_request_reply_parts (parts_, part_count_);
        return -1;
    }
    for (size_t i = 0; i < part_count_; ++i) {
        const int flags = (i + 1 < part_count_) ? ZLINK_SNDMORE : 0;
        if (state_->recv_queue.tx->send (
              reinterpret_cast<zlink::msg_t *> (&parts_[i]), flags)
            != 0) {
            zlink::request_reply::consume_send_frames_from (parts_, i,
                                                            part_count_);
            return -1;
        }
    }
    return 0;
}

std::string routing_id_key (const zlink_routing_id_t *peer_rid_)
{
    if (!has_valid_routing_id (peer_rid_))
        return std::string ();

    return std::string (reinterpret_cast<const char *> (peer_rid_->data),
                        peer_rid_->size);
}

void routing_id_from_string (const std::string &value_, zlink_routing_id_t *out_)
{
    if (!out_)
        return;

    memset (out_, 0, sizeof (*out_));
    if (value_.empty ())
        return;

    const size_t size =
      value_.size () > sizeof (out_->data) ? sizeof (out_->data) : value_.size ();
    memcpy (out_->data, value_.data (), size);
    out_->size = static_cast<uint8_t> (size);
}

bool parse_spot_routed_envelope (zlink_msg_t *parts_,
                                 size_t part_count_,
                                 parsed_spot_envelope_t *out_)
{
    if (!parts_ || !out_ || part_count_ < spot_routed_control_part_count)
        return false;

    zlink::msg_t *protocol_id =
      reinterpret_cast<zlink::msg_t *> (&parts_[0]);
    if (!protocol_id->check ()
        || (protocol_id->flags () & zlink::msg_t::command) == 0
        || !zlink::request_reply::frame_is_single_byte_value (
          &parts_[0], zmp_spot_routed_protocol_id)
        || !zlink::request_reply::frame_is_single_byte_value (
          &parts_[1], zmp_protocol_version)) {
        return false;
    }

    if (!zlink::request_reply::frame_is_single_byte_value (&parts_[2],
                                                           zmp_spot_class)
        && !zlink::request_reply::frame_is_single_byte_value (&parts_[2],
                                                              zmp_router_class)) {
        return false;
    }
    if (!zlink::request_reply::frame_is_single_byte_value (&parts_[5],
                                                           zmp_spot_class)
        && !zlink::request_reply::frame_is_single_byte_value (&parts_[5],
                                                              zmp_router_class)) {
        return false;
    }

    out_->source_class =
      static_cast<const unsigned char *> (zlink_msg_data (&parts_[2]))[0];
    out_->source_node_rid.assign (
      static_cast<const char *> (zlink_msg_data (&parts_[3])),
      zlink_msg_size (&parts_[3]));
    out_->source_endpoint_rid.assign (
      static_cast<const char *> (zlink_msg_data (&parts_[4])),
      zlink_msg_size (&parts_[4]));
    out_->destination_class =
      static_cast<const unsigned char *> (zlink_msg_data (&parts_[5]))[0];
    out_->destination_node_rid.assign (
      static_cast<const char *> (zlink_msg_data (&parts_[6])),
      zlink_msg_size (&parts_[6]));
    out_->destination_endpoint_rid.assign (
      static_cast<const char *> (zlink_msg_data (&parts_[7])),
      zlink_msg_size (&parts_[7]));
    out_->payload_parts = parts_ + spot_routed_control_part_count;
    out_->payload_part_count = part_count_ - spot_routed_control_part_count;
    return true;
}

bool resolve_spot_identity (void *spot_, routing_pair_t *out_)
{
    if (!out_) {
        errno = EFAULT;
        return false;
    }

    if (spot_handle_t *spot = as_spot_handle (spot_)) {
        zlink::service_public_api_scope_t admission (spot->public_api);
        if (!admission.acquired ())
            return false;

        zlink::spot_pub_t *spot_pub = ensure_spot_pub (spot);
        zlink::spot_pub_t *node_pub =
          spot->node ? spot->node->ensure_default_pub () : NULL;
        if (!spot_pub || !node_pub)
            return false;

        zlink_routing_id_t node_rid;
        zlink_routing_id_t spot_rid;
        memset (&node_rid, 0, sizeof (node_rid));
        memset (&spot_rid, 0, sizeof (spot_rid));
        if (node_pub->routing_id (&node_rid) != 0
            || spot_pub->routing_id (&spot_rid) != 0) {
            return false;
        }

        out_->node_rid = routing_id_key (&node_rid);
        out_->spot_rid = routing_id_key (&spot_rid);
        return !out_->node_rid.empty () && !out_->spot_rid.empty ();
    }

    errno = EFAULT;
    return false;
}

std::shared_ptr<local_spot_request_reply_state_t>
find_or_create_spot_state (void *spot_)
{
    std::lock_guard<std::mutex> lock (g_local_spot_states_mutex);
    local_spot_state_map_t::iterator it = g_local_spot_states.find (spot_);
    if (it != g_local_spot_states.end ())
        return it->second;

    std::shared_ptr<local_spot_request_reply_state_t> state (
      new local_spot_request_reply_state_t (spot_));
    g_local_spot_states[spot_] = state;
    return state;
}

std::shared_ptr<local_router_spot_state_t>
find_or_create_router_state (void *router_)
{
    std::lock_guard<std::mutex> lock (g_local_spot_states_mutex);
    local_router_state_map_t::iterator it = g_local_router_states.find (router_);
    if (it != g_local_router_states.end ())
        return it->second;

    std::shared_ptr<local_router_spot_state_t> state (
      new local_router_spot_state_t (router_));
    g_local_router_states[router_] = state;
    return state;
}

std::shared_ptr<local_spot_request_reply_state_t>
find_spot_state_by_identity (const std::string &node_rid_,
                             const std::string &spot_rid_)
{
    std::lock_guard<std::mutex> lock (g_local_spot_states_mutex);
    for (local_spot_state_map_t::const_iterator it = g_local_spot_states.begin ();
         it != g_local_spot_states.end (); ++it) {
        routing_pair_t current;
        if (!resolve_spot_identity (it->first, &current))
            continue;
        if (current.node_rid == node_rid_ && current.spot_rid == spot_rid_)
            return it->second;
    }
    return std::shared_ptr<local_spot_request_reply_state_t> ();
}

std::shared_ptr<local_router_spot_state_t>
find_router_state_by_rid (const std::string &router_rid_)
{
    std::lock_guard<std::mutex> lock (g_local_spot_states_mutex);
    for (local_router_state_map_t::const_iterator it =
           g_local_router_states.begin ();
         it != g_local_router_states.end (); ++it) {
        if (it->second && it->second->router_rid == router_rid_)
            return it->second;
    }
    return std::shared_ptr<local_router_spot_state_t> ();
}

zlink::spot_runtime_t *resolve_runtime_for_spot_destination (
  const std::string &node_rid_,
  const std::string &spot_rid_)
{
    std::shared_ptr<local_spot_request_reply_state_t> state =
      find_spot_state_by_identity (node_rid_, spot_rid_);
    if (!state)
        return NULL;
    return resolve_active_spot_runtime (state->owner);
}

int send_combined_parts_locked (zlink::socket_base_t *socket_,
                                std::vector<zlink_msg_t> *parts_)
{
    if (!socket_ || !parts_ || parts_->empty ()) {
        errno = EFAULT;
        return -1;
    }

    zlink::spot_data_plane_forwarder_t::pump_socket_commands (socket_);
    socket_->set_all_pipes_nodelay ();
    return zlink::logical_multipart_send (socket_, &(*parts_)[0], parts_->size (),
                                          0);
}

int ensure_dealer_sender (zlink::ctx_t *ctx_,
                          const std::string &endpoint_,
                          zlink::socket_base_t **socket_inout_,
                          std::string *bound_endpoint_inout_)
{
    if (!ctx_ || !socket_inout_ || !bound_endpoint_inout_
        || endpoint_.empty ()) {
        errno = EFAULT;
        return -1;
    }

    if (*socket_inout_ && *bound_endpoint_inout_ == endpoint_)
        return 0;

    if (*socket_inout_) {
        (*socket_inout_)->stop ();
        (*socket_inout_)->close ();
        *socket_inout_ = NULL;
        bound_endpoint_inout_->clear ();
    }

    zlink::socket_base_t *socket =
      ctx_->create_socket (ZLINK_CORE_SOCKET_DEALER);
    if (!socket)
        return -1;

    const int linger = 0;
    socket->setsockopt (ZLINK_INTERNAL_OPT_LINGER, &linger, sizeof (linger));
    if (socket->connect (endpoint_.c_str ()) != 0) {
        const int saved_errno = errno;
        socket->stop ();
        socket->close ();
        errno = saved_errno;
        return -1;
    }

    *socket_inout_ = socket;
    *bound_endpoint_inout_ = endpoint_;
    errno = 0;
    return 0;
}

int enqueue_spot_state_route_ingress (
  local_spot_request_reply_state_t *state_,
  zlink::spot_runtime_t *runtime_,
  std::vector<zlink_msg_t> *parts_)
{
    if (!state_ || !runtime_ || !parts_) {
        errno = EFAULT;
        return -1;
    }
    return enqueue_runtime_route_ingress_once (runtime_, parts_);
}

int enqueue_runtime_route_ingress_once (zlink::spot_runtime_t *runtime_,
                                        std::vector<zlink_msg_t> *parts_)
{
    if (!runtime_ || !parts_) {
        errno = EFAULT;
        return -1;
    }

    zlink::ctx_t *ctx = zlink::spot_node_access_t::ctx (runtime_->owner);
    if (!ctx) {
        errno = EFAULT;
        return -1;
    }

    zlink::socket_base_t *socket =
      ctx->create_socket (ZLINK_CORE_SOCKET_DEALER);
    if (!socket)
        return -1;

    const int linger = 0;
    socket->setsockopt (ZLINK_INTERNAL_OPT_LINGER, &linger, sizeof (linger));
    if (socket->connect (runtime_->route_ingress_endpoint.c_str ()) != 0) {
        const int saved_errno = errno;
        socket->stop ();
        socket->close ();
        errno = saved_errno;
        return -1;
    }

    zlink::spot_data_plane_forwarder_t::pump_socket_commands (socket);
    socket->set_all_pipes_nodelay ();
    if (zlink::wait_socket_events_internal (socket, ZLINK_POLLOUT, 100) <= 0) {
        const int saved_errno = errno != 0 ? errno : EAGAIN;
        socket->stop ();
        socket->close ();
        errno = saved_errno;
        return -1;
    }

    const int rc = send_combined_parts_locked (socket, parts_);
    const int saved_errno = errno;
    socket->stop ();
    socket->close ();
    errno = saved_errno;
    return rc;
}

int enqueue_runtime_node_router_once (zlink::spot_runtime_t *runtime_,
                                      std::vector<zlink_msg_t> *parts_)
{
    if (!runtime_ || !parts_) {
        errno = EFAULT;
        return -1;
    }

    zlink::ctx_t *ctx = zlink::spot_node_access_t::ctx (runtime_->owner);
    if (!ctx) {
        errno = EFAULT;
        return -1;
    }

    zlink::socket_base_t *socket =
      ctx->create_socket (ZLINK_CORE_SOCKET_DEALER);
    if (!socket)
        return -1;

    const int linger = 0;
    socket->setsockopt (ZLINK_INTERNAL_OPT_LINGER, &linger, sizeof (linger));
    if (socket->connect (runtime_->node_router_endpoint.c_str ()) != 0) {
        const int saved_errno = errno;
        socket->stop ();
        socket->close ();
        errno = saved_errno;
        return -1;
    }

    zlink::spot_data_plane_forwarder_t::pump_socket_commands (socket);
    socket->set_all_pipes_nodelay ();
    if (zlink::wait_socket_events_internal (socket, ZLINK_POLLOUT, 100) <= 0) {
        const int saved_errno = errno != 0 ? errno : EAGAIN;
        socket->stop ();
        socket->close ();
        errno = saved_errno;
        return -1;
    }

    const int rc = send_combined_parts_locked (socket, parts_);
    const int saved_errno = errno;
    socket->stop ();
    socket->close ();
    errno = saved_errno;
    return rc;
}

void bind_router_state_rid (void *router_,
                            const std::string &router_rid_,
                            const std::shared_ptr<local_router_spot_state_t> &state_)
{
    if (!router_ || !state_)
        return;

    std::lock_guard<std::mutex> lock (g_local_spot_states_mutex);
    for (local_router_state_map_t::iterator it = g_local_router_states.begin ();
         it != g_local_router_states.end ();) {
        if (it->first != router_ && it->second
            && it->second->router_rid == router_rid_) {
            std::lock_guard<std::mutex> state_lock (it->second->mutex);
            close_internal_pair_queue (&it->second->recv_queue);
            it = g_local_router_states.erase (it);
            continue;
        }
        ++it;
    }
    local_router_state_map_t::iterator self = g_local_router_states.find (router_);
    if (self != g_local_router_states.end ())
        self->second->router_rid = router_rid_;
}

uint64_t allocate_request_seq (uint64_t *next_request_seq_,
                               const std::set<uint64_t> &pending_sequences_)
{
    if (!next_request_seq_) {
        errno = EFAULT;
        return 0;
    }

    const uint64_t start = *next_request_seq_ == 0 ? 1 : *next_request_seq_;
    uint64_t candidate = start;

    do {
        if (candidate == 0)
            candidate = 1;

        if (pending_sequences_.count (candidate) == 0) {
            uint64_t next = candidate + 1;
            if (next == 0)
                next = 1;
            *next_request_seq_ = next;
            return candidate;
        }

        ++candidate;
        if (candidate == 0)
            candidate = 1;
    } while (candidate != start);

    errno = EBUSY;
    return 0;
}

struct spot_timeout_callback_ctx_t
{
    std::shared_ptr<local_spot_request_reply_state_t> state;
    pending_spot_key_t key;
};

struct router_spot_timeout_callback_ctx_t
{
    std::shared_ptr<local_router_spot_state_t> state;
    uint64_t request_seq;
};

void on_spot_request_timeout (void *userdata_)
{
    std::unique_ptr<spot_timeout_callback_ctx_t> ctx (
      static_cast<spot_timeout_callback_ctx_t *> (userdata_));
    if (!ctx.get () || !ctx->state)
        return;

    pending_reply_t pending;
    bool found = false;
    {
        std::lock_guard<std::mutex> lock (ctx->state->mutex);
        std::map<pending_spot_key_t, pending_reply_t>::iterator it =
          ctx->state->pending_replies.find (ctx->key);
        if (it == ctx->state->pending_replies.end ())
            return;
        pending = it->second;
        ctx->state->pending_sequences.erase (ctx->key.request_seq);
        ctx->state->pending_replies.erase (it);
        found = true;
    }

    if (found)
        zlink::request_reply::complete_reply_callback (
          pending.handler, ETIMEDOUT, NULL, 0, pending.userdata);
}

void on_router_spot_request_timeout (void *userdata_)
{
    std::unique_ptr<router_spot_timeout_callback_ctx_t> ctx (
      static_cast<router_spot_timeout_callback_ctx_t *> (userdata_));
    if (!ctx.get () || !ctx->state)
        return;

    pending_reply_t pending;
    bool found = false;
    {
        std::lock_guard<std::mutex> lock (ctx->state->mutex);
        std::map<uint64_t, pending_reply_t>::iterator it =
          ctx->state->pending_replies.find (ctx->request_seq);
        if (it == ctx->state->pending_replies.end ())
            return;
        pending = it->second;
        ctx->state->pending_sequences.erase (ctx->request_seq);
        ctx->state->pending_replies.erase (it);
        found = true;
    }

    if (found)
        zlink::request_reply::complete_reply_callback (
          pending.handler, ETIMEDOUT, NULL, 0, pending.userdata);
}

bool parse_combined_local_message (
  std::vector<zlink_msg_t> *combined_,
  parsed_spot_envelope_t *spot_envelope_out_,
  zlink::request_reply::parsed_envelope_t *request_reply_envelope_out_)
{
    if (!combined_ || !spot_envelope_out_ || !request_reply_envelope_out_) {
        errno = EFAULT;
        return false;
    }

    if (!parse_spot_routed_envelope (&(*combined_)[0], combined_->size (),
                                     spot_envelope_out_)) {
        errno = EPROTO;
        return false;
    }

    if (!zlink::request_reply::parse_envelope (
          spot_envelope_out_->payload_parts, spot_envelope_out_->payload_part_count,
          request_reply_envelope_out_)) {
        errno = EPROTO;
        return false;
    }

    return true;
}

int recv_combined_router_message (zlink::socket_base_t *socket_,
                                  std::vector<zlink_msg_t> *out_)
{
    if (!socket_ || !out_) {
        errno = EFAULT;
        return -1;
    }

    out_->clear ();

    zlink_routing_id_t source_rid;
    memset (&source_rid, 0, sizeof (source_rid));

    zlink_msg_t first;
    zlink_msg_init (&first);
    if (zlink::recv_msg_routed_socket (socket_, &first, &source_rid,
                                       ZLINK_DONTWAIT)
        != 0) {
        zlink_msg_close (&first);
        return -1;
    }

    out_->push_back (first);
    while (frame_has_more_local (out_->back ())) {
        zlink_msg_t next;
        zlink_msg_init (&next);
        if (recv_followup_with_retry_local (socket_, &next, ZLINK_DONTWAIT)
            != 0) {
            const int saved_errno = errno;
            zlink::request_reply::close_built_parts (out_);
            out_->clear ();
            errno = saved_errno;
            return -1;
        }
        out_->push_back (next);
    }

    return 0;
}

int build_spot_request_reply_message (uint8_t source_class_,
                                      const std::string &source_node_rid_,
                                      const std::string &source_endpoint_rid_,
                                      uint8_t destination_class_,
                                      const std::string &destination_node_rid_,
                                      const std::string &destination_endpoint_rid_,
                                      uint8_t message_type_,
                                      uint64_t request_seq_,
                                      zlink_msg_t *parts_,
                                      size_t part_count_,
                                      std::vector<zlink_msg_t> *out_)
{
    if (!parts_ || part_count_ == 0 || request_seq_ == 0 || !out_) {
        errno = EINVAL;
        return -1;
    }

    const size_t total_part_count =
      spot_routed_control_part_count + zlink::request_reply::control_part_count
      + part_count_;
    out_->resize (total_part_count);
    for (size_t i = 0; i < total_part_count; ++i)
        zlink_msg_init (&(*out_)[i]);

    unsigned char spot_protocol_id = zmp_spot_routed_protocol_id;
    unsigned char version = zmp_protocol_version;
    unsigned char source_class = source_class_;
    unsigned char destination_class = destination_class_;
    unsigned char rr_protocol_id = zlink::request_reply::protocol_id;
    unsigned char rr_type = message_type_;
    unsigned char seq_buf[8];
    zlink::request_reply::encode_u64_be (request_seq_, seq_buf);

    if (zlink::request_reply::init_control_part (&(*out_)[0], &spot_protocol_id, 1)
          != 0
        || zlink::request_reply::init_control_part (&(*out_)[1], &version, 1)
             != 0
        || zlink::request_reply::init_control_part (&(*out_)[2], &source_class, 1)
             != 0
        || zlink::request_reply::init_control_part (&(*out_)[3],
                                                    source_node_rid_.data (),
                                                    source_node_rid_.size ())
             != 0
        || zlink::request_reply::init_control_part (&(*out_)[4],
                                                    source_endpoint_rid_.data (),
                                                    source_endpoint_rid_.size ())
             != 0
        || zlink::request_reply::init_control_part (&(*out_)[5],
                                                    &destination_class, 1)
             != 0
        || zlink::request_reply::init_control_part (
             &(*out_)[6], destination_node_rid_.data (),
             destination_node_rid_.size ())
             != 0
        || zlink::request_reply::init_control_part (
             &(*out_)[7], destination_endpoint_rid_.data (),
             destination_endpoint_rid_.size ())
             != 0
        || zlink::request_reply::init_control_part (&(*out_)[8],
                                                    &rr_protocol_id, 1)
             != 0
        || zlink::request_reply::init_control_part (&(*out_)[9], &version, 1)
             != 0
        || zlink::request_reply::init_control_part (&(*out_)[10], &rr_type, 1)
             != 0
        || zlink::request_reply::init_control_part (&(*out_)[11], seq_buf,
                                                    sizeof (seq_buf))
             != 0) {
        const int saved_errno = errno;
        zlink::request_reply::close_built_parts (out_);
        zlink::request_reply::consume_send_frames_from (parts_, 0, part_count_);
        errno = saved_errno;
        return -1;
    }

    for (size_t i = 0; i < part_count_; ++i) {
        if (zlink_msg_move (&(*out_)[spot_routed_control_part_count
                                     + zlink::request_reply::control_part_count
                                     + i],
                            &parts_[i])
            != 0) {
            const int saved_errno = errno;
            zlink::request_reply::close_built_parts (out_);
            zlink::request_reply::consume_send_frames_from (parts_, i,
                                                            part_count_);
            errno = saved_errno;
            return -1;
        }
    }

    return 0;
}

int build_spot_routed_message (uint8_t source_class_,
                               const std::string &source_node_rid_,
                               const std::string &source_endpoint_rid_,
                               uint8_t destination_class_,
                               const std::string &destination_node_rid_,
                               const std::string &destination_endpoint_rid_,
                               zlink_msg_t *parts_,
                               size_t part_count_,
                               std::vector<zlink_msg_t> *out_)
{
    if (!parts_ || part_count_ == 0 || !out_) {
        errno = EINVAL;
        return -1;
    }

    const size_t total_part_count = spot_routed_control_part_count + part_count_;
    out_->resize (total_part_count);
    for (size_t i = 0; i < total_part_count; ++i)
        zlink_msg_init (&(*out_)[i]);

    unsigned char spot_protocol_id = zmp_spot_routed_protocol_id;
    unsigned char version = zmp_protocol_version;
    unsigned char source_class = source_class_;
    unsigned char destination_class = destination_class_;

    if (zlink::request_reply::init_control_part (&(*out_)[0], &spot_protocol_id, 1)
          != 0
        || zlink::request_reply::init_control_part (&(*out_)[1], &version, 1)
             != 0
        || zlink::request_reply::init_control_part (&(*out_)[2], &source_class, 1)
             != 0
        || zlink::request_reply::init_control_part (&(*out_)[3],
                                                    source_node_rid_.data (),
                                                    source_node_rid_.size ())
             != 0
        || zlink::request_reply::init_control_part (&(*out_)[4],
                                                    source_endpoint_rid_.data (),
                                                    source_endpoint_rid_.size ())
             != 0
        || zlink::request_reply::init_control_part (&(*out_)[5],
                                                    &destination_class, 1)
             != 0
        || zlink::request_reply::init_control_part (
             &(*out_)[6], destination_node_rid_.data (),
             destination_node_rid_.size ())
             != 0
        || zlink::request_reply::init_control_part (
             &(*out_)[7], destination_endpoint_rid_.data (),
             destination_endpoint_rid_.size ())
             != 0) {
        const int saved_errno = errno;
        zlink::request_reply::close_built_parts (out_);
        zlink::request_reply::consume_send_frames_from (parts_, 0, part_count_);
        errno = saved_errno;
        return -1;
    }

    for (size_t i = 0; i < part_count_; ++i) {
        if (zlink_msg_move (&(*out_)[spot_routed_control_part_count + i], &parts_[i])
            != 0) {
            const int saved_errno = errno;
            zlink::request_reply::close_built_parts (out_);
            zlink::request_reply::consume_send_frames_from (parts_, i,
                                                            part_count_);
            errno = saved_errno;
            return -1;
        }
    }

    return 0;
}

int deliver_reply_to_spot (
  const parsed_spot_envelope_t &spot_envelope_,
  const zlink::request_reply::parsed_envelope_t &rr_envelope_)
{
    std::shared_ptr<local_spot_request_reply_state_t> state =
      find_spot_state_by_identity (spot_envelope_.destination_node_rid,
                                   spot_envelope_.destination_endpoint_rid);
    if (!state) {
        errno = ENOENT;
        return -1;
    }

    pending_spot_key_t key;
    key.source_class = spot_envelope_.source_class;
    key.source_rid = spot_envelope_.source_class == zmp_router_class
                       ? spot_envelope_.source_endpoint_rid
                       : spot_envelope_.source_node_rid;
    key.source_spot_rid = spot_envelope_.source_class == zmp_spot_class
                            ? spot_envelope_.source_endpoint_rid
                            : std::string ();
    key.request_seq = rr_envelope_.request_seq;

    pending_reply_t pending;
    bool found = false;
    {
        std::lock_guard<std::mutex> lock (state->mutex);
        std::map<pending_spot_key_t, pending_reply_t>::iterator it =
          state->pending_replies.find (key);
        if (it != state->pending_replies.end ()) {
            pending = it->second;
            state->pending_sequences.erase (key.request_seq);
            state->pending_replies.erase (it);
            found = true;
        }
    }

    zlink::request_timeout::cancel (pending.timeout_task);

    if (!found)
        return 0;

    int callback_errno = 0;
    zlink_msg_t *callback_parts = rr_envelope_.payload_parts;
    size_t callback_part_count = rr_envelope_.payload_part_count;
    if (zlink::request_reply::decode_reply_completion (
          rr_envelope_.message_type, rr_envelope_.payload_parts,
          rr_envelope_.payload_part_count, &callback_errno, &callback_parts,
          &callback_part_count)
        != 0) {
        return -1;
    }

    zlink::request_reply::complete_reply_callback (
      pending.handler, callback_errno, callback_parts, callback_part_count,
      pending.userdata);
    return 0;
}

int deliver_reply_to_router (const std::string &router_rid_,
                             const zlink::request_reply::parsed_envelope_t
                               &rr_envelope_)
{
    std::shared_ptr<local_router_spot_state_t> state =
      find_router_state_by_rid (router_rid_);
    if (!state) {
        errno = ENOENT;
        return -1;
    }

    pending_reply_t pending;
    bool found = false;
    {
        std::lock_guard<std::mutex> lock (state->mutex);
        std::map<uint64_t, pending_reply_t>::iterator it =
          state->pending_replies.find (rr_envelope_.request_seq);
        if (it != state->pending_replies.end ()) {
            pending = it->second;
            state->pending_sequences.erase (rr_envelope_.request_seq);
            state->pending_replies.erase (it);
            found = true;
        }
    }

    zlink::request_timeout::cancel (pending.timeout_task);

    if (!found)
        return 0;

    int callback_errno = 0;
    zlink_msg_t *callback_parts = rr_envelope_.payload_parts;
    size_t callback_part_count = rr_envelope_.payload_part_count;
    if (zlink::request_reply::decode_reply_completion (
          rr_envelope_.message_type, rr_envelope_.payload_parts,
          rr_envelope_.payload_part_count, &callback_errno, &callback_parts,
          &callback_part_count)
        != 0) {
        return -1;
    }

    zlink::request_reply::complete_reply_callback (
      pending.handler, callback_errno, callback_parts, callback_part_count,
      pending.userdata);
    return 0;
}

int dispatch_local_reply (std::vector<zlink_msg_t> *combined_)
{
    if (!combined_) {
        errno = EFAULT;
        return -1;
    }

    parsed_spot_envelope_t spot_envelope;
    zlink::request_reply::parsed_envelope_t rr_envelope;
    if (!parse_combined_local_message (combined_, &spot_envelope, &rr_envelope))
        return -1;

    if (spot_envelope.destination_class == zmp_spot_class)
        return deliver_reply_to_spot (spot_envelope, rr_envelope);

    return deliver_reply_to_router (spot_envelope.destination_endpoint_rid,
                                    rr_envelope);
}

int synthesize_local_error_reply (const parsed_spot_envelope_t &request_envelope_,
                                  uint64_t request_seq_,
                                  int errnum_)
{
    zlink_msg_t errno_part;
    zlink_msg_init (&errno_part);

    unsigned char errbuf[4];
    zlink::request_reply::encode_u32_be (static_cast<uint32_t> (errnum_),
                                         errbuf);
    if (zlink_msg_init_size (&errno_part, sizeof (errbuf)) != 0)
        return -1;
    memcpy (zlink_msg_data (&errno_part), errbuf, sizeof (errbuf));

    std::vector<zlink_msg_t> combined;
    if (build_spot_request_reply_message (
          request_envelope_.destination_class,
          request_envelope_.destination_node_rid,
          request_envelope_.destination_endpoint_rid,
          request_envelope_.source_class, request_envelope_.source_node_rid,
          request_envelope_.source_endpoint_rid,
          zlink::request_reply::error_reply_type,
          request_seq_, &errno_part, 1, &combined)
        != 0) {
        const int saved_errno = errno;
        zlink::request_reply::consume_send_frame (&errno_part);
        errno = saved_errno;
        return -1;
    }

    const int rc = dispatch_local_reply (&combined);
    const int saved_errno = errno;
    zlink::request_reply::close_built_parts (&combined);
    errno = saved_errno;
    return rc;
}

int dispatch_spot_request_to_spot (const parsed_spot_envelope_t &spot_envelope_,
                                   const zlink::request_reply::parsed_envelope_t
                                     &rr_envelope_)
{
    std::shared_ptr<local_spot_request_reply_state_t> state =
      find_spot_state_by_identity (spot_envelope_.destination_node_rid,
                                   spot_envelope_.destination_endpoint_rid);
    if (!state) {
        // Local short-circuit still preserves wire semantics by building the
        // same request envelope and completing the requester through a
        // protocol-level error reply instead of a direct local failure.
        return synthesize_local_error_reply (spot_envelope_,
                                             rr_envelope_.request_seq,
                                             ENOENT);
    }

    zlink_routing_id_t source_rid;
    zlink_routing_id_t source_spot_rid;
    routing_id_from_string (
      spot_envelope_.source_class == zmp_router_class
        ? spot_envelope_.source_endpoint_rid
        : spot_envelope_.source_node_rid,
      &source_rid);
    routing_id_from_string (
      spot_envelope_.source_class == zmp_spot_class
        ? spot_envelope_.source_endpoint_rid
        : std::string (),
      &source_spot_rid);

    return dispatch_spot_message (
      state.get (), &source_rid, &source_spot_rid, rr_envelope_.request_seq,
      rr_envelope_.payload_parts, rr_envelope_.payload_part_count);
}

int dispatch_spot_request_to_router (
  const std::string &router_rid_,
  const parsed_spot_envelope_t &spot_envelope_,
  const zlink::request_reply::parsed_envelope_t &rr_envelope_)
{
    std::shared_ptr<local_router_spot_state_t> state =
      find_router_state_by_rid (router_rid_);
    if (!state) {
        return synthesize_local_error_reply (spot_envelope_,
                                             rr_envelope_.request_seq,
                                             ENOENT);
    }

    zlink_router_spot_handler_fn handler = NULL;
    void *handler_userdata = NULL;
    {
        std::lock_guard<std::mutex> lock (state->mutex);
        handler = state->handler;
        handler_userdata = state->handler_userdata;
    }

    zlink_routing_id_t source_node_rid;
    zlink_routing_id_t source_spot_rid;
    routing_id_from_string (spot_envelope_.source_node_rid, &source_node_rid);
    routing_id_from_string (spot_envelope_.source_endpoint_rid,
                            &source_spot_rid);
    if (handler) {
        handler (&source_node_rid, &source_spot_rid, rr_envelope_.request_seq,
                 rr_envelope_.payload_parts, rr_envelope_.payload_part_count,
                 handler_userdata);
        return 0;
    }

    return dispatch_router_spot_message (
      state.get (), &source_node_rid, &source_spot_rid,
      rr_envelope_.request_seq, rr_envelope_.payload_parts,
      rr_envelope_.payload_part_count);
}

int dispatch_local_request (const std::string &router_rid_,
                            std::vector<zlink_msg_t> *combined_)
{
    parsed_spot_envelope_t spot_envelope;
    zlink::request_reply::parsed_envelope_t rr_envelope;
    if (!parse_combined_local_message (combined_, &spot_envelope, &rr_envelope))
        return -1;

    if (spot_envelope.destination_class == zmp_spot_class)
        return dispatch_spot_request_to_spot (spot_envelope, rr_envelope);

    return dispatch_spot_request_to_router (router_rid_, spot_envelope,
                                            rr_envelope);
}

void erase_spot_pending_request (
  const std::shared_ptr<local_spot_request_reply_state_t> &state_,
  const pending_spot_key_t &key_)
{
    std::lock_guard<std::mutex> lock (state_->mutex);
    state_->pending_sequences.erase (key_.request_seq);
    std::map<pending_spot_key_t, pending_reply_t>::iterator it =
      state_->pending_replies.find (key_);
    if (it == state_->pending_replies.end ())
        return;
    zlink::request_timeout::cancel (it->second.timeout_task);
    state_->pending_replies.erase (it);
}

int dispatch_local_built_message (uint8_t source_class_,
                                  const std::string &source_node_rid_,
                                  const std::string &source_endpoint_rid_,
                                  uint8_t destination_class_,
                                  const std::string &destination_node_rid_,
                                  const std::string &destination_endpoint_rid_,
                                  uint8_t message_type_,
                                  uint64_t request_seq_,
                                  zlink_msg_t *parts_,
                                  size_t part_count_)
{
    std::vector<zlink_msg_t> combined;
    if (build_spot_request_reply_message (
          source_class_, source_node_rid_, source_endpoint_rid_,
          destination_class_, destination_node_rid_, destination_endpoint_rid_,
          message_type_, request_seq_, parts_, part_count_, &combined)
        != 0) {
        return -1;
    }

    const int rc = dispatch_local_reply (&combined);
    const int saved_errno = errno;
    zlink::request_reply::close_built_parts (&combined);
    errno = saved_errno;
    return rc;
}

int start_spot_request_common (void *spot_,
                               uint8_t destination_class_,
                               const std::string &destination_node_rid_,
                               const std::string &destination_endpoint_rid_,
                               uint8_t pending_source_class_,
                               const std::string &pending_source_rid_,
                               const std::string &pending_source_spot_rid_,
                               zlink_msg_t *parts_,
                               size_t part_count_,
                               uint32_t timeout_ms_,
                               zlink_reply_handler_fn handler_,
                               void *userdata_)
{
    routing_pair_t source_identity;
    if (!resolve_spot_identity (spot_, &source_identity))
        return -1;

    std::shared_ptr<local_spot_request_reply_state_t> state =
      find_or_create_spot_state (spot_);
    pending_spot_key_t key;
    pending_reply_t pending;
    uint32_t resolved_timeout_ms = zlink::request_reply::default_timeout_ms;
    {
        std::lock_guard<std::mutex> lock (state->mutex);
        const uint64_t request_seq =
          allocate_request_seq (&state->next_request_seq, state->pending_sequences);
        if (request_seq == 0)
            return -1;

        key.source_class = pending_source_class_;
        key.source_rid = pending_source_rid_;
        key.source_spot_rid = pending_source_spot_rid_;
        key.request_seq = request_seq;
        pending.key = key;
        pending.handler = handler_;
        pending.userdata = userdata_;
        resolved_timeout_ms = zlink::request_reply::resolve_timeout_ms (
          timeout_ms_, state->default_timeout_ms);
        std::unique_ptr<spot_timeout_callback_ctx_t> timeout_ctx (
          new (std::nothrow) spot_timeout_callback_ctx_t ());
        if (!timeout_ctx.get ()) {
            errno = ENOMEM;
            return -1;
        }
        timeout_ctx->state = state;
        timeout_ctx->key = key;
        pending.timeout_task =
          zlink::request_timeout::schedule (resolved_timeout_ms,
                                            &on_spot_request_timeout,
                                            timeout_ctx.release ());
        if (!pending.timeout_task) {
            errno = ENOMEM;
            return -1;
        }
        state->pending_sequences.insert (request_seq);
        state->pending_replies[key] = pending;
    }

    std::vector<zlink_msg_t> combined;
    if (build_spot_request_reply_message (
          zmp_spot_class, source_identity.node_rid, source_identity.spot_rid,
          destination_class_, destination_node_rid_, destination_endpoint_rid_,
          zlink::request_reply::request_type, key.request_seq, parts_,
          part_count_, &combined)
        != 0) {
        erase_spot_pending_request (state, key);
        return -1;
    }

    zlink::spot_runtime_t *runtime =
      destination_class_ == zmp_spot_class
        ? resolve_runtime_for_spot_destination (destination_node_rid_,
                                                destination_endpoint_rid_)
        : resolve_active_spot_runtime (spot_);
    const bool local_target =
      destination_class_ == zmp_spot_class
        ? static_cast<bool> (find_spot_state_by_identity (
            destination_node_rid_, destination_endpoint_rid_))
        : static_cast<bool> (
            find_router_state_by_rid (destination_endpoint_rid_));
    int rc = local_target
               ? dispatch_local_request (destination_class_ == zmp_router_class
                                           ? destination_endpoint_rid_
                                           : std::string (),
                                         &combined)
               : (runtime ? enqueue_spot_state_route_ingress (state.get (),
                                                              runtime, &combined)
                          : -1);
    if (rc != 0 && !local_target)
        rc = dispatch_local_request (destination_class_ == zmp_router_class
                                       ? destination_endpoint_rid_
                                       : std::string (),
                                     &combined);
    if (rc != 0) {
        const int saved_errno = errno;
        zlink::request_reply::close_built_parts (&combined);
        erase_spot_pending_request (state, key);
        errno = saved_errno;
        return -1;
    }

    zlink::request_reply::close_built_parts (&combined);
    return 0;
}

int start_spot_request_to_spot (void *spot_,
                                const zlink_routing_id_t *dest_node_rid_,
                                const zlink_routing_id_t *dest_spot_rid_,
                                zlink_msg_t *parts_,
                                size_t part_count_,
                                uint32_t timeout_ms_,
                                zlink_reply_handler_fn handler_,
                                void *userdata_)
{
    if (!has_valid_routing_id (dest_node_rid_) || !has_valid_routing_id (dest_spot_rid_)
        || !handler_) {
        errno = EINVAL;
        return -1;
    }

    return start_spot_request_common (
      spot_, zmp_spot_class, routing_id_key (dest_node_rid_),
      routing_id_key (dest_spot_rid_), zmp_spot_class,
      routing_id_key (dest_node_rid_), routing_id_key (dest_spot_rid_), parts_,
      part_count_, timeout_ms_, handler_, userdata_);
}

int start_spot_request_to_router (void *spot_,
                                  const zlink_routing_id_t *peer_rid_,
                                  zlink_msg_t *parts_,
                                  size_t part_count_,
                                  uint32_t timeout_ms_,
                                  zlink_reply_handler_fn handler_,
                                  void *userdata_)
{
    if (!has_valid_routing_id (peer_rid_) || !handler_) {
        errno = EINVAL;
        return -1;
    }

    return start_spot_request_common (
      spot_, zmp_router_class, std::string (), routing_id_key (peer_rid_),
      zmp_router_class, routing_id_key (peer_rid_), std::string (), parts_,
      part_count_, timeout_ms_, handler_, userdata_);
}

int start_router_request_to_spot (void *router_,
                                  const zlink_routing_id_t *dest_node_rid_,
                                  const zlink_routing_id_t *dest_spot_rid_,
                                  zlink_msg_t *parts_,
                                  size_t part_count_,
                                  uint32_t timeout_ms_,
                                  zlink_reply_handler_fn handler_,
                                  void *userdata_)
{
    if (!handler_ || !has_valid_routing_id (dest_node_rid_)
        || !has_valid_routing_id (dest_spot_rid_)) {
        errno = EINVAL;
        return -1;
    }

    zlink_routing_id_t router_rid;
    memset (&router_rid, 0, sizeof (router_rid));
    if (zlink_get_routing_id (router_, &router_rid) != 0 || router_rid.size == 0)
        return -1;

    std::shared_ptr<local_router_spot_state_t> state =
      find_or_create_router_state (router_);
    state->router_rid = routing_id_key (&router_rid);

    uint64_t request_seq = 0;
    pending_reply_t pending;
    uint32_t resolved_timeout_ms = zlink::request_reply::default_timeout_ms;
    {
        std::lock_guard<std::mutex> lock (state->mutex);
        request_seq =
          allocate_request_seq (&state->next_request_seq, state->pending_sequences);
        if (request_seq == 0)
            return -1;

        pending_spot_key_t key;
        key.source_class = zmp_spot_class;
        key.source_rid = routing_id_key (dest_node_rid_);
        key.source_spot_rid = routing_id_key (dest_spot_rid_);
        key.request_seq = request_seq;
        pending.key = key;
        pending.handler = handler_;
        pending.userdata = userdata_;
        resolved_timeout_ms = zlink::request_reply::resolve_timeout_ms (
          timeout_ms_, state->default_timeout_ms);
        std::unique_ptr<router_spot_timeout_callback_ctx_t> timeout_ctx (
          new (std::nothrow) router_spot_timeout_callback_ctx_t ());
        if (!timeout_ctx.get ()) {
            errno = ENOMEM;
            return -1;
        }
        timeout_ctx->state = state;
        timeout_ctx->request_seq = request_seq;
        pending.timeout_task =
          zlink::request_timeout::schedule (resolved_timeout_ms,
                                            &on_router_spot_request_timeout,
                                            timeout_ctx.release ());
        if (!pending.timeout_task) {
            errno = ENOMEM;
            return -1;
        }
        state->pending_sequences.insert (request_seq);
        state->pending_replies[request_seq] = pending;
    }

    std::vector<zlink_msg_t> combined;
    if (build_spot_request_reply_message (
          zmp_router_class, std::string (), state->router_rid, zmp_spot_class,
          routing_id_key (dest_node_rid_), routing_id_key (dest_spot_rid_),
          zlink::request_reply::request_type, request_seq, parts_, part_count_,
          &combined)
        != 0) {
        std::lock_guard<std::mutex> lock (state->mutex);
        state->pending_sequences.erase (request_seq);
        state->pending_replies.erase (request_seq);
        return -1;
    }

    const bool local_target = static_cast<bool> (find_spot_state_by_identity (
      routing_id_key (dest_node_rid_), routing_id_key (dest_spot_rid_)));
    zlink::spot_runtime_t *runtime =
      local_target ? NULL
                   : resolve_runtime_for_spot_destination (
                       routing_id_key (dest_node_rid_),
                       routing_id_key (dest_spot_rid_));
    int rc = local_target
               ? dispatch_local_request (std::string (), &combined)
               : (runtime ? enqueue_runtime_route_ingress_once (runtime,
                                                                &combined)
                          : -1);
    if (rc != 0 && !local_target)
        rc = dispatch_local_request (std::string (), &combined);
    if (rc != 0) {
        const int saved_errno = errno;
        zlink::request_reply::close_built_parts (&combined);
        std::lock_guard<std::mutex> lock (state->mutex);
        state->pending_sequences.erase (request_seq);
        state->pending_replies.erase (request_seq);
        errno = saved_errno;
        return -1;
    }

    zlink::request_reply::close_built_parts (&combined);
    return 0;
}

int dispatch_local_direct_to_spot (uint8_t source_class_,
                                   const std::string &source_node_rid_,
                                   const std::string &source_endpoint_rid_,
                                   const std::string &dest_node_rid_,
                                   const std::string &dest_spot_rid_,
                                   zlink_msg_t *parts_,
                                   size_t part_count_)
{
    std::shared_ptr<local_spot_request_reply_state_t> state =
      find_spot_state_by_identity (dest_node_rid_, dest_spot_rid_);
    if (!state) {
        zlink::request_reply::consume_send_frames_from (parts_, 0, part_count_);
        errno = 0;
        return 0;
    }

    zlink_routing_id_t source_rid;
    zlink_routing_id_t spot_rid;
    routing_id_from_string (
      source_class_ == zmp_router_class ? source_endpoint_rid_ : source_node_rid_,
      &source_rid);
    routing_id_from_string (
      source_class_ == zmp_spot_class ? source_endpoint_rid_ : std::string (),
      &spot_rid);

    if (dispatch_spot_message (state.get (), &source_rid,
                               spot_rid.size > 0 ? &spot_rid : NULL, 0,
                               parts_, part_count_)
        != 0)
        return -1;
    errno = 0;
    return 0;
}

int dispatch_local_direct_to_router (const std::string &router_rid_,
                                     const std::string &source_node_rid_,
                                     const std::string &source_spot_rid_,
                                     zlink_msg_t *parts_,
                                     size_t part_count_)
{
    std::shared_ptr<local_router_spot_state_t> state =
      find_router_state_by_rid (router_rid_);
    if (!state) {
        zlink::request_reply::consume_send_frames_from (parts_, 0, part_count_);
        errno = ENOENT;
        return -1;
    }

    zlink_routing_id_t source_node_rid;
    zlink_routing_id_t source_spot_rid;
    routing_id_from_string (source_node_rid_, &source_node_rid);
    routing_id_from_string (source_spot_rid_, &source_spot_rid);
    return dispatch_router_spot_message (
      state.get (), &source_node_rid, &source_spot_rid, 0, parts_, part_count_);
}

int process_route_combined_for_local_delivery (std::vector<zlink_msg_t> *combined_)
{
    if (!combined_) {
        errno = EFAULT;
        return -1;
    }

    parsed_spot_envelope_t spot_envelope;
    if (!parse_spot_routed_envelope (&(*combined_)[0], combined_->size (),
                                     &spot_envelope)) {
        errno = EPROTO;
        return -1;
    }

    zlink::request_reply::parsed_envelope_t rr_envelope;
    if (zlink::request_reply::parse_envelope (spot_envelope.payload_parts,
                                              spot_envelope.payload_part_count,
                                              &rr_envelope)) {
        if (rr_envelope.message_type == zlink::request_reply::request_type) {
            if (spot_envelope.destination_class == zmp_spot_class)
                return dispatch_spot_request_to_spot (spot_envelope, rr_envelope);
            return dispatch_spot_request_to_router (
              spot_envelope.destination_endpoint_rid, spot_envelope, rr_envelope);
        }

        if (spot_envelope.destination_class == zmp_spot_class)
            return deliver_reply_to_spot (spot_envelope, rr_envelope);
        return deliver_reply_to_router (spot_envelope.destination_endpoint_rid,
                                        rr_envelope);
    }

    if (spot_envelope.destination_class == zmp_spot_class) {
        return dispatch_local_direct_to_spot (
          spot_envelope.source_class, spot_envelope.source_node_rid,
          spot_envelope.source_endpoint_rid, spot_envelope.destination_node_rid,
          spot_envelope.destination_endpoint_rid, spot_envelope.payload_parts,
          spot_envelope.payload_part_count);
    }

    return dispatch_local_direct_to_router (
      spot_envelope.destination_endpoint_rid, spot_envelope.source_node_rid,
      spot_envelope.source_endpoint_rid, spot_envelope.payload_parts,
      spot_envelope.payload_part_count);
}
}

extern "C" int zlink_spot_process_route_ingress (void *node_, void *socket_)
{
    zlink::socket_base_t *socket =
      static_cast<zlink::socket_base_t *> (socket_);
    if (!socket) {
        errno = EFAULT;
        return -1;
    }

    while (true) {
        std::vector<zlink_msg_t> combined;
        if (recv_combined_router_message (socket, &combined) != 0) {
            if (errno == EAGAIN)
                return 0;
            return -1;
        }

        parsed_spot_envelope_t spot_envelope;
        int rc = -1;
        if (!parse_spot_routed_envelope (&combined[0], combined.size (),
                                         &spot_envelope)) {
            const int saved_errno = errno != 0 ? errno : EPROTO;
            zlink::request_reply::close_built_parts (&combined);
            errno = saved_errno;
            return -1;
        }

        if (spot_envelope.destination_class == zmp_router_class) {
            zlink::spot_runtime_t *runtime =
              zlink::spot_node_access_t::runtime (
                static_cast<zlink::spot_node_t *> (node_));
            rc =
              runtime ? enqueue_runtime_node_router_once (runtime, &combined)
                      : -1;
        } else {
            rc = process_route_combined_for_local_delivery (&combined);
        }

        const int saved_errno = errno;
        zlink::request_reply::close_built_parts (&combined);
        if (rc != 0) {
            errno = saved_errno;
            return -1;
        }
    }
}

extern "C" int zlink_spot_process_node_router (void *node_, void *socket_)
{
    return zlink_spot_process_route_ingress (node_, socket_);
}

int zlink_spot_request_spot (void *spot_,
                             const zlink_routing_id_t *dest_node_rid_,
                             const zlink_routing_id_t *dest_spot_rid_,
                             zlink_msg_t *parts_,
                             size_t part_count_,
                             uint32_t timeout_ms_,
                             zlink_reply_handler_fn handler_,
                             void *userdata_)
{
    if (validate_request_parts (parts_, part_count_) != 0)
        return -1;
    return start_spot_request_to_spot (spot_, dest_node_rid_, dest_spot_rid_,
                                       parts_, part_count_, timeout_ms_,
                                       handler_, userdata_);
}

int zlink_spot_send_spot (void *spot_,
                          const zlink_routing_id_t *dest_node_rid_,
                          const zlink_routing_id_t *dest_spot_rid_,
                          zlink_msg_t *parts_,
                          size_t part_count_,
                          zlink_send_flags_t flags_)
{
    LIBZLINK_UNUSED (flags_);
    if (validate_request_parts (parts_, part_count_) != 0)
        return -1;
    if (!has_valid_routing_id (dest_node_rid_) || !has_valid_routing_id (dest_spot_rid_)) {
        errno = EINVAL;
        return -1;
    }
    if (validate_recv_flags (flags_) != 0)
        return -1;

    routing_pair_t source_identity;
    if (!resolve_spot_identity (spot_, &source_identity))
        return -1;

    std::vector<zlink_msg_t> combined;
    if (build_spot_routed_message (zmp_spot_class, source_identity.node_rid,
                                   source_identity.spot_rid, zmp_spot_class,
                                   routing_id_key (dest_node_rid_),
                                   routing_id_key (dest_spot_rid_), parts_,
                                   part_count_, &combined)
        != 0)
        return -1;

    std::shared_ptr<local_spot_request_reply_state_t> state =
      find_or_create_spot_state (spot_);
    const bool local_target = static_cast<bool> (find_spot_state_by_identity (
      routing_id_key (dest_node_rid_), routing_id_key (dest_spot_rid_)));
    zlink::spot_runtime_t *runtime =
      local_target ? NULL
                   : resolve_runtime_for_spot_destination (
                       routing_id_key (dest_node_rid_),
                       routing_id_key (dest_spot_rid_));
    int rc = local_target
               ? process_route_combined_for_local_delivery (&combined)
               : (runtime ? enqueue_spot_state_route_ingress (state.get (),
                                                              runtime, &combined)
                          : -1);
    if (rc != 0 && !local_target)
        rc = process_route_combined_for_local_delivery (&combined);
    const int saved_errno = errno;
    zlink::request_reply::close_built_parts (&combined);
    errno = saved_errno;
    return rc;
}

int zlink_spot_send_router (void *spot_,
                            const zlink_routing_id_t *peer_rid_,
                            zlink_msg_t *parts_,
                            size_t part_count_,
                            zlink_send_flags_t flags_)
{
    if (validate_request_parts (parts_, part_count_) != 0)
        return -1;
    if (!has_valid_routing_id (peer_rid_)) {
        errno = EINVAL;
        return -1;
    }
    if (validate_recv_flags (flags_) != 0)
        return -1;

    routing_pair_t source_identity;
    if (!resolve_spot_identity (spot_, &source_identity))
        return -1;

    std::vector<zlink_msg_t> combined;
    if (build_spot_routed_message (zmp_spot_class, source_identity.node_rid,
                                   source_identity.spot_rid, zmp_router_class,
                                   std::string (), routing_id_key (peer_rid_),
                                   parts_, part_count_, &combined)
        != 0)
        return -1;

    std::shared_ptr<local_spot_request_reply_state_t> state =
      find_or_create_spot_state (spot_);
    const bool local_target =
      static_cast<bool> (find_router_state_by_rid (routing_id_key (peer_rid_)));
    zlink::spot_runtime_t *runtime =
      local_target ? NULL : resolve_active_spot_runtime (spot_);
    int rc = local_target
               ? process_route_combined_for_local_delivery (&combined)
               : (runtime ? enqueue_spot_state_route_ingress (state.get (),
                                                              runtime, &combined)
                          : -1);
    if (rc != 0 && !local_target)
        rc = process_route_combined_for_local_delivery (&combined);
    if (rc != 0) {
        const int saved_errno = errno;
        zlink::request_reply::close_built_parts (&combined);
        errno = saved_errno;
        return -1;
    }
    zlink::request_reply::close_built_parts (&combined);
    errno = 0;
    return 0;
}

int zlink_spot_request_router (void *spot_,
                               const zlink_routing_id_t *peer_rid_,
                               zlink_msg_t *parts_,
                               size_t part_count_,
                               uint32_t timeout_ms_,
                               zlink_reply_handler_fn handler_,
                               void *userdata_)
{
    if (validate_request_parts (parts_, part_count_) != 0)
        return -1;
    return start_spot_request_to_router (spot_, peer_rid_, parts_, part_count_,
                                         timeout_ms_, handler_, userdata_);
}

int zlink_spot_reply_spot (void *spot_,
                           const zlink_routing_id_t *dest_node_rid_,
                           const zlink_routing_id_t *dest_spot_rid_,
                           uint64_t request_seq_,
                           zlink_msg_t *parts_,
                           size_t part_count_)
{
    if (validate_request_parts (parts_, part_count_) != 0)
        return -1;
    if (!has_valid_routing_id (dest_node_rid_) || !has_valid_routing_id (dest_spot_rid_)
        || request_seq_ == 0) {
        errno = EINVAL;
        return -1;
    }

    routing_pair_t source_identity;
    if (!resolve_spot_identity (spot_, &source_identity))
        return -1;

    std::vector<zlink_msg_t> combined;
    if (build_spot_request_reply_message (
          zmp_spot_class, source_identity.node_rid, source_identity.spot_rid,
          zmp_spot_class, routing_id_key (dest_node_rid_),
          routing_id_key (dest_spot_rid_), zlink::request_reply::reply_type,
          request_seq_, parts_, part_count_, &combined)
        != 0)
        return -1;
    std::shared_ptr<local_spot_request_reply_state_t> state =
      find_or_create_spot_state (spot_);
    const bool local_target = static_cast<bool> (find_spot_state_by_identity (
      routing_id_key (dest_node_rid_), routing_id_key (dest_spot_rid_)));
    zlink::spot_runtime_t *runtime =
      local_target ? NULL
                   : resolve_runtime_for_spot_destination (
                       routing_id_key (dest_node_rid_),
                       routing_id_key (dest_spot_rid_));
    int rc = local_target
               ? dispatch_local_reply (&combined)
               : (runtime ? enqueue_spot_state_route_ingress (state.get (),
                                                              runtime, &combined)
                          : -1);
    if (rc != 0 && !local_target)
        rc = dispatch_local_reply (&combined);
    const int saved_errno = errno;
    zlink::request_reply::close_built_parts (&combined);
    errno = saved_errno;
    return rc;
}

int zlink_spot_reply_router (void *spot_,
                             const zlink_routing_id_t *peer_rid_,
                             uint64_t request_seq_,
                             zlink_msg_t *parts_,
                             size_t part_count_)
{
    if (validate_request_parts (parts_, part_count_) != 0)
        return -1;
    if (!has_valid_routing_id (peer_rid_) || request_seq_ == 0) {
        errno = EINVAL;
        return -1;
    }

    routing_pair_t source_identity;
    if (!resolve_spot_identity (spot_, &source_identity))
        return -1;

    std::vector<zlink_msg_t> combined;
    if (build_spot_request_reply_message (
          zmp_spot_class, source_identity.node_rid, source_identity.spot_rid,
          zmp_router_class, std::string (), routing_id_key (peer_rid_),
          zlink::request_reply::reply_type, request_seq_, parts_, part_count_,
          &combined)
        != 0)
        return -1;
    const bool local_target =
      static_cast<bool> (find_router_state_by_rid (routing_id_key (peer_rid_)));
    zlink::spot_runtime_t *runtime =
      local_target ? NULL : resolve_active_spot_runtime (spot_);
    int rc = local_target
               ? dispatch_local_reply (&combined)
               : (runtime ? enqueue_runtime_route_ingress_once (runtime,
                                                                &combined)
                          : -1);
    if (rc != 0 && !local_target)
        rc = dispatch_local_reply (&combined);
    const int saved_errno = errno;
    zlink::request_reply::close_built_parts (&combined);
    errno = saved_errno;
    return rc;
}

int zlink_spot_handler (void *spot_,
                        zlink_spot_handler_fn handler_,
                        void *userdata_)
{
    if (!handler_) {
        errno = EINVAL;
        return -1;
    }

    if (!as_spot_handle (spot_)) {
        errno = EFAULT;
        return -1;
    }
    if (spot_transition_to_callback_mode (as_spot_handle (spot_)) != 0)
        return -1;

    std::shared_ptr<local_spot_request_reply_state_t> state =
      find_or_create_spot_state (spot_);
    std::lock_guard<std::mutex> lock (state->mutex);
    if (state->request_handler || state->dispatch_event_handler) {
        spot_revert_callback_transition (as_spot_handle (spot_));
        errno = EBUSY;
        return -1;
    }

    state->request_handler = handler_;
    state->request_handler_userdata = userdata_;
    return 0;
}

int zlink_spot_dispatch_event_handler (
  void *spot_,
  zlink_spot_dispatch_event_handler_fn handler_,
  void *userdata_)
{
    if (!handler_) {
        errno = EINVAL;
        return -1;
    }

    if (!as_spot_handle (spot_)) {
        errno = EFAULT;
        return -1;
    }
    if (spot_transition_to_callback_mode (as_spot_handle (spot_)) != 0)
        return -1;

    std::shared_ptr<local_spot_request_reply_state_t> state =
      find_or_create_spot_state (spot_);
    std::lock_guard<std::mutex> lock (state->mutex);
    if (state->request_handler || state->dispatch_event_handler) {
        spot_revert_callback_transition (as_spot_handle (spot_));
        errno = EBUSY;
        return -1;
    }

    state->dispatch_event_handler = handler_;
    state->dispatch_event_handler_userdata = userdata_;
    return 0;
}

int zlink_spot_recv (void *spot_,
                     const zlink_routing_id_t **source_rid_out_,
                     const zlink_routing_id_t **spot_rid_out_,
                     uint64_t *request_seq_out_,
                     zlink_msg_t **parts_out_,
                     size_t *part_count_out_,
                     int flags_)
{
    if (!source_rid_out_ || !spot_rid_out_ || !request_seq_out_ || !parts_out_
        || !part_count_out_) {
        errno = EFAULT;
        return -1;
    }
    if (validate_recv_flags (flags_) != 0)
        return -1;
    if (!as_spot_handle (spot_)) {
        errno = EFAULT;
        return -1;
    }
    if (spot_require_recv_model (as_spot_handle (spot_)) != 0)
        return -1;

    std::shared_ptr<local_spot_request_reply_state_t> state =
      find_or_create_spot_state (spot_);
    std::unique_lock<std::mutex> lock (state->mutex);
    if (state->request_handler || state->dispatch_event_handler) {
        errno = EBUSY;
        return -1;
    }
    if (ensure_internal_pair_queue (resolve_spot_ctx (spot_),
                                    "zlink.spot.routed.recv",
                                    &state->recv_queue)
        != 0)
        return -1;
    lock.unlock ();
    return recv_internal_spot_queue (&state->recv_queue, source_rid_out_,
                                     spot_rid_out_, request_seq_out_,
                                     parts_out_, part_count_out_, flags_);
}

int zlink_router_request_spot (void *router_,
                               const zlink_routing_id_t *dest_node_rid_,
                               const zlink_routing_id_t *dest_spot_rid_,
                               zlink_msg_t *parts_,
                               size_t part_count_,
                               uint32_t timeout_ms_,
                               zlink_reply_handler_fn handler_,
                               void *userdata_)
{
    if (validate_request_parts (parts_, part_count_) != 0)
        return -1;

    socket_handle_t handle = as_socket_handle (router_);
    if (!handle.socket || handle.socket->socket_type () != ZLINK_CORE_SOCKET_ROUTER) {
        errno = EINVAL;
        return -1;
    }

    return start_router_request_to_spot (router_, dest_node_rid_, dest_spot_rid_,
                                         parts_, part_count_, timeout_ms_,
                                         handler_, userdata_);
}

int zlink_router_reply_spot (void *router_,
                             const zlink_routing_id_t *dest_node_rid_,
                             const zlink_routing_id_t *dest_spot_rid_,
                             uint64_t request_seq_,
                             zlink_msg_t *parts_,
                             size_t part_count_)
{
    if (validate_request_parts (parts_, part_count_) != 0)
        return -1;
    if (!has_valid_routing_id (dest_node_rid_) || !has_valid_routing_id (dest_spot_rid_)
        || request_seq_ == 0) {
        errno = EINVAL;
        return -1;
    }

    socket_handle_t handle = as_socket_handle (router_);
    if (!handle.socket || handle.socket->socket_type () != ZLINK_CORE_SOCKET_ROUTER) {
        errno = EINVAL;
        return -1;
    }

    zlink_routing_id_t router_rid;
    memset (&router_rid, 0, sizeof (router_rid));
    if (zlink_get_routing_id (router_, &router_rid) != 0 || router_rid.size == 0)
        return -1;

    std::vector<zlink_msg_t> combined;
    if (build_spot_request_reply_message (
          zmp_router_class, std::string (), routing_id_key (&router_rid),
          zmp_spot_class, routing_id_key (dest_node_rid_),
          routing_id_key (dest_spot_rid_), zlink::request_reply::reply_type,
          request_seq_, parts_, part_count_, &combined)
        != 0)
        return -1;
    const bool local_target = static_cast<bool> (find_spot_state_by_identity (
      routing_id_key (dest_node_rid_), routing_id_key (dest_spot_rid_)));
    zlink::spot_runtime_t *runtime =
      local_target ? NULL
                   : resolve_runtime_for_spot_destination (
                       routing_id_key (dest_node_rid_),
                       routing_id_key (dest_spot_rid_));
    int rc = local_target
               ? dispatch_local_reply (&combined)
               : (runtime ? enqueue_runtime_route_ingress_once (runtime,
                                                                &combined)
                          : -1);
    if (rc != 0 && !local_target)
        rc = dispatch_local_reply (&combined);
    const int saved_errno = errno;
    zlink::request_reply::close_built_parts (&combined);
    errno = saved_errno;
    return rc;
}

int zlink_router_send_spot (void *router_,
                            const zlink_routing_id_t *dest_node_rid_,
                            const zlink_routing_id_t *dest_spot_rid_,
                            zlink_msg_t *parts_,
                            size_t part_count_,
                            zlink_send_flags_t flags_)
{
    if (validate_request_parts (parts_, part_count_) != 0)
        return -1;
    if (!has_valid_routing_id (dest_node_rid_) || !has_valid_routing_id (dest_spot_rid_)) {
        errno = EINVAL;
        return -1;
    }
    if (validate_recv_flags (flags_) != 0)
        return -1;

    socket_handle_t handle = as_socket_handle (router_);
    if (!handle.socket || handle.socket->socket_type () != ZLINK_CORE_SOCKET_ROUTER) {
        errno = EINVAL;
        return -1;
    }

    zlink_routing_id_t router_rid;
    memset (&router_rid, 0, sizeof (router_rid));
    if (zlink_get_routing_id (router_, &router_rid) != 0 || router_rid.size == 0)
        return -1;

    std::vector<zlink_msg_t> combined;
    if (build_spot_routed_message (zmp_router_class, std::string (),
                                   routing_id_key (&router_rid), zmp_spot_class,
                                   routing_id_key (dest_node_rid_),
                                   routing_id_key (dest_spot_rid_), parts_,
                                   part_count_, &combined)
        != 0)
        return -1;
    const bool local_target = static_cast<bool> (find_spot_state_by_identity (
      routing_id_key (dest_node_rid_), routing_id_key (dest_spot_rid_)));
    zlink::spot_runtime_t *runtime =
      local_target ? NULL
                   : resolve_runtime_for_spot_destination (
                       routing_id_key (dest_node_rid_),
                       routing_id_key (dest_spot_rid_));
    int rc = local_target
               ? process_route_combined_for_local_delivery (&combined)
               : (runtime ? enqueue_runtime_route_ingress_once (runtime,
                                                                &combined)
                          : -1);
    if (rc != 0 && !local_target)
        rc = process_route_combined_for_local_delivery (&combined);
    const int saved_errno = errno;
    zlink::request_reply::close_built_parts (&combined);
    errno = saved_errno;
    return rc;
}

int zlink_router_spot_handler (void *router_,
                               zlink_router_spot_handler_fn handler_,
                               void *userdata_)
{
    if (!handler_) {
        errno = EINVAL;
        return -1;
    }

    socket_handle_t handle = as_socket_handle (router_);
    if (!handle.socket || handle.socket->socket_type () != ZLINK_CORE_SOCKET_ROUTER) {
        errno = EINVAL;
        return -1;
    }

    zlink_routing_id_t router_rid;
    memset (&router_rid, 0, sizeof (router_rid));
    if (zlink_get_routing_id (router_, &router_rid) != 0 || router_rid.size == 0)
        return -1;

    std::shared_ptr<local_router_spot_state_t> state =
      find_or_create_router_state (router_);
    bind_router_state_rid (router_, routing_id_key (&router_rid), state);
    std::lock_guard<std::mutex> lock (state->mutex);
    if (state->handler) {
        errno = EBUSY;
        return -1;
    }
    state->handler = handler_;
    state->handler_userdata = userdata_;
    return 0;
}

int zlink_router_spot_recv (void *router_,
                            const zlink_routing_id_t **source_node_rid_out_,
                            const zlink_routing_id_t **source_spot_rid_out_,
                            uint64_t *request_seq_out_,
                            zlink_msg_t **parts_out_,
                            size_t *part_count_out_,
                            int flags_)
{
    if (!source_node_rid_out_ || !source_spot_rid_out_ || !request_seq_out_
        || !parts_out_ || !part_count_out_) {
        errno = EFAULT;
        return -1;
    }
    if (validate_recv_flags (flags_) != 0)
        return -1;

    socket_handle_t handle = as_socket_handle (router_);
    if (!handle.socket || handle.socket->socket_type () != ZLINK_CORE_SOCKET_ROUTER) {
        errno = EINVAL;
        return -1;
    }

    zlink_routing_id_t router_rid;
    memset (&router_rid, 0, sizeof (router_rid));
    if (zlink_get_routing_id (router_, &router_rid) != 0 || router_rid.size == 0)
        return -1;

    std::shared_ptr<local_router_spot_state_t> state =
      find_or_create_router_state (router_);
    bind_router_state_rid (router_, routing_id_key (&router_rid), state);

    std::unique_lock<std::mutex> lock (state->mutex);
    if (state->handler) {
        errno = EBUSY;
        return -1;
    }

    if (ensure_internal_pair_queue (handle.socket->get_ctx (),
                                    "zlink.router.spot.recv",
                                    &state->recv_queue)
        != 0)
        return -1;
    lock.unlock ();
    return recv_internal_spot_queue (&state->recv_queue, source_node_rid_out_,
                                     source_spot_rid_out_, request_seq_out_,
                                     parts_out_, part_count_out_, flags_);
}

extern "C" int zlink_spot_request_reply_set_default_timeout (
  void *spot_,
  const void *optval_,
  size_t optvallen_)
{
    if (!as_spot_handle (spot_)) {
        errno = EINVAL;
        return -1;
    }
    if (!optval_ || optvallen_ != sizeof (int)) {
        errno = EINVAL;
        return -1;
    }

    int timeout_ms = 0;
    memcpy (&timeout_ms, optval_, sizeof (timeout_ms));
    if (timeout_ms < 0) {
        errno = EINVAL;
        return -1;
    }

    std::shared_ptr<local_spot_request_reply_state_t> state =
      find_or_create_spot_state (spot_);
    std::lock_guard<std::mutex> lock (state->mutex);
    state->default_timeout_ms = static_cast<uint32_t> (timeout_ms);
    return 0;
}

extern "C" int zlink_spot_request_reply_get_default_timeout (
  void *spot_,
  void *optval_,
  size_t *optvallen_)
{
    if (!as_spot_handle (spot_)) {
        errno = EINVAL;
        return -1;
    }
    if (!optval_ || !optvallen_ || *optvallen_ < sizeof (int)) {
        errno = EINVAL;
        return -1;
    }

    std::shared_ptr<local_spot_request_reply_state_t> state =
      find_or_create_spot_state (spot_);
    int timeout_ms = 0;
    {
        std::lock_guard<std::mutex> lock (state->mutex);
        timeout_ms = static_cast<int> (state->default_timeout_ms);
    }

    memcpy (optval_, &timeout_ms, sizeof (timeout_ms));
    *optvallen_ = sizeof (timeout_ms);
    return 0;
}

extern "C" void zlink_spot_request_reply_cleanup_spot (void *spot_)
{
    if (!as_spot_handle (spot_))
        return;

    std::lock_guard<std::mutex> lock (g_local_spot_states_mutex);
    local_spot_state_map_t::iterator it = g_local_spot_states.find (spot_);
    if (it != g_local_spot_states.end ()) {
        std::lock_guard<std::mutex> state_lock (it->second->mutex);
        if (it->second->route_ingress_tx) {
            it->second->route_ingress_tx->stop ();
            it->second->route_ingress_tx->close ();
            it->second->route_ingress_tx = NULL;
            it->second->route_ingress_endpoint.clear ();
        }
        close_internal_pair_queue (&it->second->recv_queue);
        g_local_spot_states.erase (it);
    }
}

extern "C" void zlink_spot_request_reply_cleanup_router (void *router_)
{
    const socket_handle_t handle = as_socket_handle (router_);
    if (!handle.socket)
        return;

    std::lock_guard<std::mutex> lock (g_local_spot_states_mutex);
    local_router_state_map_t::iterator it = g_local_router_states.find (router_);
    if (it != g_local_router_states.end ()) {
        std::lock_guard<std::mutex> state_lock (it->second->mutex);
        close_internal_pair_queue (&it->second->recv_queue);
        g_local_router_states.erase (it);
    }
}
