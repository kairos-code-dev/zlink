/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/spot/pubsub/spot_subject_access.hpp"

int zlink_spot_subject_get_sub_option_internal (void *handle_,
                                                zlink_sub_option_t option_,
                                                void *optval_,
                                                size_t *optvallen_)
{
    return spot_subject_get_sub_option (handle_, option_, optval_, optvallen_);
}

int zlink_spot_subject_set_subscription_internal (void *handle_, const char *filter_)
{
    return spot_subject_set_subscription (handle_, filter_);
}

int zlink_spot_subject_unset_subscription_internal (void *handle_, const char *filter_)
{
    return spot_subject_unset_subscription (handle_, filter_);
}

int zlink_spot_subject_subscription_at_internal (
  void *handle_, size_t index_, char *filter_out_, size_t *filter_len_inout_, int *is_pattern_out_)
{
    return spot_subject_subscription_at (handle_, index_, filter_out_, filter_len_inout_,
                                         is_pattern_out_);
}
