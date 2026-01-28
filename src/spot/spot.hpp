/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZMQ_SPOT_HPP_INCLUDED__
#define __ZMQ_SPOT_HPP_INCLUDED__

#include "core/msg.hpp"
#include "utils/condition_variable.hpp"
#include "utils/macros.hpp"

#include <deque>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace zmq
{
class spot_node_t;

class spot_t
{
  public:
    spot_t (spot_node_t *node_, bool threadsafe_);
    ~spot_t ();

    bool check_tag () const;

    int publish (const char *topic_,
                 zmq_msg_t *parts_,
                 size_t part_count_,
                 int flags_);
    int subscribe (const char *topic_);
    int subscribe_pattern (const char *pattern_);
    int unsubscribe (const char *topic_or_pattern_);
    int topic_create (const char *topic_, int mode_);
    int topic_destroy (const char *topic_);

    int recv (zmq_msg_t **parts_,
              size_t *part_count_,
              int flags_,
              char *topic_out_,
              size_t *topic_len_);

    int destroy ();

  private:
    friend class spot_node_t;
    struct spot_message_t
    {
        std::string topic;
        std::vector<msg_t> parts;
    };

    bool matches (const std::string &topic_) const;
    bool enqueue_message (const std::string &topic_,
                          const std::vector<msg_t> &payload_);
    bool dequeue_message (spot_message_t *out_);
    bool fetch_ring_message (spot_message_t *out_);

    zmq_msg_t *alloc_msgv_from_parts (std::vector<msg_t> *parts_,
                                      size_t *count_);
    void close_parts (std::vector<msg_t> *parts_);

    spot_node_t *_node;
    uint32_t _tag;
    bool _threadsafe;

    std::set<std::string> _topics;
    std::set<std::string> _patterns;
    std::map<std::string, uint64_t> _ring_cursors;

    std::deque<spot_message_t> _queue;
    size_t _queue_hwm;
    condition_variable_t _cv;

    ZMQ_NON_COPYABLE_NOR_MOVABLE (spot_t)
};
}

#endif
