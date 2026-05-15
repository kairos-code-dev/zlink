/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_API_POLLER_COMPLETION_INTERNAL_HPP_INCLUDED__
#define __ZLINK_API_POLLER_COMPLETION_INTERNAL_HPP_INCLUDED__

#include "api/monitoring/poller_api_internal.hpp"

bool poller_completion_is_hidden (const poller_registration_t *registration_);
int poller_completion_drain_hidden (
  const poller_registration_t *registration_);
int poller_completion_fill_event (
  const poller_registration_t *registration_,
  zlink_poller_event_t *event_out_);

#endif
