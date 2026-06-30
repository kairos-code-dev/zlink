/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../../../RegistryMessaging/Server/Consumer/Configuration/consumer_options.hpp"

namespace zlink::framework::e2e::resilience_lifecycle::consumer
{

using consumer_options_t = registry_messaging::consumer::consumer_options_t;

inline consumer_options_t read_consumer_options ()
{
    return registry_messaging::consumer::read_consumer_options ();
}

} // namespace zlink::framework::e2e::resilience_lifecycle::consumer
