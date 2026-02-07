/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "services/discovery/discovery.hpp"
#include "services/discovery/discovery_protocol.hpp"

#include "utils/err.hpp"

#include <algorithm>
#include <string.h>

namespace zlink
{
static const uint32_t discovery_tag_value = 0x1e6700d6;
static bool is_valid_service_type (uint16_t service_type_)
{
    return service_type_ == discovery_protocol::service_type_gateway_receiver
           || service_type_ == discovery_protocol::service_type_spot_node;
}

static void close_frames (std::vector<zlink_msg_t> *frames_)
{
    if (!frames_)
        return;
    for (size_t i = 0; i < frames_->size (); ++i)
        zlink_msg_close (&(*frames_)[i]);
    frames_->clear ();
}

discovery_t::discovery_t (ctx_t *ctx_, uint16_t service_type_) :
    _ctx (ctx_),
    _tag (discovery_tag_value),
    _stop (0),
    _update_seq (0),
    _service_type (service_type_)
{
    zlink_assert (_ctx);
    zlink_assert (is_valid_service_type (_service_type));
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

int discovery_t::set_socket_option (int socket_role_,
                                    int option_,
                                    const void *optval_,
                                    size_t optvallen_)
{
    if (!optval_ || optvallen_ == 0) {
        errno = EINVAL;
        return -1;
    }

    if (socket_role_ != ZLINK_DISCOVERY_SOCKET_SUB) {
        errno = EINVAL;
        return -1;
    }

    scoped_lock_t lock (_sync);
    for (size_t i = 0; i < _sub_opts.size (); ++i) {
        if (_sub_opts[i].option == option_) {
            _sub_opts[i].value.assign (
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
    _sub_opts.push_back (opt);
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

uint64_t discovery_t::update_seq ()
{
    scoped_lock_t lock (_sync);
    return _update_seq;
}

uint64_t discovery_t::service_update_seq (const std::string &service_name_)
{
    scoped_lock_t lock (_sync);
    std::map<std::string, uint64_t>::iterator it =
      _service_seq.find (service_name_);
    if (it == _service_seq.end ())
        return 0;
    return it->second;
}

void discovery_t::add_observer (discovery_observer_t *observer_)
{
    if (!observer_)
        return;
    scoped_lock_t lock (_sync);
    _observers.insert (observer_);
}

void discovery_t::remove_observer (discovery_observer_t *observer_)
{
    if (!observer_)
        return;
    scoped_lock_t lock (_sync);
    _observers.erase (observer_);
}

int discovery_t::get_receivers (const char *service_name_,
                                zlink_receiver_info_t *providers_,
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

int discovery_t::receiver_count (const char *service_name_)
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
    void *sub = zlink_socket (static_cast<void *> (_ctx), ZLINK_SUB);
    if (!sub)
        return;

    std::vector<socket_opt_t> sub_opts;
    {
        scoped_lock_t lock (_sync);
        sub_opts = _sub_opts;
    }
    for (size_t i = 0; i < sub_opts.size (); ++i) {
        if (!sub_opts[i].value.empty ())
            zlink_setsockopt (sub, sub_opts[i].option, &sub_opts[i].value[0],
                              sub_opts[i].value.size ());
    }
    zlink_setsockopt (sub, ZLINK_SUBSCRIBE, "", 0);

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
                zlink_connect (sub, it->c_str ());
                connected.insert (*it);
            }
        }

        zlink_pollitem_t item;
        item.socket = sub;
        item.fd = 0;
        item.events = ZLINK_POLLIN;
        item.revents = 0;

        const int rc = zlink_poll (&item, 1, 100);
        if (rc > 0 && (item.revents & ZLINK_POLLIN)) {
            std::vector<zlink_msg_t> frames;
            while (true) {
                zlink_msg_t frame;
                zlink_msg_init (&frame);
                if (zlink_msg_recv (&frame, sub, 0) == -1) {
                    zlink_msg_close (&frame);
                    break;
                }
                frames.push_back (frame);
                if (!zlink_msg_more (&frame))
                    break;
            }
            if (!frames.empty ())
                handle_service_list (frames);
            close_frames (&frames);
        }
    }

    zlink_close (sub);
}

void discovery_t::notify_observers (const std::set<std::string> &services_)
{
    if (services_.empty ())
        return;
    std::vector<discovery_observer_t *> observers;
    {
        scoped_lock_t lock (_sync);
        observers.assign (_observers.begin (), _observers.end ());
    }
    if (observers.empty ())
        return;
    for (std::set<std::string>::const_iterator sit = services_.begin ();
         sit != services_.end (); ++sit) {
        for (size_t i = 0; i < observers.size (); ++i) {
            if (observers[i])
                observers[i]->on_service_update (*sit);
        }
    }
}

void discovery_t::  handle_service_list (const std::vector<zlink_msg_t> &frames_)
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
        if (index + 2 >= frames_.size ())
            break;
        uint16_t service_type = 0;
        if (!discovery_protocol::read_u16 (frames_[index++], &service_type))
            break;
        const std::string service_name =
          discovery_protocol::read_string (frames_[index++]);
        uint32_t receiver_count = 0;
        if (!discovery_protocol::read_u32 (frames_[index++], &receiver_count))
            break;

        service_state_t state;
        for (uint32_t p = 0; p < receiver_count && index + 2 < frames_.size ();
             ++p) {
            provider_info_t info;
            info.service_name = service_name;
            info.endpoint = discovery_protocol::read_string (frames_[index++]);
            discovery_protocol::read_routing_id (frames_[index++],
                                                 &info.routing_id);
            discovery_protocol::read_u32 (frames_[index++], &info.weight);
            info.registered_at = 0;
            if (service_type == _service_type)
                state.providers.push_back (info);
        }

        if (service_type != _service_type)
            continue;

        std::map<std::string, service_state_t>::iterator it =
          updated.find (service_name);
        if (it == updated.end ()) {
            updated[service_name] = state;
        } else {
            it->second.providers.insert (it->second.providers.end (),
                                         state.providers.begin (),
                                         state.providers.end ());
        }
    }

    std::set<std::string> changed;
    {
        scoped_lock_t lock (_sync);
        std::map<uint32_t, uint64_t>::iterator sit =
          _registry_seq.find (registry_id);
        if (sit != _registry_seq.end () && list_seq <= sit->second)
            return;
        _registry_seq[registry_id] = list_seq;

        const auto provider_equal =
          [] (const provider_info_t &a_, const provider_info_t &b_) {
              if (a_.endpoint != b_.endpoint)
                  return false;
              if (a_.routing_id.size != b_.routing_id.size)
                  return false;
              if (a_.routing_id.size > 0
                  && memcmp (a_.routing_id.data, b_.routing_id.data,
                             a_.routing_id.size)
                       != 0)
                  return false;
              return a_.weight == b_.weight;
          };
        const auto providers_equal =
          [&] (const service_state_t &a_, const service_state_t &b_) {
              if (a_.providers.size () != b_.providers.size ())
                  return false;
              for (size_t i = 0; i < a_.providers.size (); ++i) {
                  if (!provider_equal (a_.providers[i], b_.providers[i]))
                      return false;
              }
              return true;
          };

        for (std::map<std::string, service_state_t>::iterator uit =
               updated.begin ();
             uit != updated.end (); ++uit) {
            std::map<std::string, service_state_t>::iterator oit =
              _services.find (uit->first);
            if (oit == _services.end ()
                || !providers_equal (oit->second, uit->second)) {
                _service_seq[uit->first] = _update_seq + 1;
                changed.insert (uit->first);
            }
        }
        for (std::map<std::string, service_state_t>::iterator oit =
               _services.begin ();
             oit != _services.end (); ++oit) {
            if (updated.find (oit->first) == updated.end ()) {
                _service_seq[oit->first] = _update_seq + 1;
                changed.insert (oit->first);
            }
        }
        _services.swap (updated);
        _update_seq++;
    }

    if (!changed.empty ())
        notify_observers (changed);
}
}
