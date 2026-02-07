/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "discovery/registry.hpp"
#include "discovery/protocol.hpp"

#include "utils/err.hpp"
#include "utils/random.hpp"

#include <algorithm>
#include <set>
#include <vector>
#include <cstdio>

namespace zlink
{
static const uint32_t registry_tag_value = 0x1e6700d5;

static void registry_debug (const char *msg_)
{
    if (std::getenv ("ZLINK_REGISTRY_DEBUG"))
        std::fprintf (stderr, "[registry] %s\n", msg_ ? msg_ : "");
}

static void registry_debug_rid (const char *label_,
                                const zlink_routing_id_t &rid_)
{
    if (!std::getenv ("ZLINK_REGISTRY_DEBUG"))
        return;
    std::fprintf (stderr, "[registry] %s rid(size=%u):", label_,
                  static_cast<unsigned int> (rid_.size));
    for (uint8_t i = 0; i < rid_.size; ++i)
        std::fprintf (stderr, " %02x",
                      static_cast<unsigned int> (rid_.data[i]));
    std::fprintf (stderr, "\n");
}

registry_t::registry_t (ctx_t *ctx_) :
    _ctx (ctx_),
    _tag (registry_tag_value),
    _registry_id (0),
    _registry_id_set (false),
    _list_seq (0),
    _heartbeat_interval_ms (5000),
    _heartbeat_timeout_ms (15000),
    _broadcast_interval_ms (30000),
    _stop (0)
{
    zlink_assert (_ctx);
}

registry_t::~registry_t ()
{
    _tag = 0xdeadbeef;
}

bool registry_t::check_tag () const
{
    return _tag == registry_tag_value;
}

int registry_t::set_endpoints (const char *pub_endpoint_,
                               const char *router_endpoint_)
{
    if (!pub_endpoint_ || !router_endpoint_) {
        errno = EINVAL;
        return -1;
    }
    scoped_lock_t lock (_sync);
    _pub_endpoint = pub_endpoint_;
    _router_endpoint = router_endpoint_;
    return 0;
}

int registry_t::set_id (uint32_t registry_id_)
{
    scoped_lock_t lock (_sync);
    _registry_id = registry_id_;
    _registry_id_set = true;
    return 0;
}

int registry_t::add_peer (const char *peer_pub_endpoint_)
{
    if (!peer_pub_endpoint_) {
        errno = EINVAL;
        return -1;
    }
    scoped_lock_t lock (_sync);
    _peer_pubs.push_back (peer_pub_endpoint_);
    return 0;
}

int registry_t::set_heartbeat (uint32_t interval_ms_, uint32_t timeout_ms_)
{
    if (interval_ms_ == 0 || timeout_ms_ == 0) {
        errno = EINVAL;
        return -1;
    }
    scoped_lock_t lock (_sync);
    _heartbeat_interval_ms = interval_ms_;
    _heartbeat_timeout_ms = timeout_ms_;
    return 0;
}

int registry_t::set_broadcast_interval (uint32_t interval_ms_)
{
    if (interval_ms_ == 0) {
        errno = EINVAL;
        return -1;
    }
    scoped_lock_t lock (_sync);
    _broadcast_interval_ms = interval_ms_;
    return 0;
}

int registry_t::set_socket_option (int socket_role_,
                                   int option_,
                                   const void *optval_,
                                   size_t optvallen_)
{
    if (!optval_ || optvallen_ == 0) {
        errno = EINVAL;
        return -1;
    }

    scoped_lock_t lock (_sync);
    std::vector<socket_opt_t> *opts = NULL;
    switch (socket_role_) {
        case ZLINK_REGISTRY_SOCKET_PUB:
            opts = &_pub_opts;
            break;
        case ZLINK_REGISTRY_SOCKET_ROUTER:
            opts = &_router_opts;
            break;
        case ZLINK_REGISTRY_SOCKET_PEER_SUB:
            opts = &_peer_sub_opts;
            break;
        default:
            errno = EINVAL;
            return -1;
    }
    for (size_t i = 0; i < opts->size (); ++i) {
        if ((*opts)[i].option == option_) {
            (*opts)[i].value.assign (
              static_cast<const unsigned char *> (optval_),
              static_cast<const unsigned char *> (optval_) + optvallen_);
            return 0;
        }
    }
    socket_opt_t opt;
    opt.option = option_;
    opt.value.assign (static_cast<const unsigned char *> (optval_),
                      static_cast<const unsigned char *> (optval_)
                        + optvallen_);
    opts->push_back (opt);
    return 0;
}

int registry_t::start ()
{
    scoped_lock_t lock (_sync);
    if (_pub_endpoint.empty () || _router_endpoint.empty ()) {
        errno = EINVAL;
        return -1;
    }
    if (_stop.get () == 0 && _worker.get_started ())
        return 0;

    _stop.set (0);
    _worker.start (run, this, "registry");
    return 0;
}

int registry_t::destroy ()
{
    stop_worker ();
    return 0;
}

void registry_t::stop_worker ()
{
    _stop.set (1);
    if (_worker.get_started ())
        _worker.stop ();
}

void registry_t::run (void *arg_)
{
    registry_t *self = static_cast<registry_t *> (arg_);
    self->loop ();
}

void registry_t::loop ()
{
    std::string pub_endpoint;
    std::string router_endpoint;
    std::vector<std::string> peer_pubs;
    uint32_t registry_id = 0;
    bool registry_id_set = false;

    {
        scoped_lock_t lock (_sync);
        pub_endpoint = _pub_endpoint;
        router_endpoint = _router_endpoint;
        peer_pubs = _peer_pubs;
        registry_id = _registry_id;
        registry_id_set = _registry_id_set;
    }

    void *pub = zlink_socket (static_cast<void *> (_ctx), ZLINK_XPUB);
    void *router = zlink_socket (static_cast<void *> (_ctx), ZLINK_ROUTER);
    void *peer_sub = NULL;
    std::set<std::string> peer_connected;

    if (!pub || !router) {
        if (pub)
            zlink_close (pub);
        if (router)
            zlink_close (router);
        return;
    }

    std::vector<socket_opt_t> pub_opts;
    std::vector<socket_opt_t> router_opts;
    std::vector<socket_opt_t> peer_sub_opts;
    {
        scoped_lock_t lock (_sync);
        pub_opts = _pub_opts;
        router_opts = _router_opts;
        peer_sub_opts = _peer_sub_opts;
    }

    for (size_t i = 0; i < pub_opts.size (); ++i) {
        if (!pub_opts[i].value.empty ())
            zlink_setsockopt (pub, pub_opts[i].option, &pub_opts[i].value[0],
                              pub_opts[i].value.size ());
    }
    for (size_t i = 0; i < router_opts.size (); ++i) {
        if (!router_opts[i].value.empty ())
            zlink_setsockopt (router, router_opts[i].option,
                              &router_opts[i].value[0],
                              router_opts[i].value.size ());
    }

    int verbose = 1;
    zlink_setsockopt (pub, ZLINK_XPUB_VERBOSE, &verbose, sizeof (verbose));
    if (std::getenv ("ZLINK_REGISTRY_DEBUG")) {
        int mandatory = 1;
        zlink_setsockopt (router, ZLINK_ROUTER_MANDATORY, &mandatory,
                          sizeof (mandatory));
    }

    if (zlink_bind (pub, pub_endpoint.c_str ()) != 0
        || zlink_bind (router, router_endpoint.c_str ()) != 0) {
        zlink_close (pub);
        zlink_close (router);
        return;
    }

    if (!peer_pubs.empty ()) {
        peer_sub = zlink_socket (static_cast<void *> (_ctx), ZLINK_SUB);
        if (peer_sub) {
            for (size_t i = 0; i < peer_sub_opts.size (); ++i) {
                if (!peer_sub_opts[i].value.empty ())
                    zlink_setsockopt (peer_sub,
                                      peer_sub_opts[i].option,
                                      &peer_sub_opts[i].value[0],
                                      peer_sub_opts[i].value.size ());
            }
            zlink_setsockopt (peer_sub, ZLINK_SUBSCRIBE, "", 0);
            for (size_t i = 0; i < peer_pubs.size (); ++i) {
                zlink_connect (peer_sub, peer_pubs[i].c_str ());
                peer_connected.insert (peer_pubs[i]);
            }
        }
    }

    if (!registry_id_set) {
        registry_id = zlink::generate_random ();
        if (registry_id == 0)
            registry_id = 1;
        scoped_lock_t lock (_sync);
        _registry_id = registry_id;
        _registry_id_set = true;
    }

    zlink::clock_t clock;
    uint64_t next_broadcast = clock.now_ms () + _broadcast_interval_ms;
    uint64_t last_sent_seq = _list_seq;

    while (_stop.get () == 0) {
        {
            scoped_lock_t lock (_sync);
            peer_pubs = _peer_pubs;
        }
        if (!peer_pubs.empty () && !peer_sub) {
            peer_sub = zlink_socket (static_cast<void *> (_ctx), ZLINK_SUB);
            if (peer_sub)
                zlink_setsockopt (peer_sub, ZLINK_SUBSCRIBE, "", 0);
        }
        if (peer_sub) {
            for (size_t i = 0; i < peer_pubs.size (); ++i) {
                const std::string &endpoint = peer_pubs[i];
                if (peer_connected.find (endpoint) == peer_connected.end ()) {
                    zlink_connect (peer_sub, endpoint.c_str ());
                    peer_connected.insert (endpoint);
                }
            }
        }

        zlink_pollitem_t items[3];
        int item_count = 0;
        items[item_count].socket = router;
        items[item_count].fd = 0;
        items[item_count].events = ZLINK_POLLIN;
        items[item_count].revents = 0;
        item_count++;
        items[item_count].socket = pub;
        items[item_count].fd = 0;
        items[item_count].events = ZLINK_POLLIN;
        items[item_count].revents = 0;
        item_count++;
        if (peer_sub) {
            items[item_count].socket = peer_sub;
            items[item_count].fd = 0;
            items[item_count].events = ZLINK_POLLIN;
            items[item_count].revents = 0;
            item_count++;
        }

        const int rc = zlink_poll (items, item_count, 100);
        if (rc > 0) {
            int idx = 0;
            if (items[idx].revents & ZLINK_POLLIN)
                handle_router (router);
            idx++;
            if (items[idx].revents & ZLINK_POLLIN) {
                while (true) {
                    zlink_msg_t submsg;
                    zlink_msg_init (&submsg);
                    if (zlink_msg_recv (&submsg, pub, ZLINK_DONTWAIT) == -1) {
                        zlink_msg_close (&submsg);
                        break;
                    }
                    if (zlink_msg_size (&submsg) > 0) {
                        unsigned char *data = static_cast<unsigned char *> (
                          zlink_msg_data (&submsg));
                        if (data && data[0] == 1)
                            send_service_list (pub);
                    }
                    zlink_msg_close (&submsg);
                }
            }
            idx++;
            if (peer_sub && (items[idx].revents & ZLINK_POLLIN))
                handle_peer (peer_sub);
        }

        const uint64_t now = clock.now_ms ();
        remove_expired (now);
        if (_list_seq != last_sent_seq) {
            send_service_list (pub);
            last_sent_seq = _list_seq;
            next_broadcast = now + _broadcast_interval_ms;
        } else if (now >= next_broadcast) {
            send_service_list (pub);
            next_broadcast = now + _broadcast_interval_ms;
        }
    }

    if (peer_sub)
        zlink_close (peer_sub);
    zlink_close (router);
    zlink_close (pub);
}

void registry_t::handle_router (void *router_)
{
    zlink_msg_t msg;
    zlink_msg_init (&msg);
    if (zlink_msg_recv (&msg, router_, 0) == -1) {
        zlink_msg_close (&msg);
        return;
    }

    zlink_routing_id_t sender;
    sender.size = 0;
    discovery_protocol::read_routing_id (msg, &sender);
    zlink_msg_close (&msg);
    registry_debug ("handle_router recv");
    registry_debug_rid ("sender", sender);

    std::vector<zlink_msg_t> frames;
    while (true) {
        zlink_msg_t frame;
        zlink_msg_init (&frame);
        if (zlink_msg_recv (&frame, router_, 0) == -1) {
            zlink_msg_close (&frame);
            break;
        }
        if (std::getenv ("ZLINK_REGISTRY_DEBUG")) {
            std::fprintf (stderr, "[registry] frame size=%zu more=%d\n",
                          zlink_msg_size (&frame),
                          zlink_msg_more (&frame));
        }
        frames.push_back (frame);
        if (!zlink_msg_more (&frame))
            break;
    }

    if (frames.empty ()) {
        for (size_t i = 0; i < frames.size (); ++i)
            zlink_msg_close (&frames[i]);
        return;
    }

    uint16_t msg_id = 0;
    if (!discovery_protocol::read_u16 (frames[0], &msg_id)) {
        for (size_t i = 0; i < frames.size (); ++i)
            zlink_msg_close (&frames[i]);
        return;
    }

    if (std::getenv ("ZLINK_REGISTRY_DEBUG")) {
        std::fprintf (stderr, "[registry] msg_id=0x%04x frames=%zu\n",
                      msg_id, frames.size ());
    }

    switch (msg_id) {
        case discovery_protocol::msg_register:
            handle_register (router_, &frames[0], frames.size (), sender);
            break;
        case discovery_protocol::msg_unregister:
            handle_unregister (&frames[0], frames.size ());
            break;
        case discovery_protocol::msg_heartbeat:
            handle_heartbeat (&frames[0], frames.size ());
            break;
        case discovery_protocol::msg_update_weight:
            handle_update_weight (router_, &frames[0], frames.size (), sender);
            break;
        default:
            break;
    }

    for (size_t i = 0; i < frames.size (); ++i)
        zlink_msg_close (&frames[i]);
}

void registry_t::handle_peer (void *sub_)
{
    std::vector<zlink_msg_t> frames;
    while (true) {
        zlink_msg_t frame;
        zlink_msg_init (&frame);
        if (zlink_msg_recv (&frame, sub_, 0) == -1) {
            zlink_msg_close (&frame);
            break;
        }
        frames.push_back (frame);
        if (!zlink_msg_more (&frame))
            break;
    }

    if (frames.empty ())
        return;

    uint16_t msg_id = 0;
    if (!discovery_protocol::read_u16 (frames[0], &msg_id)) {
        for (size_t i = 0; i < frames.size (); ++i)
            zlink_msg_close (&frames[i]);
        return;
    }

    if (msg_id != discovery_protocol::msg_service_list
        && msg_id != discovery_protocol::msg_registry_sync) {
        for (size_t i = 0; i < frames.size (); ++i)
            zlink_msg_close (&frames[i]);
        return;
    }

    if (frames.size () < 4) {
        for (size_t i = 0; i < frames.size (); ++i)
            zlink_msg_close (&frames[i]);
        return;
    }

    uint32_t peer_registry_id = 0;
    uint64_t list_seq = 0;
    uint32_t service_count = 0;
    if (!discovery_protocol::read_u32 (frames[1], &peer_registry_id)
        || !discovery_protocol::read_u64 (frames[2], &list_seq)
        || !discovery_protocol::read_u32 (frames[3], &service_count)) {
        for (size_t i = 0; i < frames.size (); ++i)
            zlink_msg_close (&frames[i]);
        return;
    }

    service_map_t incoming;
    zlink::clock_t clock;
    const uint64_t now = clock.now_ms ();

    uint32_t local_registry_id = 0;
    {
        scoped_lock_t lock (_sync);
        local_registry_id = _registry_id;
        if (local_registry_id == 0)
            local_registry_id = 1;

        if (peer_registry_id == local_registry_id) {
            for (size_t i = 0; i < frames.size (); ++i)
                zlink_msg_close (&frames[i]);
            return;
        }

        _peer_last_seen[peer_registry_id] = now;
        std::map<uint32_t, uint64_t>::iterator it =
          _peer_seq.find (peer_registry_id);
        if (it != _peer_seq.end () && list_seq <= it->second) {
            for (size_t i = 0; i < frames.size (); ++i)
                zlink_msg_close (&frames[i]);
            return;
        }
    }

    size_t index = 4;
    for (uint32_t i = 0; i < service_count && index < frames.size (); ++i) {
        if (index + 1 >= frames.size ())
            break;
        const std::string service_name =
          discovery_protocol::read_string (frames[index++]);
        uint32_t provider_count = 0;
        if (!discovery_protocol::read_u32 (frames[index++], &provider_count))
            break;

        service_entry_t &service = incoming[service_name];
        for (uint32_t p = 0; p < provider_count && index + 2 < frames.size ();
             ++p) {
            provider_entry_t entry;
            entry.endpoint = discovery_protocol::read_string (frames[index++]);
            discovery_protocol::read_routing_id (frames[index++],
                                                 &entry.routing_id);
            uint32_t weight = 1;
            discovery_protocol::read_u32 (frames[index++], &weight);
            entry.weight = weight == 0 ? 1 : weight;
            entry.registered_at = now;
            entry.last_heartbeat = now;
            entry.source_registry = peer_registry_id;
            if (!entry.endpoint.empty ())
                service.providers[entry.endpoint] = entry;
        }
    }

    bool changed = false;
    {
        scoped_lock_t lock (_sync);
        std::map<uint32_t, uint64_t>::iterator it =
          _peer_seq.find (peer_registry_id);
        if (peer_registry_id == local_registry_id
            || (it != _peer_seq.end () && list_seq <= it->second)) {
            for (size_t i = 0; i < frames.size (); ++i)
                zlink_msg_close (&frames[i]);
            return;
        }

        for (service_map_t::const_iterator sit = incoming.begin ();
             sit != incoming.end (); ++sit) {
            const std::string &service_name = sit->first;
            const provider_map_t &providers = sit->second.providers;
            service_map_t::const_iterator existing_service =
              _services.find (service_name);
            for (provider_map_t::const_iterator pit = providers.begin ();
                 pit != providers.end (); ++pit) {
                bool match = false;
                if (existing_service != _services.end ()) {
                    provider_map_t::const_iterator ep =
                      existing_service->second.providers.find (pit->first);
                    if (ep != existing_service->second.providers.end ()
                        && ep->second.source_registry == peer_registry_id) {
                        const provider_entry_t &cur = ep->second;
                        const provider_entry_t &incoming_entry = pit->second;
                        match =
                          cur.weight == incoming_entry.weight
                          && cur.routing_id.size
                               == incoming_entry.routing_id.size
                          && (cur.routing_id.size == 0
                              || memcmp (cur.routing_id.data,
                                         incoming_entry.routing_id.data,
                                         cur.routing_id.size)
                                   == 0);
                    } else if (ep != existing_service->second.providers.end ()
                               && ep->second.source_registry
                                    != peer_registry_id) {
                        match = true;
                    }
                }
                if (!match) {
                    changed = true;
                    break;
                }
            }
            if (changed)
                break;
        }

        if (!changed) {
            for (service_map_t::const_iterator sit = _services.begin ();
                 sit != _services.end (); ++sit) {
                const provider_map_t &providers = sit->second.providers;
                for (provider_map_t::const_iterator pit = providers.begin ();
                     pit != providers.end (); ++pit) {
                    if (pit->second.source_registry != peer_registry_id)
                        continue;
                    service_map_t::const_iterator incoming_service =
                      incoming.find (sit->first);
                    if (incoming_service == incoming.end ()
                        || incoming_service->second.providers.find (pit->first)
                             == incoming_service->second.providers.end ()) {
                        changed = true;
                        break;
                    }
                }
                if (changed)
                    break;
            }
        }

        if (!changed) {
            _peer_seq[peer_registry_id] = list_seq;
            for (size_t i = 0; i < frames.size (); ++i)
                zlink_msg_close (&frames[i]);
            return;
        }

        for (service_map_t::iterator sit = _services.begin ();
             sit != _services.end ();) {
            provider_map_t &providers = sit->second.providers;
            for (provider_map_t::iterator pit = providers.begin ();
                 pit != providers.end ();) {
                if (pit->second.source_registry == peer_registry_id) {
                    pit = providers.erase (pit);
                    continue;
                }
                ++pit;
            }
            if (providers.empty ()) {
                sit = _services.erase (sit);
                continue;
            }
            ++sit;
        }

        for (service_map_t::const_iterator sit = incoming.begin ();
             sit != incoming.end (); ++sit) {
            const std::string &service_name = sit->first;
            const provider_map_t &providers = sit->second.providers;
            service_entry_t &service = _services[service_name];
            for (provider_map_t::const_iterator pit = providers.begin ();
                 pit != providers.end (); ++pit) {
                provider_map_t::iterator existing =
                  service.providers.find (pit->first);
                if (existing != service.providers.end ()
                    && existing->second.source_registry != peer_registry_id) {
                    continue;
                }
                service.providers[pit->first] = pit->second;
            }
        }

        _peer_seq[peer_registry_id] = list_seq;
        _list_seq++;
    }

    for (size_t i = 0; i < frames.size (); ++i)
        zlink_msg_close (&frames[i]);
}

void registry_t::handle_register (void *router_, const zlink_msg_t *frames_,
                                  size_t frame_count_,
                                  const zlink_routing_id_t &sender_id_)
{
    if (frame_count_ < 3) {
        send_register_ack (router_, sender_id_, 0xFF, std::string (),
                           "invalid register");
        return;
    }

    const std::string service_name =
      discovery_protocol::read_string (frames_[1]);
    const std::string endpoint =
      discovery_protocol::read_string (frames_[2]);

    if (service_name.empty () || endpoint.empty ()) {
        send_register_ack (router_, sender_id_, 0x02, endpoint,
                           "invalid endpoint");
        return;
    }

    uint32_t weight = 1;
    if (frame_count_ >= 4)
        discovery_protocol::read_u32 (frames_[3], &weight);
    if (weight == 0)
        weight = 1;

    zlink::clock_t clock;
    const uint64_t now = clock.now_ms ();

    service_entry_t &service = _services[service_name];
    provider_entry_t &entry = service.providers[endpoint];
    entry.endpoint = endpoint;
    entry.routing_id = sender_id_;
    entry.weight = weight;
    entry.registered_at = now;
    entry.last_heartbeat = now;
    entry.source_registry = _registry_id;

    _list_seq++;
    send_register_ack (router_, sender_id_, 0x00, endpoint, std::string ());
}

void registry_t::handle_unregister (const zlink_msg_t *frames_,
                                    size_t frame_count_)
{
    if (frame_count_ < 3)
        return;

    const std::string service_name =
      discovery_protocol::read_string (frames_[1]);
    const std::string endpoint =
      discovery_protocol::read_string (frames_[2]);

    service_map_t::iterator sit = _services.find (service_name);
    if (sit == _services.end ())
        return;

    provider_map_t::iterator pit = sit->second.providers.find (endpoint);
    if (pit == sit->second.providers.end ())
        return;
    if (pit->second.source_registry != _registry_id)
        return;

    sit->second.providers.erase (pit);
    if (sit->second.providers.empty ())
        _services.erase (sit);

    _list_seq++;
}

void registry_t::handle_heartbeat (const zlink_msg_t *frames_,
                                   size_t frame_count_)
{
    if (frame_count_ < 3)
        return;

    const std::string service_name =
      discovery_protocol::read_string (frames_[1]);
    const std::string endpoint =
      discovery_protocol::read_string (frames_[2]);

    service_map_t::iterator sit = _services.find (service_name);
    if (sit == _services.end ())
        return;

    provider_map_t::iterator pit = sit->second.providers.find (endpoint);
    if (pit == sit->second.providers.end ())
        return;

    zlink::clock_t clock;
    pit->second.last_heartbeat = clock.now_ms ();
}

void registry_t::handle_update_weight (void *router_, const zlink_msg_t *frames_,
                                       size_t frame_count_,
                                       const zlink_routing_id_t &sender_id_)
{
    if (frame_count_ < 4) {
        send_register_ack (router_, sender_id_, 0xFF, std::string (),
                           "invalid update");
        return;
    }

    const std::string service_name =
      discovery_protocol::read_string (frames_[1]);
    const std::string endpoint =
      discovery_protocol::read_string (frames_[2]);
    uint32_t weight = 1;
    discovery_protocol::read_u32 (frames_[3], &weight);
    if (weight == 0)
        weight = 1;

    service_map_t::iterator sit = _services.find (service_name);
    if (sit == _services.end ()) {
        send_register_ack (router_, sender_id_, 0x01, endpoint,
                           "service not found");
        return;
    }

    provider_map_t::iterator pit = sit->second.providers.find (endpoint);
    if (pit == sit->second.providers.end ()) {
        send_register_ack (router_, sender_id_, 0x01, endpoint,
                           "provider not found");
        return;
    }
    if (pit->second.source_registry != _registry_id) {
        send_register_ack (router_, sender_id_, 0x01, endpoint,
                           "provider not local");
        return;
    }

    pit->second.weight = weight;
    _list_seq++;
    send_register_ack (router_, sender_id_, 0x00, endpoint, std::string ());
}

void registry_t::send_register_ack (void *router_,
                                    const zlink_routing_id_t &sender_id_,
                                    uint8_t status_,
                                    const std::string &endpoint_,
                                    const std::string &error_)
{
    registry_debug ("send_register_ack");
    registry_debug_rid ("ack target", sender_id_);
    auto log_rc = [] (const char *label_, int rc_) {
        if (!std::getenv ("ZLINK_REGISTRY_DEBUG"))
            return;
        if (rc_ == -1) {
            std::fprintf (stderr, "[registry] %s failed errno=%d (%s)\n",
                          label_, errno, std::strerror (errno));
        }
    };
    zlink_msg_t id_frame;
    zlink_msg_init_size (&id_frame, sender_id_.size);
    if (sender_id_.size > 0)
        memcpy (zlink_msg_data (&id_frame), sender_id_.data, sender_id_.size);

    const int rc_id = zlink_msg_send (&id_frame, router_, ZLINK_SNDMORE);
    log_rc ("send ack id", rc_id);
    if (rc_id == -1) {
        zlink_msg_close (&id_frame);
        return;
    }

    log_rc ("send ack msg_id",
            discovery_protocol::send_u16 (
              router_, discovery_protocol::msg_register_ack, ZLINK_SNDMORE));
    log_rc ("send ack status",
            discovery_protocol::send_frame (router_, &status_,
                                            sizeof (status_), ZLINK_SNDMORE));
    log_rc ("send ack endpoint",
            discovery_protocol::send_string (router_, endpoint_,
                                             ZLINK_SNDMORE));
    log_rc ("send ack error",
            discovery_protocol::send_string (router_, error_, 0));
}

void registry_t::send_service_list (void *pub_)
{
    uint32_t registry_id = 0;
    {
        scoped_lock_t lock (_sync);
        registry_id = _registry_id;
        if (registry_id == 0)
            registry_id = 1;
    }

    discovery_protocol::send_u16 (pub_, discovery_protocol::msg_service_list,
                                  ZLINK_SNDMORE);
    discovery_protocol::send_u32 (pub_, registry_id, ZLINK_SNDMORE);
    discovery_protocol::send_u64 (pub_, _list_seq, ZLINK_SNDMORE);

    uint32_t service_count = 0;
    for (service_map_t::const_iterator it = _services.begin ();
         it != _services.end (); ++it) {
        if (!it->second.providers.empty ())
            service_count++;
    }

    discovery_protocol::send_u32 (pub_, service_count,
                                  service_count == 0 ? 0 : ZLINK_SNDMORE);

    if (service_count == 0)
        return;

    uint32_t emitted = 0;
    for (service_map_t::const_iterator it = _services.begin ();
         it != _services.end (); ++it) {
        if (it->second.providers.empty ())
            continue;

        const std::string &service_name = it->first;
        const provider_map_t &providers = it->second.providers;
        const uint32_t provider_count =
          static_cast<uint32_t> (providers.size ());

        discovery_protocol::send_string (pub_, service_name, ZLINK_SNDMORE);
        discovery_protocol::send_u32 (pub_, provider_count,
                                      ZLINK_SNDMORE);

        uint32_t provider_index = 0;
        for (provider_map_t::const_iterator pit = providers.begin ();
             pit != providers.end (); ++pit, ++provider_index) {
            const provider_entry_t &entry = pit->second;
            const bool last_provider =
              (provider_index + 1) == provider_count
              && (emitted + 1) == service_count;

            discovery_protocol::send_string (pub_, entry.endpoint,
                                             ZLINK_SNDMORE);
            discovery_protocol::send_routing_id (pub_, entry.routing_id,
                                                 ZLINK_SNDMORE);
            discovery_protocol::send_u32 (pub_, entry.weight,
                                          last_provider ? 0 : ZLINK_SNDMORE);
        }

        emitted++;
    }
}

void registry_t::remove_expired (uint64_t now_ms_)
{
    const uint32_t local_registry_id = _registry_id;
    bool changed = false;
    for (service_map_t::iterator sit = _services.begin ();
         sit != _services.end ();) {
        provider_map_t &providers = sit->second.providers;
        for (provider_map_t::iterator pit = providers.begin ();
             pit != providers.end ();) {
            if (pit->second.source_registry != local_registry_id) {
                ++pit;
                continue;
            }
            if (now_ms_ > pit->second.last_heartbeat
                && now_ms_ - pit->second.last_heartbeat
                     > _heartbeat_timeout_ms) {
                pit = providers.erase (pit);
                changed = true;
                continue;
            }
            ++pit;
        }
        if (providers.empty ())
            sit = _services.erase (sit);
        else
            ++sit;
    }

    uint64_t peer_timeout_ms = _broadcast_interval_ms;
    if (peer_timeout_ms == 0)
        peer_timeout_ms = 30000;
    peer_timeout_ms *= 3;

    for (std::map<uint32_t, uint64_t>::iterator pit = _peer_last_seen.begin ();
         pit != _peer_last_seen.end ();) {
        const uint32_t peer_id = pit->first;
        if (now_ms_ > pit->second && now_ms_ - pit->second > peer_timeout_ms) {
            for (service_map_t::iterator sit = _services.begin ();
                 sit != _services.end ();) {
                provider_map_t &providers = sit->second.providers;
                for (provider_map_t::iterator eit = providers.begin ();
                     eit != providers.end ();) {
                    if (eit->second.source_registry == peer_id) {
                        eit = providers.erase (eit);
                        changed = true;
                        continue;
                    }
                    ++eit;
                }
                if (providers.empty ()) {
                    sit = _services.erase (sit);
                    continue;
                }
                ++sit;
            }
            _peer_seq.erase (peer_id);
            pit = _peer_last_seen.erase (pit);
            continue;
        }
        ++pit;
    }

    if (changed)
        _list_seq++;
}
}
