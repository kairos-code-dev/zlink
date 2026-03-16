/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_SPOT_INTERNAL_RECEIVER_HPP_INCLUDED__
#define __ZLINK_SPOT_INTERNAL_RECEIVER_HPP_INCLUDED__

#include "services/spot/spot_sub.hpp"

namespace zlink
{
class spot_internal_receiver_t
{
  public:
    explicit spot_internal_receiver_t (spot_sub_t *sub_) : _sub (sub_) {}

    spot_sub_t *impl () const { return _sub; }

    int set_option (int option_, const void *optval_, size_t optvallen_)
    {
        return _sub ? _sub->set_option (option_, optval_, optvallen_) : -1;
    }

    int subscribe (const char *topic_) { return _sub ? _sub->subscribe (topic_) : -1; }

    int subscribe_pattern (const char *pattern_)
    {
        return _sub ? _sub->subscribe_pattern (pattern_) : -1;
    }

    int unsubscribe (const char *topic_or_pattern_)
    {
        return _sub ? _sub->unsubscribe (topic_or_pattern_) : -1;
    }

    int fill_monitor_snapshot (zlink_monitor_snapshot_t *out_) const
    {
        return _sub ? _sub->fill_monitor_snapshot (out_) : -1;
    }

    void *monitor_open (int events_) { return _sub ? _sub->monitor_open (events_) : NULL; }

    int set_direct_handler (spot_sub_direct_handler_fn handler_, void *userdata_)
    {
        return _sub ? _sub->set_direct_handler (handler_, userdata_) : -1;
    }

    bool has_filters () const { return _sub && _sub->has_filters (); }

    void append_raw_filters (std::set<std::string> *out_) const
    {
        if (_sub)
            _sub->append_raw_filters (out_);
    }

    int destroy_from_node () { return _sub ? _sub->destroy_from_node () : 0; }

    int abort_create () { return _sub ? _sub->abort_create () : 0; }

  private:
    spot_sub_t *_sub;
};
}

#endif
