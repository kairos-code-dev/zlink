/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_SERVICE_SPOT_REQUEST_REPLY_INTERNAL_HPP_INCLUDED__
#define __ZLINK_SERVICE_SPOT_REQUEST_REPLY_INTERNAL_HPP_INCLUDED__

#include <zlink.h>

#include <memory>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <stdint.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "api/request_completion_queue_internal.hpp"
#include "api/request_reply_runtime_core.hpp"
#include "api/request_timeout_scheduler_internal.hpp"
#include "core/signaler.hpp"

namespace zlink
{
class service_control_runtime_t;
class socket_base_t;
class spot_node_t;
struct spot_runtime_t;

namespace spot_reqrep_internal
{
struct pending_spot_key_t
{
    uint8_t source_class;
    std::string source_rid;
    std::string source_spot_rid;
    uint64_t request_seq;

    bool operator== (const pending_spot_key_t &other_) const;
    bool operator< (const pending_spot_key_t &other_) const;
};

struct pending_spot_key_hash_t
{
    size_t operator() (const pending_spot_key_t &key_) const;
};

struct pending_reply_t
{
    zlink_reply_handler_fn handler;
    void *userdata;
    uint64_t deadline_ns;
    std::shared_ptr<zlink::request_timeout::task_t> timeout_task;
};

struct parsed_spot_envelope_t
{
    uint8_t source_class;
    std::string source_node_rid;
    std::string source_endpoint_rid;
    zlink_routing_id_t source_node_rid_value;
    zlink_routing_id_t source_endpoint_rid_value;
    uint8_t destination_class;
    std::string destination_node_rid;
    std::string destination_endpoint_rid;
    zlink_routing_id_t destination_node_rid_value;
    zlink_routing_id_t destination_endpoint_rid_value;
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
    std::deque<zlink_spot_dispatch_info_t> subscribe_pending;
    std::deque<zlink_spot_dispatch_info_t> routed_pending;
    std::deque<zlink_spot_dispatch_info_t> actor_join_pending;
    std::deque<zlink_spot_dispatch_info_t> actor_readable_pending;
    std::deque<zlink_spot_dispatch_info_t> channel_reply_pending;
    std::deque<zlink_spot_dispatch_info_t> timer_pending;
    std::set<std::pair<int, void *> > queued_keys;
    std::set<std::pair<int, void *> > rearm_keys;
    zlink_spot_dispatch_info_t active_info;
    bool active_info_valid;
    bool running;
};

struct spot_channel_reply_source_t
{
    explicit spot_channel_reply_source_t (void *dealer_);

    void *dealer;
    zlink::request_completion::queue_state_t completion;
};

struct queued_spot_subscribe_message_t
{
    queued_spot_subscribe_message_t ();
    ~queued_spot_subscribe_message_t ();

    queued_spot_subscribe_message_t (
      queued_spot_subscribe_message_t &&other_) noexcept;
    queued_spot_subscribe_message_t &operator= (
      queued_spot_subscribe_message_t &&other_) noexcept;

    zlink_routing_id_t source_rid;
    std::string topic;
    std::vector<zlink_msg_t> parts;

  private:
    queued_spot_subscribe_message_t (
      const queued_spot_subscribe_message_t &);
    queued_spot_subscribe_message_t &operator= (
      const queued_spot_subscribe_message_t &);
};

struct spot_subscribe_dispatch_queue_t
{
    spot_subscribe_dispatch_queue_t ();

    std::mutex mutex;
    std::condition_variable cv;
    std::deque<queued_spot_subscribe_message_t> messages;
    bool closed;
};

struct queued_routed_message_t
{
    queued_routed_message_t ();
    ~queued_routed_message_t ();

    queued_routed_message_t (queued_routed_message_t &&other_) noexcept;
    queued_routed_message_t &operator= (queued_routed_message_t &&other_) noexcept;

    zlink_routing_id_t source_rid;
    zlink_routing_id_t spot_rid;
    uint64_t request_seq;
    std::vector<zlink_msg_t> parts;

  private:
    queued_routed_message_t (const queued_routed_message_t &);
    queued_routed_message_t &operator= (const queued_routed_message_t &);
};

struct routed_message_queue_t
{
    routed_message_queue_t ();

    std::mutex mutex;
    std::deque<queued_routed_message_t> pending;
    size_t pending_count;
    zlink::signaler_t signaler;
    bool signal_armed;
};

struct spot_request_reply_request_state_t :
    public zlink::request_reply_runtime::sequence_state_t
{
    spot_request_reply_request_state_t ();
    std::unordered_map<pending_spot_key_t,
                       pending_reply_t,
                       pending_spot_key_hash_t>
      pending_replies;
};

struct spot_request_reply_recv_state_t
{
    spot_request_reply_recv_state_t ();

    spot_subscribe_dispatch_queue_t subscribe_queue;
    routed_message_queue_t routed_recv_queue;
    zlink_spot_handler_fn request_handler;
    void *request_handler_userdata;
};

struct spot_request_reply_completion_state_t
{
    spot_request_reply_completion_state_t ();

    zlink::request_completion::queue_state_t direct;
    std::map<void *, std::shared_ptr<spot_channel_reply_source_t> >
      channel_reply_sources;
    size_t pending_channel_requests;
};

struct spot_request_reply_state_t
{
    explicit spot_request_reply_state_t (void *owner_);

    void *owner;
    std::mutex mutex;
    spot_request_reply_request_state_t requests;
    spot_request_reply_recv_state_t recv;
    spot_request_reply_completion_state_t completion_state;
    spot_dispatch_state_t dispatch;
};

struct router_spot_request_reply_request_state_t :
    public zlink::request_reply_runtime::sequence_state_t
{
    router_spot_request_reply_request_state_t ();
    std::unordered_map<uint64_t, pending_reply_t> pending_replies;
};

struct router_spot_request_reply_state_t
{
    explicit router_spot_request_reply_state_t (void *owner_);

    void *owner;
    std::string router_rid;
    std::mutex mutex;
    router_spot_request_reply_request_state_t requests;
    zlink::request_completion::queue_state_t completion;
};

enum routed_spot_delivery_kind_t
{
    routed_spot_delivery_request,
    routed_spot_delivery_reply,
    routed_spot_delivery_direct
};

enum router_spot_delivery_kind_t
{
    router_spot_delivery_request,
    router_spot_delivery_reply,
    router_spot_delivery_direct
};

typedef std::unordered_map<std::string, std::weak_ptr<spot_request_reply_state_t> >
  spot_state_spot_index_t;
typedef std::unordered_map<std::string, spot_state_spot_index_t>
  spot_state_identity_index_t;
typedef std::unordered_map<std::string, std::weak_ptr<router_spot_request_reply_state_t> >
  router_state_identity_index_t;
std::unordered_map<void *, std::shared_ptr<spot_request_reply_state_t> >
  &spot_owner_states ();

extern std::mutex g_spot_request_reply_index_mutex;
extern spot_state_identity_index_t g_spot_state_identity_index;
extern router_state_identity_index_t g_router_state_identity_index;
extern thread_local zlink_routing_id_t g_spot_recv_source_rid;
extern thread_local zlink_routing_id_t g_spot_recv_spot_rid;

int validate_request_parts (zlink_msg_t *parts_, size_t part_count_);
int init_buffer_frame (zlink_msg_t *msg_, const void *data_, size_t size_);
std::shared_ptr<spot_request_reply_state_t> try_find_spot_state (void *spot_);
void erase_spot_owner_state (void *spot_);
int install_spot_dispatch_event_task (spot_request_reply_state_t *state_);
void run_spot_dispatch_worker_once (void *spot_);
void maybe_dispatch_spot_info (
  spot_request_reply_state_t *state_,
  zlink_spot_dispatch_event_t event_,
  zlink_spot_dispatch_subject_kind_t subject_kind_,
  void *subject_);
void close_spot_dispatch_parts (zlink_msg_t *parts_, size_t part_count_);
int queue_spot_message (spot_request_reply_state_t *state_,
                        const zlink_routing_id_t *source_rid_,
                        const zlink_routing_id_t *spot_rid_,
                        uint64_t request_seq_,
                        zlink_msg_t *parts_,
                        size_t part_count_);
int queue_spot_subscribe_message (spot_request_reply_state_t *state_,
                                  const zlink_routing_id_t *source_rid_,
                                  const char *topic_,
                                  size_t topic_len_,
                                  zlink_msg_t *parts_,
                                  size_t part_count_);
void close_spot_subscribe_dispatch_queue (
  spot_subscribe_dispatch_queue_t *queue_);
int recv_internal_spot_queue (spot_request_reply_state_t *state_,
                              const zlink_routing_id_t **source_rid_out_,
                              const zlink_routing_id_t **spot_rid_out_,
                              uint64_t *request_seq_out_,
                              zlink_msg_t **parts_out_,
                              size_t *part_count_out_,
                              int flags_);
int recv_internal_spot_subscribe_queue (
  spot_subscribe_dispatch_queue_t *queue_,
  zlink_routing_id_t *source_rid_out_,
  zlink_msg_t **parts_out_,
  size_t *part_count_out_,
  char *topic_id_out_,
  size_t *topic_id_len_out_,
  int flags_);
int register_spot_pending_request (
  const std::shared_ptr<spot_request_reply_state_t> &state_,
  const pending_spot_key_t &key_,
  uint32_t timeout_ms_,
  zlink_reply_handler_fn handler_,
  void *userdata_);
int register_router_spot_pending_request (
  const std::shared_ptr<router_spot_request_reply_state_t> &state_,
  uint64_t request_seq_,
  const pending_spot_key_t &key_,
  uint32_t timeout_ms_,
  zlink_reply_handler_fn handler_,
  void *userdata_);
void erase_spot_pending_request (
  const std::shared_ptr<spot_request_reply_state_t> &state_,
  const pending_spot_key_t &key_);
int recv_combined_router_message (zlink::socket_base_t *socket_,
                                  std::vector<zlink_msg_t> *out_);
int send_combined_parts_on_socket (zlink::socket_base_t *socket_,
                                   std::vector<zlink_msg_t> *parts_,
                                   zlink_send_flags_t flags_);
int enqueue_runtime_routed_send (zlink::spot_runtime_t *runtime_,
                                 std::vector<zlink_msg_t> *parts_,
                                 zlink_send_flags_t flags_,
                                 int sndtimeo_ms_);
int drain_runtime_routed_send_queue (zlink::spot_runtime_t *runtime_);
int enqueue_runtime_external_router_ingress (
  zlink::spot_runtime_t *runtime_,
  std::vector<zlink_msg_t> *parts_);
int drain_runtime_external_router_ingress_queue (
  zlink::spot_runtime_t *runtime_);
int process_external_router_combined_for_data_plane (
  zlink::spot_node_t *node_,
  std::vector<zlink_msg_t> *combined_);
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
                                      std::vector<zlink_msg_t> *out_);
int build_spot_routed_message (uint8_t source_class_,
                               const std::string &source_node_rid_,
                               const std::string &source_endpoint_rid_,
                               uint8_t destination_class_,
                               const std::string &destination_node_rid_,
                               const std::string &destination_endpoint_rid_,
                               zlink_msg_t *parts_,
                               size_t part_count_,
                               std::vector<zlink_msg_t> *out_);
bool parse_spot_routed_envelope (zlink_msg_t *parts_,
                                 size_t part_count_,
                                 parsed_spot_envelope_t *out_);
bool resolve_spot_node_routing_id (spot_node_t *node_,
                                   std::string *out_);
bool should_process_spot_routed_locally (
  spot_node_t *node_,
  const parsed_spot_envelope_t &envelope_);
int dispatch_spot_routed_delivery (spot_node_t *origin_node_,
                                   routed_spot_delivery_kind_t kind_,
                                   bool local_target_,
                                   const std::string &destination_endpoint_rid_,
                                   zlink_send_flags_t flags_,
                                   int sndtimeo_ms_,
                                   std::vector<zlink_msg_t> *combined_);
int dispatch_router_spot_delivery (
  const std::string &destination_node_rid_,
  const std::string &destination_spot_rid_,
  router_spot_delivery_kind_t kind_,
  zlink_send_flags_t flags_,
  int sndtimeo_ms_,
  std::vector<zlink_msg_t> *combined_);
int dispatch_local_reply (std::vector<zlink_msg_t> *combined_);
int dispatch_local_request (const std::string &router_rid_,
                            std::vector<zlink_msg_t> *combined_);
int dispatch_local_reply_impl (std::vector<zlink_msg_t> *combined_);
int dispatch_local_request_impl (const std::string &router_rid_,
                                 std::vector<zlink_msg_t> *combined_);
int dispatch_local_built_message (uint8_t source_class_,
                                  const std::string &source_node_rid_,
                                  const std::string &source_endpoint_rid_,
                                  uint8_t destination_class_,
                                  const std::string &destination_node_rid_,
                                  const std::string &destination_endpoint_rid_,
                                  uint8_t message_type_,
                                  uint64_t request_seq_,
                                  zlink_msg_t *parts_,
                                  size_t part_count_);
int process_route_combined_for_local_delivery (
  std::vector<zlink_msg_t> *combined_);
int process_parsed_route_combined_for_local_delivery (
  std::vector<zlink_msg_t> *combined_,
  const parsed_spot_envelope_t &spot_envelope_);
int process_route_combined_for_local_delivery_impl (
  std::vector<zlink_msg_t> *combined_);
int process_parsed_route_combined_for_local_delivery_impl (
  std::vector<zlink_msg_t> *combined_,
  const parsed_spot_envelope_t &spot_envelope_);
std::shared_ptr<spot_request_reply_state_t> find_or_create_spot_state (
  void *spot_);
std::shared_ptr<router_spot_request_reply_state_t> find_or_create_router_state (
  void *router_);
std::shared_ptr<spot_request_reply_state_t> find_spot_state_by_identity (
  const std::string &node_rid_,
  const std::string &spot_rid_);
std::vector<std::shared_ptr<spot_request_reply_state_t> > snapshot_spot_states ();
std::shared_ptr<router_spot_request_reply_state_t> find_router_state_by_rid (
  const std::string &router_rid_);
void unregister_spot_identity (
  const std::shared_ptr<spot_request_reply_state_t> &state_);
zlink::spot_runtime_t *resolve_runtime_for_spot_destination (
  const std::string &node_rid_,
  const std::string &spot_rid_);
int ensure_spot_completion_queue_ready (
  const std::shared_ptr<spot_request_reply_state_t> &state_);
int ensure_router_completion_queue_ready (
  const std::shared_ptr<router_spot_request_reply_state_t> &state_);
int ensure_spot_recv_ready (
  const std::shared_ptr<spot_request_reply_state_t> &state_);
int spot_routed_recv_fd (
  const std::shared_ptr<spot_request_reply_state_t> &state_,
  zlink_fd_t *fd_out_);
int spot_routed_recv_wait (
  const std::shared_ptr<spot_request_reply_state_t> &state_,
  int timeout_ms_,
  bool *input_ready_out_,
  bool *signal_ready_out_);
void close_spot_routed_recv_state (
  const std::shared_ptr<spot_request_reply_state_t> &state_);
int queue_spot_reply_completion (
  const std::shared_ptr<spot_request_reply_state_t> &state_,
  zlink_reply_handler_fn handler_,
  void *userdata_,
  int errnum_,
  zlink_msg_t *parts_,
  size_t part_count_);
int queue_router_reply_completion (
  const std::shared_ptr<router_spot_request_reply_state_t> &state_,
  zlink_reply_handler_fn handler_,
  void *userdata_,
  int errnum_,
  zlink_msg_t *parts_,
  size_t part_count_);
int queue_spot_channel_reply_completion (
  const std::shared_ptr<spot_request_reply_state_t> &state_,
  void *dealer_,
  zlink_reply_handler_fn handler_,
  void *userdata_,
  int errnum_,
  zlink_msg_t *parts_,
  size_t part_count_);
int drain_spot_reply_completions (
  const std::shared_ptr<spot_request_reply_state_t> &state_,
  void *owner_handle_);
int drain_spot_channel_reply_completions_from (
  const std::shared_ptr<spot_request_reply_state_t> &state_,
  void *owner_handle_,
  void *dealer_);
int drain_router_reply_completions (
  const std::shared_ptr<router_spot_request_reply_state_t> &state_,
  void *owner_handle_);
bool has_spot_reply_completions (
  const std::shared_ptr<spot_request_reply_state_t> &state_);
bool has_spot_channel_reply_completions (
  const std::shared_ptr<spot_request_reply_state_t> &state_);
bool has_spot_channel_reply_source (
  const std::shared_ptr<spot_request_reply_state_t> &state_,
  void *dealer_);
void snapshot_spot_channel_reply_dealers (
  const std::shared_ptr<spot_request_reply_state_t> &state_,
  std::vector<void *> *dealers_out_);
bool has_router_reply_completions (
  const std::shared_ptr<router_spot_request_reply_state_t> &state_);
zlink::socket_base_t *spot_completion_signal_socket (
  const std::shared_ptr<spot_request_reply_state_t> &state_);
zlink::socket_base_t *router_completion_signal_socket (
  const std::shared_ptr<router_spot_request_reply_state_t> &state_);
void claim_spot_completion_owner (
  const std::shared_ptr<spot_request_reply_state_t> &state_);
void claim_router_completion_owner (
  const std::shared_ptr<router_spot_request_reply_state_t> &state_);
bool current_thread_is_spot_completion_owner (
  const std::shared_ptr<spot_request_reply_state_t> &state_);
bool current_thread_is_router_completion_owner (
  const std::shared_ptr<router_spot_request_reply_state_t> &state_);
bool in_spot_request_completion_callback (void *spot_);
int drain_attached_channel_reply_bridge_progress (
  const std::shared_ptr<spot_request_reply_state_t> &state_);
void unregister_spot_channel_reply_observers (
  const std::shared_ptr<spot_request_reply_state_t> &state_);
void clear_spot_channel_reply_sources (
  const std::shared_ptr<spot_request_reply_state_t> &state_,
  std::vector<std::shared_ptr<spot_channel_reply_source_t> > *sources_out_);
bool has_pending_spot_request_work (
  const std::shared_ptr<spot_request_reply_state_t> &state_);
bool has_pending_router_spot_request_work (
  const std::shared_ptr<router_spot_request_reply_state_t> &state_);
int drain_close_spot_request_reply_state (void *spot_);
int drain_close_router_spot_request_reply_state (void *router_);
void bind_router_state_rid (
  void *router_,
  const std::string &router_rid_,
  const std::shared_ptr<router_spot_request_reply_state_t> &state_);
}
}

#endif
