/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_TESTS_ZLINK_TESTING_HPP_INCLUDED__
#define __ZLINK_TESTS_ZLINK_TESTING_HPP_INCLUDED__

#include "services/common/service_public_api.hpp"

namespace zlink
{
service_public_api_guard_t *spot_public_api_guard_for_testing (void *spot_);
void destroy_spot_handle_for_testing (void *spot_);
void destroy_registered_spot_handle_for_testing (void *spot_);
int actor_stream_owner_set_for_testing (void *stream_, void *node_);
}

#endif
