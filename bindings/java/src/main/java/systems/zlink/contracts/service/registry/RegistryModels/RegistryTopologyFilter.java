/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.registry;

import systems.zlink.contracts.core.RoutingId;

/**
 * Filters a registry topology query; null fields match anything.
 * @param autoConnectType restrict to this topology type, or null for any
 * @param serviceKind restrict to this service kind, or null for any
 * @param serviceRole restrict to this service role, or null for any
 * @param channelName restrict to this channel name, or null for any
 * @param routingId restrict to this routing id, or null for any
 * @param state restrict to this topology state, or null for any
 * @param source restrict to this topology source, or null for any
 */
public record RegistryTopologyFilter(AutoConnectType autoConnectType,
                                     ServiceKind serviceKind,
                                     ServiceRole serviceRole,
                                     String channelName, RoutingId routingId,
                                     TopologyState state,
                                     TopologySource source) {
}
