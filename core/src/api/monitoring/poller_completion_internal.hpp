/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_API_POLLER_COMPLETION_INTERNAL_HPP_INCLUDED__
#define __ZLINK_API_POLLER_COMPLETION_INTERNAL_HPP_INCLUDED__

#include "api/monitoring/poller_api_internal.hpp"

bool poller_subject_is_completion (poller_subject_kind_t subject_kind_);
bool poller_completion_is_hidden (const poller_registration_t *registration_);
int poller_completion_drain_hidden (const poller_registration_t *registration_);

#endif
