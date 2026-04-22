/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_SPOT_DEFAULTS_HPP_INCLUDED__
#define __ZLINK_SPOT_DEFAULTS_HPP_INCLUDED__

#include <stddef.h>

namespace zlink
{
struct spot_node_option_setting_t
{
    spot_node_option_setting_t () : enabled (false), value (0), size (0) {}

    bool enabled;
    int value;
    size_t size;
};

struct spot_node_pub_defaults_t
{
    spot_node_option_setting_t sndhwm;
    spot_node_option_setting_t sndtimeo;
    spot_node_option_setting_t linger;
    spot_node_option_setting_t nodrop;
    spot_node_option_setting_t sndbuf;
    spot_node_option_setting_t rcvbuf;
};

struct spot_node_sub_defaults_t
{
    spot_node_option_setting_t rcvhwm;
    spot_node_option_setting_t linger;
    spot_node_option_setting_t sndbuf;
    spot_node_option_setting_t rcvbuf;
    spot_node_option_setting_t rcvtimeo;
};
}

#endif
