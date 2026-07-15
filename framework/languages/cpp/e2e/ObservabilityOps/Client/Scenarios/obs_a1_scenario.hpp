/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

namespace zlink::framework::e2e::observability_ops::client::scenarios
{

/* OBS-A1 is driven through the connector client entrypoint. Keeping the ID in
 * its own scenario file makes the public verification flow discoverable while
 * the remaining Config 11 scenario extraction proceeds independently. */
inline constexpr const char *obs_a1_scenario_id = "OBS-A1";

} // namespace zlink::framework::e2e::observability_ops::client::scenarios
