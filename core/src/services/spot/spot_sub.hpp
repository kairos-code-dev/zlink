/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_SPOT_SUB_HPP_INCLUDED__
#define __ZLINK_SPOT_SUB_HPP_INCLUDED__

#include "core/msg.hpp"
#include "utils/atomic_counter.hpp"
#include "utils/condition_variable.hpp"
#include "utils/macros.hpp"

#include <deque>
#include <set>
#include <string>
#include <vector>

namespace zlink
{
class spot_node_t;
class socket_base_t;

struct spot_shared_message_t
{
    std::string topic;
    std::vector<msg_t> parts;
    atomic_counter_t refs;

    spot_shared_message_t () : refs (1) {}
};

class spot_sub_t
{
  public:
    explicit spot_sub_t (spot_node_t *node_);
    ~spot_sub_t ();

    bool check_tag () const;

    int subscribe (const char *topic_);
    int subscribe_pattern (const char *pattern_);
    int unsubscribe (const char *topic_or_pattern_);
    int set_handler (zlink_spot_sub_handler_fn handler_, void *userdata_);

    int recv (zlink_msg_t **parts_,
              size_t *part_count_,
              int flags_,
              char *topic_out_,
              size_t *topic_len_);

    socket_base_t *sub_socket () const;

    int set_socket_option (int option_,
                           const void *optval_,
                           size_t optvallen_);

    int destroy ();

  private:
    friend class spot_node_t;
    struct queue_entry_t
    {
        spot_shared_message_t *shared;
        queue_entry_t () : shared (NULL) {}
    };

    enum handler_state_t
    {
        handler_none = 0,
        handler_active,
        handler_clearing
    };

    bool matches (const std::string &topic_) const;
    bool enqueue_message (const std::string &topic_,
                          const std::vector<msg_t> &payload_);
    bool enqueue_shared_message (spot_shared_message_t *shared_);
    bool dequeue_message (spot_shared_message_t **out_);
    bool callback_enabled () const;

    zlink_msg_t *alloc_msgv_from_parts (std::vector<msg_t> *parts_,
                                        size_t *count_);
    zlink_msg_t *alloc_msgv_from_parts_ref (const std::vector<msg_t> &parts_,
                                            size_t *count_);
    void close_parts (std::vector<msg_t> *parts_);

    spot_node_t *_node;
    uint32_t _tag;
    std::set<std::string> _topics;
    std::set<std::string> _patterns;

    std::deque<queue_entry_t> _queue;
    size_t _queue_hwm;
    condition_variable_t _cv;

    zlink_spot_sub_handler_fn _handler;
    void *_handler_userdata;
    handler_state_t _handler_state;
    atomic_counter_t _callback_inflight;
    condition_variable_t _callback_cv;

    ZLINK_NON_COPYABLE_NOR_MOVABLE (spot_sub_t)
};
}

#endif
