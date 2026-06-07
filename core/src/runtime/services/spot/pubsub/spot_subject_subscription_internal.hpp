/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_SPOT_SUBJECT_SUBSCRIPTION_INTERNAL_HPP_INCLUDED__
#define __ZLINK_SPOT_SUBJECT_SUBSCRIPTION_INTERNAL_HPP_INCLUDED__

#include <vector>

#include "services/spot/pubsub/spot_sub.hpp"

int spot_append_subscription_subjects (void *handle_,
                                       std::vector<zlink::spot_sub_t::subject_descriptor_t> *out_);

#endif
