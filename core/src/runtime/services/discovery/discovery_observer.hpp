/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_DISCOVERY_OBSERVER_HPP_INCLUDED__
#define __ZLINK_DISCOVERY_OBSERVER_HPP_INCLUDED__

#include <string>

namespace zlink
{
class discovery_t;

class discovery_observer_t
{
  public:
    virtual ~discovery_observer_t () {}
    virtual void on_service_update (const std::string &channel_name_) = 0;
    virtual void on_discovery_shutdown_requested (discovery_t *discovery_) { (void) discovery_; }
    virtual void on_discovery_destroyed (discovery_t *discovery_) { (void) discovery_; }
};
}

#endif
