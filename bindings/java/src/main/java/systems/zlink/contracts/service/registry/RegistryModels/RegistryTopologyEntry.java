/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.registry;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.service.spot.SpotKind;

/**
 * One entry in a registry's topology: a service endpoint and its state.
 * @param autoConnectType the auto-connect topology type
 * @param routingId the peer's routing id
 * @param serviceKind the kind of service
 * @param serviceRole the service role
 * @param channelName the logical channel name
 * @param endpoint the service's transport endpoint
 * @param source where this entry was learned from
 * @param state the current connection state
 * @param desiredCount the desired connection count
 * @param readyCount the number of ready connections
 * @param errorCode the last error code, or 0 for none
 * @param lastReportedMs when this entry was last reported, in milliseconds
 * @param spotKind the kind of spot, if applicable
 */
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
