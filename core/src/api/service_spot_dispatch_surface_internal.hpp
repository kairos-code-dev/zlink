/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_API_SERVICE_SPOT_DISPATCH_SURFACE_INTERNAL_HPP_INCLUDED__
#define __ZLINK_API_SERVICE_SPOT_DISPATCH_SURFACE_INTERNAL_HPP_INCLUDED__

#include "zlink.h"

extern "C" void zlink_spot_notify_dispatch_event (
  void *spot_,
  zlink_spot_dispatch_event_t event_);
extern "C" void zlink_spot_notify_dispatch_info (
  void *spot_,
  zlink_spot_dispatch_event_t event_,
  zlink_spot_dispatch_subject_kind_t subject_kind_,
  void *subject_);
extern "C" int zlink_spot_install_external_router_dispatch (void *node_,
                                                            void *socket_);
extern "C" int zlink_spot_process_external_router (void *node_, void *socket_);

#endif
