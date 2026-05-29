/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.registry;

import systems.zlink.contracts.core.RoutingId;

public record RegistryTopologyFilter(AutoConnectType autoConnectType,
                                     ServiceKind serviceKind,
                                     ServiceRole serviceRole,
                                     String channelName, RoutingId routingId,
                                     TopologyState state,
                                     TopologySource source) {
}
