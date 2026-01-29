/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "discovery/gateway.hpp"

#include "utils/err.hpp"
#include "utils/clock.hpp"
#include "utils/random.hpp"
#include "sockets/socket_base.hpp"

#if defined ZMQ_HAVE_WINDOWS
#include "utils/windows.hpp"
#else
#include <unistd.h>
#endif

#include <algorithm>
#include <string.h>

namespace zmq
{
static const uint32_t gateway_tag_value = 0x1e6700d7;
static const int gateway_default_timeout_ms = 5000;

static void sleep_ms (int ms_)
{
#if defined ZMQ_HAVE_WINDOWS
    Sleep (ms_);
#else
    usleep (static_cast<useconds_t> (ms_) * 1000);
#endif
}

gateway_t::gateway_t (ctx_t *ctx_, discovery_t *discovery_) :
    _ctx (ctx_),
    _discovery (discovery_),
    _tag (gateway_tag_value),
    _next_request_id (1),
    _stop (0)
{
    zmq_assert (_ctx);
    _worker.start (run, this, "gateway");
    if (_discovery)
        _discovery->add_listener (this);
}

gateway_t::~gateway_t ()
{
    if (_discovery)
        _discovery->remove_listener (this);
    _tag = 0xdeadbeef;
}

bool gateway_t::check_tag () const
{
    return _tag == gateway_tag_value;
}

void gateway_t::on_discovery_event (int event_,
                                    const std::string &service_name_)
{
    if (service_name_.empty ())
        return;
    if (event_ != DISCOVERY_EVENT_PROVIDER_ADDED
        && event_ != DISCOVERY_EVENT_PROVIDER_REMOVED
        && event_ != DISCOVERY_EVENT_SERVICE_AVAILABLE
        && event_ != DISCOVERY_EVENT_SERVICE_UNAVAILABLE)
        return;

    scoped_lock_t lock (_sync);
    _refresh_queue.insert (service_name_);
}

static int allocate_threadsafe_router (ctx_t *ctx_, socket_base_t **socket_,
                                       thread_safe_socket_t **threadsafe_)
{
    *socket_ = ctx_->create_socket (ZMQ_ROUTER);
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

gateway_t::service_pool_t *
gateway_t::get_or_create_pool (const std::string &service_name_)
{
    std::map<std::string, service_pool_t>::iterator it =
      _pools.find (service_name_);
    if (it != _pools.end ())
        return &it->second;

    service_pool_t pool;
    pool.service_name = service_name_;
    pool.socket = NULL;
    pool.threadsafe = NULL;
    pool.rr_index = 0;
    pool.lb_strategy = ZMQ_GATEWAY_LB_ROUND_ROBIN;

    if (allocate_threadsafe_router (_ctx, &pool.socket, &pool.threadsafe)
        != 0)
        return NULL;

    _pools.insert (std::make_pair (service_name_, pool));
    return &_pools.find (service_name_)->second;
}

void gateway_t::refresh_pool (service_pool_t *pool_)
{
    if (!pool_ || !_discovery)
        return;

    std::vector<provider_info_t> providers;
    _discovery->snapshot_providers (pool_->service_name, &providers);

    std::vector<std::string> next_endpoints;
    std::map<std::string, zmq_routing_id_t> next_routing;

    for (size_t i = 0; i < providers.size (); ++i) {
        const provider_info_t &entry = providers[i];
        next_endpoints.push_back (entry.endpoint);
        next_routing[entry.endpoint] = entry.routing_id;
    }

    for (size_t i = 0; i < next_endpoints.size (); ++i) {
        const std::string &endpoint = next_endpoints[i];
        if (std::find (pool_->endpoints.begin (), pool_->endpoints.end (),
                       endpoint)
            == pool_->endpoints.end ()) {
            std::map<std::string, zmq_routing_id_t>::iterator rit =
              next_routing.find (endpoint);
            if (rit != next_routing.end () && rit->second.size > 0) {
                pool_->threadsafe->setsockopt (
                  ZMQ_CONNECT_ROUTING_ID, rit->second.data, rit->second.size);
            }
            pool_->threadsafe->connect (endpoint.c_str ());
        }
    }

    for (size_t i = 0; i < pool_->endpoints.size (); ++i) {
        const std::string &endpoint = pool_->endpoints[i];
        if (std::find (next_endpoints.begin (), next_endpoints.end (),
                       endpoint)
            == next_endpoints.end ()) {
            pool_->threadsafe->term_endpoint (endpoint.c_str ());
        }
    }

    pool_->providers.swap (providers);
    pool_->endpoints.swap (next_endpoints);
    pool_->routing_map.swap (next_routing);
}

bool gateway_t::select_provider (service_pool_t *pool_,
                                 zmq_routing_id_t *rid_)
{
    if (!pool_ || pool_->providers.empty () || !rid_)
        return false;

    if (pool_->lb_strategy == ZMQ_GATEWAY_LB_WEIGHTED) {
        uint32_t total = 0;
        for (size_t i = 0; i < pool_->providers.size (); ++i) {
            uint32_t weight = pool_->providers[i].weight;
            if (weight == 0)
                weight = 1;
            total += weight;
        }
        if (total == 0)
            total = 1;
        uint32_t pick = zmq::generate_random () % total;
        uint32_t acc = 0;
        for (size_t i = 0; i < pool_->providers.size (); ++i) {
            uint32_t weight = pool_->providers[i].weight;
            if (weight == 0)
                weight = 1;
            acc += weight;
            if (pick < acc) {
                *rid_ = pool_->providers[i].routing_id;
                return true;
            }
        }
    }

    const size_t index = pool_->rr_index % pool_->providers.size ();
    pool_->rr_index++;
    *rid_ = pool_->providers[index].routing_id;
    return true;
}

int gateway_t::send_request_frames (service_pool_t *pool_,
                                    const zmq_routing_id_t &rid_,
                                    uint64_t request_id_,
                                    zmq_msg_t *parts_,
                                    size_t part_count_,
                                    int flags_)
{
    if (!pool_ || !pool_->threadsafe) {
        errno = ENOTSUP;
        return -1;
    }
    if (flags_ != 0) {
        errno = ENOTSUP;
        return -1;
    }

    msg_t rid;
    if (rid.init_size (rid_.size) != 0)
        return -1;
    if (rid_.size > 0)
        memcpy (rid.data (), rid_.data, rid_.size);
    int flags = (part_count_ > 0 || request_id_ != 0) ? ZMQ_SNDMORE : 0;
    if (pool_->threadsafe->send (&rid, flags) != 0) {
        rid.close ();
        return -1;
    }
    rid.close ();

    msg_t req_id;
    if (req_id.init_size (sizeof (uint64_t)) != 0)
        return -1;
    memcpy (req_id.data (), &request_id_, sizeof (uint64_t));
    flags = part_count_ > 0 ? ZMQ_SNDMORE : 0;
    if (pool_->threadsafe->send (&req_id, flags) != 0) {
        req_id.close ();
        return -1;
    }
    req_id.close ();

    for (size_t i = 0; i < part_count_; ++i) {
        msg_t &part = *reinterpret_cast<msg_t *> (&parts_[i]);
        flags = (i + 1 < part_count_) ? ZMQ_SNDMORE : 0;
        if (pool_->threadsafe->send (&part, flags) != 0)
            return -1;
    }

    return 0;
}

int gateway_t::send (const char *service_name_,
                     zmq_msg_t *parts_,
                     size_t part_count_,
                     int flags_,
                     uint64_t *request_id_out_)
{
    if (!service_name_ || service_name_[0] == '\0' || !parts_
        || part_count_ == 0) {
        errno = EINVAL;
        return -1;
    }
    if (flags_ != 0) {
        errno = ENOTSUP;
        return -1;
    }

    uint64_t request_id = 0;
    {
        scoped_lock_t lock (_sync);
        service_pool_t *pool = get_or_create_pool (service_name_);
        if (!pool) {
            errno = ENOMEM;
            return -1;
        }

        refresh_pool (pool);
        zmq_routing_id_t rid;
        if (!select_provider (pool, &rid)) {
            errno = EHOSTUNREACH;
            return -1;
        }

        request_id = _next_request_id++;
        if (request_id == 0)
            request_id = _next_request_id++;

        pending_request_t pending;
        pending.service_name = service_name_;
        pending.callback = NULL;
        pending.has_deadline = false;

        if (_pending.find (request_id) != _pending.end ()) {
            errno = EAGAIN;
            return -1;
        }
        _pending.insert (std::make_pair (request_id, pending));

        if (send_request_frames (pool, rid, request_id, parts_, part_count_,
                                 flags_)
            != 0) {
            _pending.erase (request_id);
            return -1;
        }
    }

    if (request_id_out_)
        *request_id_out_ = request_id;

    for (size_t i = 0; i < part_count_; ++i)
        zmq_msg_close (&parts_[i]);

    return 0;
}

zmq_msg_t *gateway_t::alloc_msgv_from_parts (std::vector<msg_t> *parts_,
                                             size_t *count_)
{
    if (count_)
        *count_ = 0;
    if (!parts_ || parts_->empty ())
        return NULL;

    const size_t count = parts_->size ();
    zmq_msg_t *out =
      static_cast<zmq_msg_t *> (malloc (count * sizeof (zmq_msg_t)));
    if (!out) {
        errno = ENOMEM;
        close_parts (parts_);
        return NULL;
    }

    for (size_t i = 0; i < count; ++i) {
        msg_t *dst = reinterpret_cast<msg_t *> (&out[i]);
        if (dst->init () != 0 || dst->move ((*parts_)[i]) != 0) {
            for (size_t j = 0; j <= i; ++j)
                zmq_msg_close (&out[j]);
            free (out);
            close_parts (parts_);
            errno = EFAULT;
            return NULL;
        }
    }

    parts_->clear ();
    if (count_)
        *count_ = count;
    return out;
}

void gateway_t::close_parts (std::vector<msg_t> *parts_)
{
    if (!parts_)
        return;
    for (size_t i = 0; i < parts_->size (); ++i)
        (*parts_)[i].close ();
    parts_->clear ();
}

int gateway_t::recv_from_pool (service_pool_t *pool_,
                               uint64_t *request_id_,
                               std::vector<msg_t> *parts_)
{
    if (!pool_ || !pool_->threadsafe || !request_id_ || !parts_) {
        errno = EINVAL;
        return -1;
    }

    msg_t routing;
    if (routing.init () != 0)
        return -1;
    if (pool_->threadsafe->recv (&routing, ZMQ_DONTWAIT) != 0) {
        routing.close ();
        return -1;
    }

    if (!(routing.flags () & msg_t::more)) {
        routing.close ();
        errno = EINVAL;
        return -1;
    }

    msg_t current;
    if (current.init () != 0) {
        routing.close ();
        return -1;
    }
    if (pool_->threadsafe->recv (&current, 0) != 0) {
        current.close ();
        routing.close ();
        return -1;
    }

    parts_->clear ();
    *request_id_ = 0;

    bool more = current.flags () & msg_t::more;
    if (current.size () == sizeof (uint64_t) && more) {
        memcpy (request_id_, current.data (), sizeof (uint64_t));
        current.close ();
    } else {
        parts_->push_back (msg_t ());
        parts_->back ().init ();
        parts_->back ().move (current);
    }

    while (more) {
        msg_t part;
        if (part.init () != 0) {
            close_parts (parts_);
            routing.close ();
            return -1;
        }
        if (pool_->threadsafe->recv (&part, 0) != 0) {
            part.close ();
            close_parts (parts_);
            routing.close ();
            return -1;
        }
        more = part.flags () & msg_t::more;
        parts_->push_back (msg_t ());
        parts_->back ().init ();
        parts_->back ().move (part);
    }

    routing.close ();
    return 0;
}

void gateway_t::handle_response (uint64_t request_id_,
                                 std::vector<msg_t> *parts_,
                                 const std::string &fallback_service_,
                                 std::deque<callback_entry_t> *callbacks_)
{
    zmq_gateway_request_cb_fn callback = NULL;
    std::string service_name = fallback_service_;

    if (request_id_ != 0) {
        std::map<uint64_t, pending_request_t>::iterator it =
          _pending.find (request_id_);
        if (it != _pending.end ()) {
            service_name = it->second.service_name;
            callback = it->second.callback;
            _pending.erase (it);
        }
    }

    if (callback) {
        callbacks_->push_back (callback_entry_t ());
        callback_entry_t &entry = callbacks_->back ();
        entry.callback = callback;
        entry.request_id = request_id_;
        entry.error = 0;
        entry.parts.swap (*parts_);
        return;
    }

    scoped_lock_t lock (_completion_sync);
    _completion_queue.push_back (completion_entry_t ());
    completion_entry_t &completion = _completion_queue.back ();
    completion.service_name = service_name;
    completion.request_id = request_id_;
    completion.error = 0;
    completion.parts.swap (*parts_);
    _completion_cv.broadcast ();
}

void gateway_t::run (void *arg_)
{
    gateway_t *self = static_cast<gateway_t *> (arg_);
    self->loop ();
}

void gateway_t::loop ()
{
    while (_stop.get () == 0) {
        bool handled = false;
        std::deque<callback_entry_t> callbacks;

        {
            scoped_lock_t lock (_sync);
            if (!_refresh_queue.empty ()) {
                for (std::set<std::string>::const_iterator it =
                       _refresh_queue.begin ();
                     it != _refresh_queue.end (); ++it) {
                    std::map<std::string, service_pool_t>::iterator pit =
                      _pools.find (*it);
                    if (pit != _pools.end ())
                        refresh_pool (&pit->second);
                }
                _refresh_queue.clear ();
                handled = true;
            }

            for (std::map<std::string, service_pool_t>::iterator it =
                   _pools.begin ();
                 it != _pools.end (); ++it) {
                service_pool_t &pool = it->second;
                uint64_t request_id = 0;
                std::vector<msg_t> parts;
                if (recv_from_pool (&pool, &request_id, &parts) == 0) {
                    handle_response (request_id, &parts, pool.service_name,
                                     &callbacks);
                    handled = true;
                }
            }

            const std::chrono::steady_clock::time_point now =
              std::chrono::steady_clock::now ();
            for (std::map<uint64_t, pending_request_t>::iterator it =
                   _pending.begin ();
                 it != _pending.end ();) {
                pending_request_t &pending = it->second;
                if (pending.callback && pending.has_deadline
                    && pending.deadline <= now) {
                    callbacks.push_back (callback_entry_t ());
                    callback_entry_t &entry = callbacks.back ();
                    entry.callback = pending.callback;
                    entry.request_id = it->first;
                    entry.error = ETIMEDOUT;
                    _pending.erase (it++);
                    handled = true;
                } else {
                    ++it;
                }
            }
        }

        for (std::deque<callback_entry_t>::iterator it = callbacks.begin ();
             it != callbacks.end (); ++it) {
            callback_entry_t &entry = *it;
            if (!entry.callback)
                continue;
            zmq_msg_t *parts = NULL;
            size_t count = 0;
            int err = entry.error;
            if (err == 0 && !entry.parts.empty ()) {
                parts = alloc_msgv_from_parts (&entry.parts, &count);
                if (!parts) {
                    err = errno ? errno : ENOMEM;
                    count = 0;
                }
            } else if (!entry.parts.empty ()) {
                close_parts (&entry.parts);
            }
            entry.callback (entry.request_id, parts, count, err);
        }

        if (!handled)
            sleep_ms (1);
    }
}

int gateway_t::wait_for_completion (completion_entry_t *entry_,
                                    int timeout_ms_)
{
    if (!entry_) {
        errno = EINVAL;
        return -1;
    }
    if (timeout_ms_ < -1) {
        errno = EINVAL;
        return -1;
    }

    _completion_sync.lock ();
    if (timeout_ms_ == 0 && _completion_queue.empty ()) {
        _completion_sync.unlock ();
        errno = EAGAIN;
        return -1;
    }

    const std::chrono::steady_clock::time_point deadline =
      timeout_ms_ > 0 ? std::chrono::steady_clock::now ()
                            + std::chrono::milliseconds (timeout_ms_)
                      : std::chrono::steady_clock::time_point ();

    while (_completion_queue.empty ()) {
        if (timeout_ms_ < 0) {
            _completion_cv.wait (&_completion_sync, -1);
        } else {
            const std::chrono::steady_clock::time_point now =
              std::chrono::steady_clock::now ();
            if (now >= deadline) {
                _completion_sync.unlock ();
                errno = ETIMEDOUT;
                return -1;
            }
            const auto remaining =
              std::chrono::duration_cast<std::chrono::milliseconds> (deadline
                                                                     - now);
            int rc = _completion_cv.wait (
              &_completion_sync, static_cast<int> (remaining.count ()));
            if (rc == -1 && errno == EAGAIN && _completion_queue.empty ()) {
                _completion_sync.unlock ();
                errno = ETIMEDOUT;
                return -1;
            }
        }
    }

    completion_entry_t &stored = _completion_queue.front ();
    entry_->service_name = stored.service_name;
    entry_->request_id = stored.request_id;
    entry_->error = stored.error;
    entry_->parts.swap (stored.parts);
    _completion_queue.pop_front ();
    _completion_sync.unlock ();
    return 0;
}

int gateway_t::recv (zmq_msg_t **parts_,
                     size_t *part_count_,
                     int flags_,
                     char *service_name_out_,
                     uint64_t *request_id_out_)
{
    if (flags_ != 0) {
        errno = ENOTSUP;
        return -1;
    }

    completion_entry_t entry;
    if (wait_for_completion (&entry, -1) != 0)
        return -1;

    if (service_name_out_) {
        memset (service_name_out_, 0, 256);
        strncpy (service_name_out_, entry.service_name.c_str (), 255);
    }
    if (request_id_out_)
        *request_id_out_ = entry.request_id;

    zmq_msg_t *out_parts = NULL;
    size_t out_count = 0;
    if (!entry.parts.empty ()) {
        out_parts = alloc_msgv_from_parts (&entry.parts, &out_count);
        if (!out_parts) {
            if (parts_)
                *parts_ = NULL;
            if (part_count_)
                *part_count_ = 0;
            return -1;
        }
    }

    if (parts_)
        *parts_ = out_parts;
    if (part_count_)
        *part_count_ = out_count;

    if (entry.error != 0) {
        errno = entry.error;
        return -1;
    }
    return 0;
}

uint64_t gateway_t::request (const char *service_name_,
                             zmq_msg_t *parts_,
                             size_t part_count_,
                             zmq_gateway_request_cb_fn callback_,
                             int timeout_ms_)
{
    if (!service_name_ || service_name_[0] == '\0' || !parts_
        || part_count_ == 0) {
        errno = EINVAL;
        return 0;
    }
    if (!callback_) {
        errno = EINVAL;
        return 0;
    }

    int timeout = timeout_ms_;
    if (timeout_ms_ == ZMQ_REQUEST_TIMEOUT_DEFAULT)
        timeout = gateway_default_timeout_ms;
    if (timeout < -1) {
        errno = EINVAL;
        return 0;
    }

    uint64_t request_id = 0;
    {
        scoped_lock_t lock (_sync);
        service_pool_t *pool = get_or_create_pool (service_name_);
        if (!pool)
            return 0;

        refresh_pool (pool);
        zmq_routing_id_t rid;
        if (!select_provider (pool, &rid)) {
            errno = EHOSTUNREACH;
            return 0;
        }

        request_id = _next_request_id++;
        if (request_id == 0)
            request_id = _next_request_id++;

        pending_request_t pending;
        pending.service_name = service_name_;
        pending.callback = callback_;
        pending.has_deadline = timeout >= 0;
        if (pending.has_deadline)
            pending.deadline = std::chrono::steady_clock::now ()
                               + std::chrono::milliseconds (timeout);

        if (_pending.find (request_id) != _pending.end ()) {
            errno = EAGAIN;
            return 0;
        }
        _pending.insert (std::make_pair (request_id, pending));

        if (send_request_frames (pool, rid, request_id, parts_, part_count_, 0)
            != 0) {
            _pending.erase (request_id);
            return 0;
        }
    }

    for (size_t i = 0; i < part_count_; ++i)
        zmq_msg_close (&parts_[i]);

    return request_id;
}

uint64_t gateway_t::request_send (const char *service_name_,
                                  zmq_msg_t *parts_,
                                  size_t part_count_,
                                  int flags_)
{
    if (!service_name_ || service_name_[0] == '\0' || !parts_
        || part_count_ == 0) {
        errno = EINVAL;
        return 0;
    }
    if (flags_ != 0) {
        errno = ENOTSUP;
        return 0;
    }

    uint64_t request_id = 0;
    {
        scoped_lock_t lock (_sync);
        service_pool_t *pool = get_or_create_pool (service_name_);
        if (!pool)
            return 0;

        refresh_pool (pool);
        zmq_routing_id_t rid;
        if (!select_provider (pool, &rid)) {
            errno = EHOSTUNREACH;
            return 0;
        }

        request_id = _next_request_id++;
        if (request_id == 0)
            request_id = _next_request_id++;

        pending_request_t pending;
        pending.service_name = service_name_;
        pending.callback = NULL;
        pending.has_deadline = false;

        if (_pending.find (request_id) != _pending.end ()) {
            errno = EAGAIN;
            return 0;
        }
        _pending.insert (std::make_pair (request_id, pending));

        if (send_request_frames (pool, rid, request_id, parts_, part_count_,
                                 flags_)
            != 0) {
            _pending.erase (request_id);
            return 0;
        }
    }

    for (size_t i = 0; i < part_count_; ++i)
        zmq_msg_close (&parts_[i]);

    return request_id;
}

int gateway_t::request_recv (zmq_gateway_completion_t *completion_,
                             int timeout_ms_)
{
    if (!completion_) {
        errno = EINVAL;
        return -1;
    }

    completion_entry_t entry;
    if (wait_for_completion (&entry, timeout_ms_) != 0)
        return -1;

    memset (completion_->service_name, 0, sizeof (completion_->service_name));
    strncpy (completion_->service_name, entry.service_name.c_str (),
             sizeof (completion_->service_name) - 1);
    completion_->request_id = entry.request_id;
    completion_->error = entry.error;
    completion_->parts = NULL;
    completion_->part_count = 0;

    if (!entry.parts.empty ()) {
        completion_->parts = alloc_msgv_from_parts (&entry.parts,
                                                    &completion_->part_count);
        if (!completion_->parts) {
            completion_->error = errno ? errno : ENOMEM;
            errno = completion_->error;
            return -1;
        }
    }

    if (completion_->error != 0) {
        errno = completion_->error;
        return -1;
    }
    return 0;
}

int gateway_t::set_lb_strategy (const char *service_name_, int strategy_)
{
    if (!service_name_ || service_name_[0] == '\0') {
        errno = EINVAL;
        return -1;
    }
    if (strategy_ != ZMQ_GATEWAY_LB_ROUND_ROBIN
        && strategy_ != ZMQ_GATEWAY_LB_WEIGHTED) {
        errno = EINVAL;
        return -1;
    }

    scoped_lock_t lock (_sync);
    service_pool_t *pool = get_or_create_pool (service_name_);
    if (!pool)
        return -1;
    pool->lb_strategy = strategy_;
    return 0;
}

int gateway_t::connection_count (const char *service_name_)
{
    if (!service_name_ || service_name_[0] == '\0') {
        errno = EINVAL;
        return -1;
    }

    scoped_lock_t lock (_sync);
    std::map<std::string, service_pool_t>::iterator it =
      _pools.find (service_name_);
    if (it == _pools.end ())
        return 0;
    return static_cast<int> (it->second.endpoints.size ());
}

void *gateway_t::threadsafe_router (const char *service_name_)
{
    if (!service_name_ || service_name_[0] == '\0') {
        errno = EINVAL;
        return NULL;
    }

    scoped_lock_t lock (_sync);
    service_pool_t *pool = get_or_create_pool (service_name_);
    if (!pool)
        return NULL;
    return static_cast<void *> (pool->socket);
}

int gateway_t::destroy ()
{
    if (_discovery)
        _discovery->remove_listener (this);
    _stop.set (1);
    if (_worker.get_started ())
        _worker.stop ();

    scoped_lock_t lock (_sync);
    for (std::map<std::string, service_pool_t>::iterator it =
           _pools.begin ();
         it != _pools.end (); ++it) {
        service_pool_t &pool = it->second;
        if (pool.threadsafe) {
            pool.threadsafe->close ();
            if (pool.socket)
                pool.socket->set_threadsafe_proxy (NULL);
            delete pool.threadsafe;
            pool.threadsafe = NULL;
            pool.socket = NULL;
        } else if (pool.socket) {
            pool.socket->close ();
            pool.socket = NULL;
        }
    }
    _pools.clear ();

    _pending.clear ();

    scoped_lock_t completion_lock (_completion_sync);
    for (std::deque<completion_entry_t>::iterator it =
           _completion_queue.begin ();
         it != _completion_queue.end (); ++it) {
        close_parts (&it->parts);
    }
    _completion_queue.clear ();

    return 0;
}
}
