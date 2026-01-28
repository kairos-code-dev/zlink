/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "spot/spot_node.hpp"
#include "spot/spot.hpp"

#include "discovery/protocol.hpp"
#include "sockets/socket_base.hpp"
#include "utils/clock.hpp"
#include "utils/err.hpp"
#include "utils/random.hpp"

#include <algorithm>
#include <string.h>

#if defined ZMQ_HAVE_WINDOWS
#include "utils/windows.hpp"
#else
#include <unistd.h>
#endif

namespace zmq
{
static const uint32_t spot_node_tag_value = 0x1e6700d9;
static const uint32_t default_heartbeat_ms = 5000;
static const uint64_t discovery_refresh_ms = 500;

static void sleep_ms (int ms_)
{
#if defined ZMQ_HAVE_WINDOWS
    Sleep (ms_);
#else
    usleep (static_cast<useconds_t> (ms_) * 1000);
#endif
}

static int create_threadsafe_socket (ctx_t *ctx_, int type_,
                                     socket_base_t **socket_,
                                     thread_safe_socket_t **threadsafe_)
{
    *socket_ = ctx_->create_socket (type_);
    if (!*socket_)
        return -1;

    *threadsafe_ = new (std::nothrow) thread_safe_socket_t (ctx_, *socket_);
    if (!*threadsafe_) {
        (*socket_)->close ();
        *socket_ = NULL;
        errno = ENOMEM;
        return -1;
    }

    (*socket_)->set_threadsafe_proxy (*threadsafe_);
    return 0;
}

static void close_parts (std::vector<msg_t> *parts_)
{
    if (!parts_)
        return;
    for (size_t i = 0; i < parts_->size (); ++i)
        (*parts_)[i].close ();
    parts_->clear ();
}

static bool copy_parts_from_msgv (zmq_msg_t *parts_,
                                  size_t part_count_,
                                  std::vector<msg_t> *out_)
{
    out_->clear ();
    if (part_count_ == 0)
        return true;
    out_->resize (part_count_);
    for (size_t i = 0; i < part_count_; ++i) {
        msg_t &dst = (*out_)[i];
        if (dst.init () != 0) {
            close_parts (out_);
            return false;
        }
        msg_t &src = *reinterpret_cast<msg_t *> (&parts_[i]);
        if (dst.copy (src) != 0) {
            close_parts (out_);
            return false;
        }
    }
    return true;
}

static bool copy_parts_from_vec (const std::vector<msg_t> &src_,
                                 std::vector<msg_t> *out_)
{
    out_->clear ();
    if (src_.empty ())
        return true;
    out_->resize (src_.size ());
    for (size_t i = 0; i < src_.size (); ++i) {
        msg_t &dst = (*out_)[i];
        if (dst.init () != 0) {
            close_parts (out_);
            return false;
        }
        msg_t &src = const_cast<msg_t &> (src_[i]);
        if (dst.copy (src) != 0) {
            close_parts (out_);
            return false;
        }
    }
    return true;
}

static int send_frame_threadsafe (thread_safe_socket_t *socket_,
                                  const void *data_,
                                  size_t size_,
                                  int flags_)
{
    if (!socket_) {
        errno = ENOTSUP;
        return -1;
    }
    msg_t msg;
    if (msg.init_size (size_) != 0)
        return -1;
    if (size_ > 0 && data_)
        memcpy (msg.data (), data_, size_);
    if (socket_->send (&msg, flags_) != 0) {
        msg.close ();
        return -1;
    }
    msg.close ();
    return 0;
}

static int send_u16_threadsafe (thread_safe_socket_t *socket_,
                                uint16_t value_,
                                int flags_)
{
    return send_frame_threadsafe (socket_, &value_, sizeof (value_), flags_);
}

static int send_u32_threadsafe (thread_safe_socket_t *socket_,
                                uint32_t value_,
                                int flags_)
{
    return send_frame_threadsafe (socket_, &value_, sizeof (value_), flags_);
}

static int send_string_threadsafe (thread_safe_socket_t *socket_,
                                   const std::string &value_,
                                   int flags_)
{
    return send_frame_threadsafe (socket_, value_.empty () ? NULL
                                                             : value_.data (),
                                  value_.size (), flags_);
}

spot_node_t::spot_node_t (ctx_t *ctx_) :
    _ctx (ctx_),
    _tag (spot_node_tag_value),
    _pub (NULL),
    _pub_threadsafe (NULL),
    _sub (NULL),
    _sub_threadsafe (NULL),
    _dealer (NULL),
    _dealer_threadsafe (NULL),
    _node_id (0),
    _registered (false),
    _heartbeat_interval_ms (default_heartbeat_ms),
    _last_heartbeat_ms (0),
    _discovery (NULL),
    _next_discovery_refresh_ms (0),
    _stop (0)
{
    zmq_assert (_ctx);

    _routing_id.size = 0;
    _node_id = zmq::generate_random ();
    if (_node_id == 0)
        _node_id = 1;
    _routing_id.size = sizeof (_node_id);
    memcpy (_routing_id.data, &_node_id, sizeof (_node_id));

    if (create_threadsafe_socket (_ctx, ZMQ_SUB, &_sub, &_sub_threadsafe)
        != 0) {
        _sub = NULL;
        _sub_threadsafe = NULL;
    }

    _worker.start (run, this, "spotnode");
}

spot_node_t::~spot_node_t ()
{
    _tag = 0xdeadbeef;
}

bool spot_node_t::check_tag () const
{
    return _tag == spot_node_tag_value;
}

bool spot_node_t::validate_topic (const char *topic_, std::string *out_)
{
    if (!topic_ || topic_[0] == '\0')
        return false;
    const size_t len = strlen (topic_);
    if (len == 0 || len > 255)
        return false;
    if (out_)
        *out_ = std::string (topic_, len);
    return true;
}

bool spot_node_t::validate_pattern (const char *pattern_, std::string *prefix_)
{
    if (!pattern_ || pattern_[0] == '\0')
        return false;
    const size_t len = strlen (pattern_);
    if (len == 0 || len > 255)
        return false;
    const char *star = strchr (pattern_, '*');
    if (!star)
        return false;
    if (star != pattern_ + len - 1)
        return false;
    if (strchr (star + 1, '*'))
        return false;
    if (prefix_)
        *prefix_ = std::string (pattern_, len - 1);
    return true;
}

bool spot_node_t::validate_service_name (const std::string &name_)
{
    if (name_.empty () || name_.size () > 64)
        return false;
    for (size_t i = 0; i < name_.size (); ++i) {
        const char c = name_[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.'
              || c == '-'))
            return false;
    }
    return true;
}

std::string spot_node_t::resolve_advertise (const std::string &bind_endpoint_)
{
    if (bind_endpoint_.empty ())
        return std::string ();

    std::string endpoint = bind_endpoint_;
    std::string::size_type pos = endpoint.find ("tcp://");
    if (pos == 0) {
        std::string::size_type host_start = strlen ("tcp://");
        std::string::size_type colon = endpoint.find (':', host_start);
        if (colon != std::string::npos) {
            std::string host = endpoint.substr (host_start, colon - host_start);
            if (host == "*" || host == "0.0.0.0")
                endpoint.replace (host_start, host.size (), "127.0.0.1");
        }
    }

    return endpoint;
}

bool spot_node_t::topic_is_ringbuffer (const std::string &topic_) const
{
    std::map<std::string, topic_state_t>::const_iterator it =
      _topics.find (topic_);
    if (it == _topics.end ())
        return false;
    return it->second.mode == ZMQ_SPOT_TOPIC_RINGBUFFER;
}

void spot_node_t::ensure_ringbuffer_topic (const std::string &topic_)
{
    std::map<std::string, topic_state_t>::iterator it = _topics.find (topic_);
    if (it != _topics.end ())
        return;
    topic_state_t state;
    state.mode = ZMQ_SPOT_TOPIC_RINGBUFFER;
    _topics.insert (std::make_pair (topic_, state));
}

void spot_node_t::add_filter (const std::string &filter_)
{
    if (filter_.empty ())
        return;
    size_t &count = _filter_refcount[filter_];
    if (count == 0 && _sub_threadsafe) {
        _sub_threadsafe->setsockopt (ZMQ_SUBSCRIBE, filter_.data (),
                                     filter_.size ());
    }
    ++count;
}

void spot_node_t::remove_filter (const std::string &filter_)
{
    std::map<std::string, size_t>::iterator it =
      _filter_refcount.find (filter_);
    if (it == _filter_refcount.end ())
        return;
    if (it->second <= 1) {
        if (_sub_threadsafe) {
            _sub_threadsafe->setsockopt (ZMQ_UNSUBSCRIBE, filter_.data (),
                                         filter_.size ());
        }
        _filter_refcount.erase (it);
        return;
    }
    --it->second;
}

int spot_node_t::bind (const char *endpoint_)
{
    if (!endpoint_) {
        errno = EINVAL;
        return -1;
    }

    if (!_pub_threadsafe) {
        if (create_threadsafe_socket (_ctx, ZMQ_PUB, &_pub, &_pub_threadsafe)
            != 0)
            return -1;
    }

    int rc = _pub_threadsafe->bind (endpoint_);
    if (rc == 0) {
        scoped_lock_t lock (_sync);
        _bind_endpoints.push_back (endpoint_);
    }
    return rc;
}

int spot_node_t::connect_registry (const char *registry_router_endpoint_)
{
    if (!registry_router_endpoint_) {
        errno = EINVAL;
        return -1;
    }

    if (!_dealer_threadsafe) {
        if (create_threadsafe_socket (_ctx, ZMQ_DEALER, &_dealer,
                                      &_dealer_threadsafe)
            != 0)
            return -1;
        _dealer_threadsafe->setsockopt (ZMQ_ROUTING_ID, _routing_id.data,
                                        _routing_id.size);
    }

    const int rc = _dealer_threadsafe->connect (registry_router_endpoint_);
    if (rc == 0) {
        scoped_lock_t lock (_sync);
        _registry_endpoints.insert (registry_router_endpoint_);
    }
    return rc;
}

int spot_node_t::connect_peer_pub (const char *peer_pub_endpoint_)
{
    if (!peer_pub_endpoint_) {
        errno = EINVAL;
        return -1;
    }
    if (!_sub_threadsafe) {
        errno = ENOTSUP;
        return -1;
    }

    scoped_lock_t lock (_sync);
    if (_peer_endpoints.count (peer_pub_endpoint_))
        return 0;
    _peer_endpoints.insert (peer_pub_endpoint_);
    return _sub_threadsafe->connect (peer_pub_endpoint_);
}

int spot_node_t::disconnect_peer_pub (const char *peer_pub_endpoint_)
{
    if (!peer_pub_endpoint_) {
        errno = EINVAL;
        return -1;
    }
    if (!_sub_threadsafe) {
        errno = ENOTSUP;
        return -1;
    }

    scoped_lock_t lock (_sync);
    _peer_endpoints.erase (peer_pub_endpoint_);
    return _sub_threadsafe->term_endpoint (peer_pub_endpoint_);
}

int spot_node_t::register_node (const char *service_name_,
                                const char *advertise_endpoint_)
{
    if (!_dealer_threadsafe) {
        errno = ENOTSUP;
        return -1;
    }

    std::string service = service_name_ && service_name_[0] != '\0'
                            ? service_name_
                            : "spot-node";
    if (!validate_service_name (service)) {
        errno = EINVAL;
        return -1;
    }

    std::string advertise;
    if (advertise_endpoint_ && advertise_endpoint_[0] != '\0') {
        advertise = advertise_endpoint_;
    } else {
        scoped_lock_t lock (_sync);
        if (_bind_endpoints.size () != 1) {
            errno = EINVAL;
            return -1;
        }
        advertise = resolve_advertise (_bind_endpoints[0]);
    }
    if (advertise.empty ()) {
        errno = EINVAL;
        return -1;
    }

    if (send_u16_threadsafe (_dealer_threadsafe,
                             discovery_protocol::msg_register, ZMQ_SNDMORE)
          != 0
        || send_string_threadsafe (_dealer_threadsafe, service, ZMQ_SNDMORE)
             != 0
        || send_string_threadsafe (_dealer_threadsafe, advertise,
                                   ZMQ_SNDMORE)
             != 0
        || send_u32_threadsafe (_dealer_threadsafe, 1, 0) != 0)
        return -1;

    zmq_msg_t reply;
    zmq_msg_init (&reply);
    if (_dealer_threadsafe->recv (reinterpret_cast<msg_t *> (&reply), 0) != 0) {
        zmq_msg_close (&reply);
        return -1;
    }

    std::vector<zmq_msg_t> frames;
    frames.push_back (reply);
    while (zmq_msg_more (&frames.back ())) {
        zmq_msg_t frame;
        zmq_msg_init (&frame);
        if (_dealer_threadsafe->recv (reinterpret_cast<msg_t *> (&frame), 0)
            != 0) {
            zmq_msg_close (&frame);
            break;
        }
        frames.push_back (frame);
    }

    uint16_t msg_id = 0;
    uint8_t status = 0xFF;
    if (frames.size () >= 2
        && discovery_protocol::read_u16 (frames[0], &msg_id)
        && msg_id == discovery_protocol::msg_register_ack) {
        if (zmq_msg_size (&frames[1]) == sizeof (uint8_t)) {
            memcpy (&status, zmq_msg_data (&frames[1]), sizeof (uint8_t));
        }
    }

    for (size_t i = 0; i < frames.size (); ++i)
        zmq_msg_close (&frames[i]);

    if (status != 0) {
        errno = EINVAL;
        return -1;
    }

    scoped_lock_t lock (_sync);
    _registered = true;
    _service_name = service;
    _advertise_endpoint = advertise;
    _last_heartbeat_ms = 0;
    return 0;
}

int spot_node_t::unregister_node (const char *service_name_)
{
    if (!_dealer_threadsafe) {
        errno = ENOTSUP;
        return -1;
    }

    std::string service = service_name_ && service_name_[0] != '\0'
                            ? service_name_
                            : "spot-node";
    if (!validate_service_name (service)) {
        errno = EINVAL;
        return -1;
    }

    std::string advertise;
    {
        scoped_lock_t lock (_sync);
        advertise = _advertise_endpoint;
        _registered = false;
    }

    if (send_u16_threadsafe (_dealer_threadsafe,
                             discovery_protocol::msg_unregister, ZMQ_SNDMORE)
          != 0
        || send_string_threadsafe (_dealer_threadsafe, service, ZMQ_SNDMORE)
             != 0
        || send_string_threadsafe (_dealer_threadsafe, advertise, 0) != 0)
        return -1;

    return 0;
}

int spot_node_t::set_discovery (discovery_t *discovery_,
                                const char *service_name_)
{
    if (!discovery_) {
        errno = EINVAL;
        return -1;
    }

    std::string service = service_name_ && service_name_[0] != '\0'
                            ? service_name_
                            : "spot-node";
    if (!validate_service_name (service)) {
        errno = EINVAL;
        return -1;
    }

    scoped_lock_t lock (_sync);
    _discovery = discovery_;
    _discovery_service = service;
    _next_discovery_refresh_ms = 0;
    return 0;
}

spot_t *spot_node_t::create_spot (bool threadsafe_)
{
    spot_t *spot = new (std::nothrow) spot_t (this, threadsafe_);
    if (!spot) {
        errno = ENOMEM;
        return NULL;
    }

    scoped_lock_t lock (_sync);
    _spots.insert (spot);
    return spot;
}

void spot_node_t::remove_spot (spot_t *spot_)
{
    if (!spot_)
        return;

    scoped_lock_t lock (_sync);
    _spots.erase (spot_);

    for (std::set<std::string>::const_iterator it = spot_->_topics.begin ();
         it != spot_->_topics.end (); ++it) {
        remove_filter (*it);
    }

    for (std::set<std::string>::const_iterator it =
           spot_->_patterns.begin ();
         it != spot_->_patterns.end (); ++it) {
        remove_filter (*it);
    }

    spot_->_topics.clear ();
    spot_->_patterns.clear ();
    spot_->_ring_cursors.clear ();
    while (!spot_->_queue.empty ()) {
        spot_t::spot_message_t &msg = spot_->_queue.front ();
        close_parts (&msg.parts);
        spot_->_queue.pop_front ();
    }
}

int spot_node_t::topic_create (const char *topic_, int mode_)
{
    std::string topic;
    if (!validate_topic (topic_, &topic)) {
        errno = EINVAL;
        return -1;
    }
    if (mode_ != ZMQ_SPOT_TOPIC_QUEUE
        && mode_ != ZMQ_SPOT_TOPIC_RINGBUFFER) {
        errno = EINVAL;
        return -1;
    }

    scoped_lock_t lock (_sync);
    if (_topics.find (topic) != _topics.end ()) {
        errno = EEXIST;
        return -1;
    }

    topic_state_t state;
    state.mode = mode_;
    _topics.insert (std::make_pair (topic, state));
    return 0;
}

int spot_node_t::topic_destroy (const char *topic_)
{
    std::string topic;
    if (!validate_topic (topic_, &topic)) {
        errno = EINVAL;
        return -1;
    }

    scoped_lock_t lock (_sync);
    std::map<std::string, topic_state_t>::iterator it = _topics.find (topic);
    if (it == _topics.end ()) {
        errno = ENOENT;
        return -1;
    }
    for (std::deque<std::vector<msg_t> >::iterator rit =
           it->second.ring.entries.begin ();
         rit != it->second.ring.entries.end (); ++rit)
        close_parts (&(*rit));
    it->second.ring.entries.clear ();
    _topics.erase (it);

    for (std::set<spot_t *>::iterator sit = _spots.begin ();
         sit != _spots.end (); ++sit) {
        (*sit)->_ring_cursors.erase (topic);
    }
    return 0;
}

int spot_node_t::subscribe (spot_t *spot_, const char *topic_)
{
    if (!spot_) {
        errno = EINVAL;
        return -1;
    }
    std::string topic;
    if (!validate_topic (topic_, &topic)) {
        errno = EINVAL;
        return -1;
    }

    scoped_lock_t lock (_sync);
    if (!spot_->_topics.insert (topic).second)
        return 0;

    add_filter (topic);
    if (topic_is_ringbuffer (topic)) {
        std::map<std::string, topic_state_t>::iterator it = _topics.find (topic);
        if (it != _topics.end ()) {
            const uint64_t end_seq = it->second.ring.start_seq
                                     + it->second.ring.entries.size ();
            spot_->_ring_cursors[topic] = end_seq;
        }
    }
    return 0;
}

int spot_node_t::subscribe_pattern (spot_t *spot_, const char *pattern_)
{
    if (!spot_) {
        errno = EINVAL;
        return -1;
    }
    std::string prefix;
    if (!validate_pattern (pattern_, &prefix)) {
        errno = EINVAL;
        return -1;
    }

    scoped_lock_t lock (_sync);
    if (!spot_->_patterns.insert (prefix).second)
        return 0;
    add_filter (prefix);
    return 0;
}

int spot_node_t::unsubscribe (spot_t *spot_, const char *topic_or_pattern_)
{
    if (!spot_ || !topic_or_pattern_) {
        errno = EINVAL;
        return -1;
    }

    std::string prefix;
    if (validate_pattern (topic_or_pattern_, &prefix)) {
        scoped_lock_t lock (_sync);
        if (spot_->_patterns.erase (prefix) == 0) {
            errno = EINVAL;
            return -1;
        }
        remove_filter (prefix);
        return 0;
    }

    std::string topic;
    if (!validate_topic (topic_or_pattern_, &topic)) {
        errno = EINVAL;
        return -1;
    }

    scoped_lock_t lock (_sync);
    if (spot_->_topics.erase (topic) == 0) {
        errno = EINVAL;
        return -1;
    }
    spot_->_ring_cursors.erase (topic);
    remove_filter (topic);
    return 0;
}

int spot_node_t::publish (spot_t *spot_,
                          const char *topic_,
                          zmq_msg_t *parts_,
                          size_t part_count_,
                          int flags_)
{
    if (!spot_) {
        errno = EINVAL;
        return -1;
    }
    std::string topic;
    if (!validate_topic (topic_, &topic)) {
        errno = EINVAL;
        return -1;
    }
    if (!parts_ || part_count_ == 0) {
        errno = EINVAL;
        return -1;
    }
    if (flags_ != 0) {
        errno = ENOTSUP;
        return -1;
    }

    std::vector<msg_t> payload;
    if (!copy_parts_from_msgv (parts_, part_count_, &payload))
        return -1;

    {
        scoped_lock_t lock (_sync);
        dispatch_local (topic, payload);
    }

    if (_pub_threadsafe) {
        msg_t topic_frame;
        if (topic_frame.init_size (topic.size ()) != 0) {
            close_parts (&payload);
            return -1;
        }
        if (!topic.empty ())
            memcpy (topic_frame.data (), topic.data (), topic.size ());

        int flags = part_count_ > 0 ? ZMQ_SNDMORE : 0;
        if (_pub_threadsafe->send (&topic_frame, flags) != 0) {
            topic_frame.close ();
            close_parts (&payload);
            return -1;
        }
        topic_frame.close ();

        for (size_t i = 0; i < part_count_; ++i) {
            msg_t &part = *reinterpret_cast<msg_t *> (&parts_[i]);
            flags = (i + 1 < part_count_) ? ZMQ_SNDMORE : 0;
            if (_pub_threadsafe->send (&part, flags) != 0) {
                close_parts (&payload);
                return -1;
            }
        }
    }

    close_parts (&payload);
    for (size_t i = 0; i < part_count_; ++i)
        zmq_msg_close (&parts_[i]);

    return 0;
}

void spot_node_t::dispatch_local (const std::string &topic_,
                                  const std::vector<msg_t> &payload_)
{
    const bool ringbuffer = topic_is_ringbuffer (topic_);

    if (ringbuffer) {
        ensure_ringbuffer_topic (topic_);
        topic_state_t &state = _topics[topic_];
        std::vector<msg_t> entry_parts;
        if (copy_parts_from_vec (payload_, &entry_parts)) {
            state.ring.entries.push_back (std::vector<msg_t> ());
            state.ring.entries.back ().swap (entry_parts);
            if (state.ring.entries.size () > state.ring.hwm) {
                close_parts (&state.ring.entries.front ());
                state.ring.entries.pop_front ();
                state.ring.start_seq++;
            }

            for (std::set<spot_t *>::iterator sit = _spots.begin ();
                 sit != _spots.end (); ++sit) {
                std::map<std::string, uint64_t>::iterator cit =
                  (*sit)->_ring_cursors.find (topic_);
                if (cit != (*sit)->_ring_cursors.end ()) {
                    if (cit->second < state.ring.start_seq)
                        cit->second = state.ring.start_seq;
                    (*sit)->_cv.broadcast ();
                }
            }
        }
    }

    for (std::set<spot_t *>::iterator it = _spots.begin (); it != _spots.end ();
         ++it) {
        spot_t *spot = *it;
        if (!spot->matches (topic_))
            continue;
        if (ringbuffer && spot->_ring_cursors.count (topic_))
            continue;
        spot->enqueue_message (topic_, payload_);
    }
}

void spot_node_t::process_sub ()
{
    if (!_sub_threadsafe)
        return;

    while (true) {
        msg_t topic_frame;
        if (topic_frame.init () != 0)
            return;
        if (_sub_threadsafe->recv (&topic_frame, ZMQ_DONTWAIT) != 0) {
            topic_frame.close ();
            return;
        }

        const bool more = (topic_frame.flags () & msg_t::more) != 0;
        std::string topic;
        if (topic_frame.size () > 0) {
            const char *data = static_cast<const char *> (topic_frame.data ());
            topic.assign (data, data + topic_frame.size ());
        }
        topic_frame.close ();

        if (!more)
            continue;

        std::vector<msg_t> payload;
        bool has_more = more;
        while (has_more) {
            msg_t part;
            if (part.init () != 0) {
                close_parts (&payload);
                break;
            }
            if (_sub_threadsafe->recv (&part, 0) != 0) {
                part.close ();
                close_parts (&payload);
                break;
            }
            has_more = (part.flags () & msg_t::more) != 0;
            payload.push_back (msg_t ());
            payload.back ().init ();
            payload.back ().move (part);
        }

        if (!topic.empty ()) {
            scoped_lock_t lock (_sync);
            dispatch_local (topic, payload);
        }
        close_parts (&payload);
    }
}

void spot_node_t::refresh_peers ()
{
    discovery_t *disc = NULL;
    std::string service;
    {
        scoped_lock_t lock (_sync);
        disc = _discovery;
        service = _discovery_service;
    }
    if (!disc || !_sub_threadsafe)
        return;

    std::vector<provider_info_t> providers;
    disc->snapshot_providers (service, &providers);

    std::set<std::string> next;
    for (size_t i = 0; i < providers.size (); ++i) {
        const provider_info_t &entry = providers[i];
        if (entry.endpoint.empty ())
            continue;
        if (entry.routing_id.size == _routing_id.size
            && memcmp (entry.routing_id.data, _routing_id.data,
                       _routing_id.size)
                 == 0)
            continue;
        next.insert (entry.endpoint);
    }

    std::vector<std::string> to_connect;
    std::vector<std::string> to_disconnect;
    {
        scoped_lock_t lock (_sync);
        for (std::set<std::string>::iterator it = next.begin ();
             it != next.end (); ++it) {
            if (_peer_endpoints.count (*it) == 0)
                to_connect.push_back (*it);
        }
        for (std::set<std::string>::iterator it = _peer_endpoints.begin ();
             it != _peer_endpoints.end (); ++it) {
            if (next.count (*it) == 0)
                to_disconnect.push_back (*it);
        }
        _peer_endpoints = next;
    }

    for (size_t i = 0; i < to_connect.size (); ++i)
        _sub_threadsafe->connect (to_connect[i].c_str ());
    for (size_t i = 0; i < to_disconnect.size (); ++i)
        _sub_threadsafe->term_endpoint (to_disconnect[i].c_str ());
}

void spot_node_t::send_heartbeat (uint64_t now_ms_)
{
    thread_safe_socket_t *dealer = NULL;
    std::string service;
    std::string endpoint;
    {
        scoped_lock_t lock (_sync);
        if (!_registered || !_dealer_threadsafe)
            return;
        dealer = _dealer_threadsafe;
        service = _service_name;
        endpoint = _advertise_endpoint;
        _last_heartbeat_ms = now_ms_;
    }

    send_u16_threadsafe (dealer, discovery_protocol::msg_heartbeat,
                         ZMQ_SNDMORE);
    send_string_threadsafe (dealer, service, ZMQ_SNDMORE);
    send_string_threadsafe (dealer, endpoint, 0);
}

void spot_node_t::run (void *arg_)
{
    spot_node_t *self = static_cast<spot_node_t *> (arg_);
    self->loop ();
}

void spot_node_t::loop ()
{
    zmq::clock_t clock;
    while (_stop.get () == 0) {
        bool handled = false;

        process_sub ();

        const uint64_t now = clock.now_ms ();
        bool do_heartbeat = false;
        bool do_refresh = false;
        {
            scoped_lock_t lock (_sync);
            if (_registered
                && (now - _last_heartbeat_ms) >= _heartbeat_interval_ms)
                do_heartbeat = true;
            if (_discovery && now >= _next_discovery_refresh_ms) {
                _next_discovery_refresh_ms = now + discovery_refresh_ms;
                do_refresh = true;
            }
        }

        if (do_heartbeat) {
            send_heartbeat (now);
            handled = true;
        }

        if (do_refresh) {
            refresh_peers ();
            handled = true;
        }

        if (!handled)
            sleep_ms (1);
    }
}

int spot_node_t::destroy ()
{
    _stop.set (1);
    if (_worker.get_started ())
        _worker.stop ();

    if (_dealer_threadsafe) {
        _dealer_threadsafe->close ();
        if (_dealer)
            _dealer->set_threadsafe_proxy (NULL);
        delete _dealer_threadsafe;
        _dealer_threadsafe = NULL;
        _dealer = NULL;
    } else if (_dealer) {
        _dealer->close ();
        _dealer = NULL;
    }

    if (_pub_threadsafe) {
        _pub_threadsafe->close ();
        if (_pub)
            _pub->set_threadsafe_proxy (NULL);
        delete _pub_threadsafe;
        _pub_threadsafe = NULL;
        _pub = NULL;
    } else if (_pub) {
        _pub->close ();
        _pub = NULL;
    }

    if (_sub_threadsafe) {
        _sub_threadsafe->close ();
        if (_sub)
            _sub->set_threadsafe_proxy (NULL);
        delete _sub_threadsafe;
        _sub_threadsafe = NULL;
        _sub = NULL;
    } else if (_sub) {
        _sub->close ();
        _sub = NULL;
    }

    scoped_lock_t lock (_sync);
    _spots.clear ();
    for (std::map<std::string, topic_state_t>::iterator it = _topics.begin ();
         it != _topics.end (); ++it) {
        for (std::deque<std::vector<msg_t> >::iterator rit =
               it->second.ring.entries.begin ();
             rit != it->second.ring.entries.end (); ++rit)
            close_parts (&(*rit));
        it->second.ring.entries.clear ();
    }
    _topics.clear ();
    _filter_refcount.clear ();
    _peer_endpoints.clear ();
    _registry_endpoints.clear ();
    _bind_endpoints.clear ();
    return 0;
}
}
