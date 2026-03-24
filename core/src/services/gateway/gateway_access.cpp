/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/gateway/gateway_access.hpp"

#include <new>

#include "services/discovery/discovery.hpp"
#include "services/gateway/gateway.hpp"

namespace zlink
{
void *gateway_access_t::create (ctx_t *ctx_, const char *service_name_)
{
    gateway_t *gateway =
      new (std::nothrow) gateway_t (ctx_, service_name_, NULL);
    if (!gateway) {
        errno = ENOMEM;
        return NULL;
    }
    return gateway;
}

gateway_t *gateway_access_t::from_handle (void *gateway_)
{
    if (!gateway_) {
        errno = EFAULT;
        return NULL;
    }

    gateway_t *gateway = static_cast<gateway_t *> (gateway_);
    if (!gateway->check_tag ()) {
        errno = EFAULT;
        return NULL;
    }
    return gateway;
}

int gateway_access_t::begin_close_or_fail_busy (gateway_t *gateway_)
{
    return gateway_ && gateway_->public_api_guard ().begin_close_or_fail_busy ()
             ? 0
             : -1;
}

void gateway_access_t::cancel_close (gateway_t *gateway_)
{
    if (gateway_)
        gateway_->public_api_guard ().cancel_close ();
}

int gateway_access_t::attach_discovery (gateway_t *gateway_, void *discovery_)
{
    if (!gateway_ || !discovery_) {
        errno = EFAULT;
        return -1;
    }
    discovery_t *discovery = static_cast<discovery_t *> (discovery_);
    if (!discovery->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return gateway_->attach_discovery (discovery);
}

int gateway_access_t::bind (gateway_t *gateway_, const char *bind_endpoint_)
{
    return gateway_ ? gateway_->bind (bind_endpoint_) : -1;
}

int gateway_access_t::connect (gateway_t *gateway_,
                               const char *endpoint_,
                               const zlink_routing_id_t *routing_id_)
{
    return gateway_ ? gateway_->connect (endpoint_, routing_id_) : -1;
}

int gateway_access_t::disconnect (gateway_t *gateway_, const char *endpoint_)
{
    return gateway_ ? gateway_->disconnect (endpoint_) : -1;
}

int gateway_access_t::snapshot_status (gateway_t *gateway_,
                                       zlink_gateway_status_t *out_)
{
    if (!gateway_) {
        errno = EFAULT;
        return -1;
    }
    service_public_api_scope_t admission (gateway_->public_api_guard ());
    if (!admission.acquired ())
        return -1;
    return gateway_->snapshot_status (out_);
}

int gateway_access_t::set_lb_strategy (gateway_t *gateway_,
                                       zlink_gateway_lb_strategy_t strategy_)
{
    return gateway_ ? gateway_->set_lb_strategy (strategy_) : -1;
}

int gateway_access_t::update_peer_weight (
  gateway_t *gateway_,
  const zlink_routing_id_t *routing_id_,
  uint32_t weight_)
{
    return gateway_ ? gateway_->update_peer_weight (routing_id_, weight_) : -1;
}

int gateway_access_t::destroy (gateway_t *gateway_)
{
    return gateway_ ? gateway_->destroy () : -1;
}

void gateway_access_t::delete_handle (gateway_t *gateway_)
{
    delete gateway_;
}

int gateway_access_t::set_socket_option (gateway_t *gateway_,
                                         int option_,
                                         const void *optval_,
                                         size_t optvallen_)
{
    return gateway_ ? gateway_->set_socket_option (option_, optval_, optvallen_)
                    : -1;
}

int gateway_access_t::get_socket_option (gateway_t *gateway_,
                                         int option_,
                                         void *optval_,
                                         size_t *optvallen_)
{
    return gateway_ ? gateway_->get_socket_option (option_, optval_, optvallen_)
                    : -1;
}

int gateway_access_t::last_endpoint (gateway_t *gateway_,
                                     char *buffer_,
                                     size_t *size_inout_)
{
    return gateway_ ? gateway_->last_endpoint (buffer_, size_inout_) : -1;
}

int gateway_access_t::set_routing_id (gateway_t *gateway_,
                                      const void *data_,
                                      size_t size_)
{
    return gateway_ ? gateway_->set_routing_id (data_, size_) : -1;
}

int gateway_access_t::routing_id (gateway_t *gateway_, zlink_routing_id_t *out_)
{
    return gateway_ ? gateway_->routing_id (out_) : -1;
}

int gateway_access_t::set_tls_server (gateway_t *gateway_,
                                      const char *cert_,
                                      const char *key_)
{
    return gateway_ ? gateway_->set_tls_server (cert_, key_) : -1;
}

int gateway_access_t::set_tls_client (gateway_t *gateway_,
                                      const char *ca_cert_,
                                      const char *hostname_,
                                      int trust_system_)
{
    return gateway_
             ? gateway_->set_tls_client (ca_cert_, hostname_, trust_system_)
             : -1;
}

int gateway_access_t::send (gateway_t *gateway_,
                            zlink_msg_t *parts_,
                            size_t part_count_,
                            zlink_send_flags_t flags_)
{
    return gateway_ ? gateway_->send (parts_, part_count_, flags_) : -1;
}

int gateway_access_t::send_rid (gateway_t *gateway_,
                                const zlink_routing_id_t *routing_id_,
                                zlink_msg_t *parts_,
                                size_t part_count_,
                                zlink_send_flags_t flags_)
{
    return gateway_
             ? gateway_->send_rid (routing_id_, parts_, part_count_, flags_)
             : -1;
}

service_public_api_guard_t *gateway_access_t::public_api_guard (
  gateway_t *gateway_)
{
    return gateway_ ? &gateway_->public_api_guard () : NULL;
}

int gateway_access_t::set_recv_handler (gateway_t *gateway_,
                                        zlink_socket_msg_handler_fn handler_,
                                        void *userdata_)
{
    return gateway_ ? gateway_->set_handler (handler_, userdata_) : -1;
}

int gateway_access_t::set_send_ready_handler (
  gateway_t *gateway_,
  zlink_send_ready_handler_fn handler_,
  void *userdata_)
{
    return gateway_ ? gateway_->set_send_ready_handler (handler_, userdata_)
                    : -1;
}

void *gateway_access_t::monitor_open (gateway_t *gateway_, int events_)
{
    return gateway_ ? gateway_->monitor_open (events_) : NULL;
}

socket_base_t *gateway_access_t::router_socket (gateway_t *gateway_)
{
    return gateway_ ? static_cast<socket_base_t *> (gateway_->router ()) : NULL;
}

void gateway_access_t::dispatch_send_ready (gateway_t *gateway_)
{
    if (gateway_)
        gateway_->dispatch_send_ready ();
}

void gateway_access_t::dispatch_message (gateway_t *gateway_,
                                         const zlink_routing_id_t *source_rid_,
                                         zlink_msg_t *parts_,
                                         size_t part_count_)
{
    if (gateway_)
        gateway_->dispatch_message (source_rid_, parts_, part_count_);
}
}
