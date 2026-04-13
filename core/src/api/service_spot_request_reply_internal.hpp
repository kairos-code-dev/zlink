/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_SERVICE_SPOT_REQUEST_REPLY_INTERNAL_HPP_INCLUDED__
#define __ZLINK_SERVICE_SPOT_REQUEST_REPLY_INTERNAL_HPP_INCLUDED__

#include <zlink.h>

#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <stdint.h>

#include "api/internal_pair_queue_internal.hpp"
#include "api/request_timeout_scheduler_internal.hpp"

namespace zlink
{
class service_control_runtime_t;

namespace spot_reqrep_internal
{
struct pending_spot_key_t
{
    uint8_t source_class;
    std::string source_rid;
    std::string source_spot_rid;
    uint64_t request_seq;

    bool operator< (const pending_spot_key_t &other_) const;
};

struct pending_reply_t
{
    pending_spot_key_t key;
    zlink_reply_handler_fn handler;
    void *userdata;
    std::shared_ptr<zlink::request_timeout::task_t> timeout_task;
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

struct spot_dispatch_state_t
{
    spot_dispatch_state_t ();

    zlink_spot_dispatch_event_handler_fn handler;
    void *handler_userdata;
    zlink::service_control_runtime_t *runtime;
    uint64_t task_id;
    std::mutex mutex;
    uint32_t pending_event_mask;
    bool running;
};

struct spot_request_reply_state_t
{
    explicit spot_request_reply_state_t (void *owner_);

    void *owner;
    std::mutex mutex;
    uint32_t default_timeout_ms;
    uint64_t next_request_seq;
    std::set<uint64_t> pending_sequences;
    std::map<pending_spot_key_t, pending_reply_t> pending_replies;
    zlink::internal_pair_queue::queue_t subscribe_queue;
    zlink::internal_pair_queue::queue_t recv_queue;
    zlink_spot_handler_fn request_handler;
    void *request_handler_userdata;
    spot_dispatch_state_t dispatch;
};

struct router_spot_request_reply_state_t
{
    explicit router_spot_request_reply_state_t (void *owner_);

    void *owner;
    std::string router_rid;
    std::mutex mutex;
    uint32_t default_timeout_ms;
    uint64_t next_request_seq;
    std::set<uint64_t> pending_sequences;
    std::map<uint64_t, pending_reply_t> pending_replies;
};

typedef std::map<std::string, std::weak_ptr<spot_request_reply_state_t> >
  spot_state_identity_index_t;
typedef std::map<std::string, std::weak_ptr<router_spot_request_reply_state_t> >
  router_state_identity_index_t;

extern std::mutex g_spot_request_reply_index_mutex;
extern spot_state_identity_index_t g_spot_state_identity_index;
extern router_state_identity_index_t g_router_state_identity_index;
extern thread_local zlink_routing_id_t g_spot_recv_source_rid;
extern thread_local zlink_routing_id_t g_spot_recv_spot_rid;

std::string make_spot_identity_key (const std::string &node_rid_,
                                    const std::string &spot_rid_);
int validate_request_parts (zlink_msg_t *parts_, size_t part_count_);
int init_buffer_frame (zlink_msg_t *msg_, const void *data_, size_t size_);
}
}

#endif
