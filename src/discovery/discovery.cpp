/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "discovery/discovery.hpp"
#include "discovery/protocol.hpp"

#include "utils/err.hpp"

#include <algorithm>
#include <string.h>

namespace zmq
{
static const uint32_t discovery_tag_value = 0x1e6700d6;

static void close_frames (std::vector<zmq_msg_t> *frames_)
{
    if (!frames_)
        return;
    for (size_t i = 0; i < frames_->size (); ++i)
        zmq_msg_close (&(*frames_)[i]);
    frames_->clear ();
}

discovery_t::discovery_t (ctx_t *ctx_) :
    _ctx (ctx_),
    _tag (discovery_tag_value),
    _stop (0)
{
    zmq_assert (_ctx);
}

discovery_t::~discovery_t ()
{
    _tag = 0xdeadbeef;
}

bool discovery_t::check_tag () const
{
    return _tag == discovery_tag_value;
}

int discovery_t::connect_registry (const char *registry_pub_endpoint_)
{
    if (!registry_pub_endpoint_) {
        errno = EINVAL;
        return -1;
    }

    {
        scoped_lock_t lock (_sync);
        _registry_endpoints.insert (registry_pub_endpoint_);
    }

    if (!_worker.get_started ())
        _worker.start (run, this, "discovery");

    return 0;
}

int discovery_t::subscribe (const char *service_name_)
{
    if (!service_name_ || service_name_[0] == '\0') {
        errno = EINVAL;
        return -1;
    }
    scoped_lock_t lock (_sync);
    _subscriptions.insert (service_name_);
    return 0;
}

int discovery_t::unsubscribe (const char *service_name_)
{
    if (!service_name_ || service_name_[0] == '\0') {
        errno = EINVAL;
        return -1;
    }
    scoped_lock_t lock (_sync);
    _subscriptions.erase (service_name_);
    return 0;
}

void discovery_t::snapshot_providers (const std::string &service_name_,
                                      std::vector<provider_info_t> *out_)
{
    if (!out_)
        return;
    out_->clear ();
    scoped_lock_t lock (_sync);
    if (!_subscriptions.empty ()
        && _subscriptions.find (service_name_) == _subscriptions.end ())
        return;
    std::map<std::string, service_state_t>::iterator it =
      _services.find (service_name_);
    if (it == _services.end ())
        return;
    *out_ = it->second.providers;
}

void discovery_t::add_listener (discovery_listener_t *listener_)
{
    if (!listener_)
        return;
    scoped_lock_t lock (_sync);
    _listeners.insert (listener_);
}

void discovery_t::remove_listener (discovery_listener_t *listener_)
{
    if (!listener_)
        return;
    scoped_lock_t lock (_sync);
    _listeners.erase (listener_);
}

int discovery_t::get_providers (const char *service_name_,
                                zmq_provider_info_t *providers_,
                                size_t *count_)
{
    if (!service_name_ || !count_) {
        errno = EINVAL;
        return -1;
    }

    std::vector<provider_info_t> snapshot;
    snapshot_providers (service_name_, &snapshot);

    const size_t capacity = *count_;
    *count_ = snapshot.size ();
    if (!providers_)
        return 0;

    const size_t copy_count = std::min (capacity, snapshot.size ());
    for (size_t i = 0; i < copy_count; ++i) {
        const provider_info_t &entry = snapshot[i];
        memset (&providers_[i], 0, sizeof (providers_[i]));
        strncpy (providers_[i].service_name, entry.service_name.c_str (),
                 sizeof (providers_[i].service_name) - 1);
        strncpy (providers_[i].endpoint, entry.endpoint.c_str (),
                 sizeof (providers_[i].endpoint) - 1);
        providers_[i].routing_id = entry.routing_id;
        providers_[i].weight = entry.weight;
        providers_[i].registered_at = entry.registered_at;
    }

    return 0;
}

int discovery_t::provider_count (const char *service_name_)
{
    if (!service_name_ || service_name_[0] == '\0') {
        errno = EINVAL;
        return -1;
    }

    scoped_lock_t lock (_sync);
    if (!_subscriptions.empty ()
        && _subscriptions.find (service_name_) == _subscriptions.end ())
        return 0;
    std::map<std::string, service_state_t>::iterator it =
      _services.find (service_name_);
    if (it == _services.end ())
        return 0;
    return static_cast<int> (it->second.providers.size ());
}

int discovery_t::service_available (const char *service_name_)
{
    if (!service_name_ || service_name_[0] == '\0') {
        errno = EINVAL;
        return -1;
    }

    scoped_lock_t lock (_sync);
    if (!_subscriptions.empty ()
        && _subscriptions.find (service_name_) == _subscriptions.end ())
        return 0;
    std::map<std::string, service_state_t>::iterator it =
      _services.find (service_name_);
    if (it == _services.end ())
        return 0;
    return it->second.providers.empty () ? 0 : 1;
}

int discovery_t::destroy ()
{
    _stop.set (1);
    if (_worker.get_started ())
        _worker.stop ();
    return 0;
}

void discovery_t::run (void *arg_)
{
    discovery_t *self = static_cast<discovery_t *> (arg_);
    self->loop ();
}

void discovery_t::loop ()
{
    void *sub = zmq_socket (static_cast<void *> (_ctx), ZMQ_SUB);
    if (!sub)
        return;

    zmq_setsockopt (sub, ZMQ_SUBSCRIBE, "", 0);

    std::set<std::string> connected;

    while (_stop.get () == 0) {
        std::set<std::string> endpoints;
        {
            scoped_lock_t lock (_sync);
            endpoints = _registry_endpoints;
        }

        for (std::set<std::string>::const_iterator it = endpoints.begin ();
             it != endpoints.end (); ++it) {
            if (connected.find (*it) == connected.end ()) {
                zmq_connect (sub, it->c_str ());
                connected.insert (*it);
            }
        }

        zmq_pollitem_t item;
        item.socket = sub;
        item.fd = 0;
        item.events = ZMQ_POLLIN;
        item.revents = 0;

        const int rc = zmq_poll (&item, 1, 100);
        if (rc > 0 && (item.revents & ZMQ_POLLIN)) {
            std::vector<zmq_msg_t> frames;
            while (true) {
                zmq_msg_t frame;
                zmq_msg_init (&frame);
                if (zmq_msg_recv (&frame, sub, 0) == -1) {
                    zmq_msg_close (&frame);
                    break;
                }
                frames.push_back (frame);
                if (!zmq_msg_more (&frame))
                    break;
            }
            if (!frames.empty ())
                handle_service_list (frames);
            close_frames (&frames);
        }
    }

    zmq_close (sub);
}

void discovery_t::handle_service_list (const std::vector<zmq_msg_t> &frames_)
{
    if (frames_.size () < 4)
        return;

    uint16_t msg_id = 0;
    if (!discovery_protocol::read_u16 (frames_[0], &msg_id))
        return;
    if (msg_id != discovery_protocol::msg_service_list)
        return;

    uint32_t registry_id = 0;
    uint64_t list_seq = 0;
    uint32_t service_count = 0;

    if (!discovery_protocol::read_u32 (frames_[1], &registry_id)
        || !discovery_protocol::read_u64 (frames_[2], &list_seq)
        || !discovery_protocol::read_u32 (frames_[3], &service_count)) {
        return;
    }

    std::map<std::string, service_state_t> updated;

    size_t index = 4;
    for (uint32_t i = 0; i < service_count && index < frames_.size (); ++i) {
        if (index + 1 >= frames_.size ())
            break;
        const std::string service_name =
          discovery_protocol::read_string (frames_[index++]);
        uint32_t provider_count = 0;
        if (!discovery_protocol::read_u32 (frames_[index++], &provider_count))
            break;

        service_state_t state;
        for (uint32_t p = 0; p < provider_count && index + 2 < frames_.size ();
             ++p) {
            provider_info_t info;
            info.service_name = service_name;
            info.endpoint = discovery_protocol::read_string (frames_[index++]);
            discovery_protocol::read_routing_id (frames_[index++],
                                                 &info.routing_id);
            discovery_protocol::read_u32 (frames_[index++], &info.weight);
            info.registered_at = 0;
            state.providers.push_back (info);
        }

        updated[service_name] = state;
    }

    std::vector<std::pair<int, std::string> > events;
    std::vector<discovery_listener_t *> listeners;
    {
        scoped_lock_t lock (_sync);
        std::map<uint32_t, uint64_t>::iterator sit =
          _registry_seq.find (registry_id);
        if (sit != _registry_seq.end () && list_seq <= sit->second)
            return;
        _registry_seq[registry_id] = list_seq;

        std::set<std::string> services;
        for (std::map<std::string, service_state_t>::const_iterator it =
               _services.begin ();
             it != _services.end (); ++it)
            services.insert (it->first);
        for (std::map<std::string, service_state_t>::const_iterator it =
               updated.begin ();
             it != updated.end (); ++it)
            services.insert (it->first);

        for (std::set<std::string>::const_iterator it = services.begin ();
             it != services.end (); ++it) {
            const std::string &service = *it;
            if (!_subscriptions.empty ()
                && _subscriptions.find (service) == _subscriptions.end ())
                continue;

            size_t old_count = 0;
            size_t new_count = 0;
            std::set<std::string> old_eps;
            std::set<std::string> new_eps;

            std::map<std::string, service_state_t>::const_iterator old_it =
              _services.find (service);
            if (old_it != _services.end ()) {
                old_count = old_it->second.providers.size ();
                for (size_t i = 0; i < old_it->second.providers.size (); ++i)
                    old_eps.insert (old_it->second.providers[i].endpoint);
            }

            std::map<std::string, service_state_t>::const_iterator new_it =
              updated.find (service);
            if (new_it != updated.end ()) {
                new_count = new_it->second.providers.size ();
                for (size_t i = 0; i < new_it->second.providers.size (); ++i)
                    new_eps.insert (new_it->second.providers[i].endpoint);
            }

            if (old_count == 0 && new_count > 0)
                events.push_back (
                  std::make_pair (DISCOVERY_EVENT_SERVICE_AVAILABLE, service));
            if (old_count > 0 && new_count == 0)
                events.push_back (std::make_pair (
                  DISCOVERY_EVENT_SERVICE_UNAVAILABLE, service));

            bool provider_added = false;
            bool provider_removed = false;
            for (std::set<std::string>::const_iterator ep =
                   new_eps.begin ();
                 ep != new_eps.end (); ++ep) {
                if (old_eps.find (*ep) == old_eps.end ()) {
                    provider_added = true;
                    break;
                }
            }
            for (std::set<std::string>::const_iterator ep =
                   old_eps.begin ();
                 ep != old_eps.end (); ++ep) {
                if (new_eps.find (*ep) == new_eps.end ()) {
                    provider_removed = true;
                    break;
                }
            }
            if (provider_added)
                events.push_back (
                  std::make_pair (DISCOVERY_EVENT_PROVIDER_ADDED, service));
            if (provider_removed)
                events.push_back (
                  std::make_pair (DISCOVERY_EVENT_PROVIDER_REMOVED, service));
        }

        _services.swap (updated);
        listeners.assign (_listeners.begin (), _listeners.end ());
    }

    for (size_t i = 0; i < events.size (); ++i) {
        for (size_t l = 0; l < listeners.size (); ++l) {
            discovery_listener_t *listener = listeners[l];
            if (!listener)
                continue;
            listener->on_discovery_event (events[i].first, events[i].second);
        }
    }
}
}
