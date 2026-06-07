/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_SPOT_INTERNAL_RECEIVER_HPP_INCLUDED__
#define __ZLINK_SPOT_INTERNAL_RECEIVER_HPP_INCLUDED__

#include "services/spot/common/spot_defaults.hpp"
#include "services/spot/pubsub/spot_sub.hpp"
#include "utils/mutex.hpp"

#include <atomic>
#include <set>
#include <vector>

namespace zlink
{
class spot_pub_t;

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

    int fill_monitor_snapshot (zlink_monitor_status_t *out_) const
    {
        return _sub ? _sub->fill_monitor_snapshot (out_) : -1;
    }

    socket_base_t *snapshot_socket () const { return _sub ? _sub->snapshot_socket () : NULL; }

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

class spot_node_default_handles_t
{
  public:
    typedef spot_node_option_setting_t option_setting_t;
    typedef spot_node_pub_defaults_t pub_defaults_t;
    typedef spot_node_sub_defaults_t sub_defaults_t;

    spot_node_default_handles_t ();

    static int validate_pub_option (int option_, const void *optval_, size_t optvallen_);
    static int validate_sub_option (int option_, const void *optval_, size_t optvallen_);
    static void
    copy_option_setting (option_setting_t *dst_, const void *optval_, size_t optvallen_);

    int set_pub_option (int option_, const void *optval_, size_t optvallen_);
    int set_sub_option (int option_, const void *optval_, size_t optvallen_);

    pub_defaults_t load_pub_defaults () const;
    sub_defaults_t load_sub_defaults () const;

    spot_sub_t *default_sub () const;
    spot_internal_receiver_t *internal_receiver () const;

    spot_sub_t *fast_default_sub () const;
    spot_internal_receiver_t *fast_internal_receiver () const;
    mutex_t &default_sub_init_lock () { return _default_sub_sync; }

    void publish_default_sub (spot_sub_t *sub_);
    spot_internal_receiver_t *publish_internal_receiver (spot_internal_receiver_t *receiver_,
                                                         spot_sub_t *created_sub_,
                                                         spot_sub_t *previous_default_sub_,
                                                         bool *installed_out_);

    void remove_spot_pub (spot_pub_t *pub_);
    bool remove_spot_sub (spot_sub_t *sub_);
    void snapshot_destroy_handles (const std::set<spot_pub_t *> &pubs_,
                                   const std::set<spot_sub_t *> &subs_,
                                   std::vector<spot_pub_t *> *pubs_out_,
                                   std::vector<spot_sub_t *> *subs_out_);
    spot_internal_receiver_t *detach_internal_receiver ();

  private:
    void store_pub_option (int option_, const void *optval_, size_t optvallen_);
    void store_sub_option (int option_, const void *optval_, size_t optvallen_);

    mutable mutex_t _sync;
    mutable mutex_t _default_sub_sync;
    spot_sub_t *_default_sub;
    spot_internal_receiver_t *_internal_receiver;
    std::atomic<spot_sub_t *> _default_sub_fast;
    std::atomic<spot_internal_receiver_t *> _internal_receiver_fast;
    pub_defaults_t _pub_defaults;
    sub_defaults_t _sub_defaults;
};
}

#endif
