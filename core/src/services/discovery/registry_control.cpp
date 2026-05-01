/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "core/c_api_copy_internal.hpp"
#include "core/recv_internal.hpp"
#include "core/send_internal.hpp"
#include "services/discovery/registry.hpp"
#include "services/discovery/discovery_protocol.hpp"

#include <cstdio>
#include <cstring>
#include <vector>

namespace zlink
{
namespace
{
void registry_debug (const char *msg_)
{
    if (std::getenv ("ZLINK_REGISTRY_DEBUG"))
        std::fprintf (stderr, "[registry] %s\n", msg_ ? msg_ : "");
}

void registry_debug_rid (const char *label_, const zlink_routing_id_t &rid_)
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
}

void registry_t::handle_router (void *router_)
{
    zlink_msg_t msg;
    zlink_msg_init (&msg);
    if (recv_msg_internal (router_, &msg, ZLINK_DONTWAIT) == -1) {
        zlink_msg_close (&msg);
        return;
    }

    zlink_routing_id_t sender;
    sender.size = 0;
    discovery_protocol::read_routing_id (msg, &sender);
    zlink_msg_close (&msg);
    std::vector<zlink_msg_t> frames;
    while (true) {
        zlink_msg_t frame;
        zlink_msg_init (&frame);
        if (recv_msg_internal (router_, &frame, ZLINK_DONTWAIT) == -1) {
            zlink_msg_close (&frame);
            break;
        }
        frames.push_back (frame);
        if (!msg_frame_has_more (frame))
            break;
    }

    if (frames.empty ()) {
        close_msg_frames (&frames);
        return;
    }

    uint16_t msg_id = 0;
    if (zlink_msg_size (&frames[0])
        == sizeof (discovery_protocol::bootstrap_req_t)) {
        discovery_protocol::bootstrap_req_t req;
        memcpy (&req, zlink_msg_data (&frames[0]), sizeof (req));
        msg_id = req.msg_id;
    } else if (!discovery_protocol::read_u16 (frames[0], &msg_id)) {
        close_msg_frames (&frames);
        return;
    }

    if (std::getenv ("ZLINK_REGISTRY_DEBUG")) {
        std::fprintf (stderr, "[registry] msg_id=0x%04x frames=%zu\n", msg_id,
                      frames.size ());
    }

    switch (msg_id) {
        case discovery_protocol::msg_register:
            handle_register (router_, &frames[0], frames.size (), sender);
            break;
        case discovery_protocol::msg_unregister:
            handle_unregister (router_, &frames[0], frames.size (), sender);
            break;
        case discovery_protocol::msg_heartbeat:
            handle_heartbeat (&frames[0], frames.size ());
            break;
        case discovery_protocol::msg_bootstrap_req:
            handle_bootstrap (router_, sender);
            break;
        case discovery_protocol::msg_topology_report:
            handle_topology_report (&frames[0], frames.size ());
            break;
        case discovery_protocol::msg_topology_query:
            handle_topology_query (router_, &frames[0], frames.size (), sender);
            break;
        case discovery_protocol::msg_update_attributes:
            handle_update_attributes (router_, &frames[0], frames.size (),
                                      sender);
            break;
        default:
            break;
    }

    close_msg_frames (&frames);
}

void registry_t::handle_bootstrap (void *router_,
                                   const zlink_routing_id_t &sender_id_)
{
    send_bootstrap_reply (router_, sender_id_);
}

void registry_t::handle_topology_report (const zlink_msg_t *frames_,
                                         size_t frame_count_)
{
    if (frame_count_ < 2)
        return;
    if (zlink_msg_size (&frames_[1])
        != sizeof (zlink_registry_topology_entry_t))
        return;

    zlink_registry_topology_entry_t entry;
    memcpy (&entry, zlink_msg_data (const_cast<zlink_msg_t *> (&frames_[1])),
            sizeof (entry));
    upsert_topology_entry (entry, zlink::clock_t ().now_ms ());
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

    const int rc_id =
      zlink::send_msg_internal (router_, &id_frame, ZLINK_SNDMORE);
    log_rc ("send ack id", rc_id);
    if (rc_id == -1) {
        zlink_msg_close (&id_frame);
        return;
    }

    log_rc ("send ack msg_id",
            discovery_protocol::send_u16 (
              router_, discovery_protocol::msg_register_ack, ZLINK_SNDMORE));
    log_rc ("send ack status",
            discovery_protocol::send_frame (router_, &status_, sizeof (status_),
                                            ZLINK_SNDMORE));
    log_rc ("send ack endpoint", discovery_protocol::send_string (
                                   router_, endpoint_, ZLINK_SNDMORE));
    log_rc ("send ack error",
            discovery_protocol::send_string (router_, error_, 0));
}

void registry_t::send_unregister_ack (void *router_,
                                      const zlink_routing_id_t &sender_id_,
                                      uint8_t status_,
                                      const std::string &error_)
{
    registry_debug ("send_unregister_ack");
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

    const int rc_id =
      zlink::send_msg_internal (router_, &id_frame, ZLINK_SNDMORE);
    log_rc ("send unreg ack id", rc_id);
    if (rc_id == -1) {
        zlink_msg_close (&id_frame);
        return;
    }

    log_rc ("send unreg ack msg_id",
            discovery_protocol::send_u16 (
              router_, discovery_protocol::msg_unregister_ack, ZLINK_SNDMORE));
    log_rc ("send unreg ack status", discovery_protocol::send_frame (
                                       router_, &status_, sizeof (status_),
                                       error_.empty () ? 0 : ZLINK_SNDMORE));
    if (!error_.empty ())
        log_rc ("send unreg ack error",
                discovery_protocol::send_string (router_, error_, 0));
}

void registry_t::send_bootstrap_reply (void *router_,
                                       const zlink_routing_id_t &sender_id_)
{
    zlink_msg_t id_frame;
    zlink_msg_init_size (&id_frame, sender_id_.size);
    if (sender_id_.size > 0)
        memcpy (zlink_msg_data (&id_frame), sender_id_.data, sender_id_.size);
    const int rc_id =
      zlink::send_msg_internal (router_, &id_frame, ZLINK_SNDMORE);
    if (std::getenv ("ZLINK_REGISTRY_DEBUG")) {
        std::fprintf (
          stderr, "[registry] bootstrap reply id rc=%d rid_size=%u errno=%d\n",
          rc_id, static_cast<unsigned int> (sender_id_.size), errno);
    }
    if (rc_id == -1) {
        zlink_msg_close (&id_frame);
        return;
    }

    uint32_t registry_id = 0;
    uint32_t heartbeat_interval_ms = 0;
    std::string pub_endpoint;
    std::string uplink_endpoint;
    {
        scoped_lock_t lock (_sync);
        registry_id = _registry_id == 0 ? 1 : _registry_id;
        heartbeat_interval_ms = _heartbeat_interval_ms;
        pub_endpoint = _pub_endpoint;
        uplink_endpoint = _router_endpoint;
    }

    discovery_protocol::bootstrap_rep_t rep;
    memset (&rep, 0, sizeof (rep));
    rep.msg_id = discovery_protocol::msg_bootstrap_rep;
    rep.heartbeat_interval_ms = heartbeat_interval_ms;
    rep.registry_id = registry_id;
    copy_fixed_c_string_from_cstr (rep.pub_endpoint, sizeof (rep.pub_endpoint),
                                   pub_endpoint.c_str ());
    copy_fixed_c_string_from_cstr (rep.uplink_endpoint,
                                   sizeof (rep.uplink_endpoint),
                                   uplink_endpoint.c_str ());
    const int rc_rep =
      discovery_protocol::send_frame (router_, &rep, sizeof (rep), 0);
    if (std::getenv ("ZLINK_REGISTRY_DEBUG")) {
        std::fprintf (
          stderr,
          "[registry] bootstrap reply body rc=%d errno=%d pub=%s uplink=%s\n",
          rc_rep, errno, pub_endpoint.c_str (), uplink_endpoint.c_str ());
    }
}
}
