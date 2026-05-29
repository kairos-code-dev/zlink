/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.registry;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.service.spot.SpotKind;

public record RegistryTopologyEntry(AutoConnectType autoConnectType,
                                    RoutingId routingId,
                                    ServiceKind serviceKind,
                                    ServiceRole serviceRole,
                                    String channelName, String endpoint,
                                    TopologySource source, TopologyState state,
                                    int desiredCount,
                                    int readyCount, int errorCode,
                                    long lastReportedMs,
                                    SpotKind spotKind) {
}
