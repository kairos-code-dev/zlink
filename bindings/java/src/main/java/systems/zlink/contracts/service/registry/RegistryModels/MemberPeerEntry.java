/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.registry;

import systems.zlink.contracts.core.RoutingId;

/**
 * One member peer registered on a channel.
 * @param autoConnectType the auto-connect topology type
 * @param serviceRole the service role
 * @param channelName the logical channel name
 * @param endpoint the peer's transport endpoint
 * @param routingId the peer's routing id
 * @param value the peer's advertised value
 * @param weight the peer's load-balancing weight
 */
public record MemberPeerEntry(AutoConnectType autoConnectType,
                              ServiceRole serviceRole,
                              String channelName, String endpoint,
                              RoutingId routingId, long value,
                              int weight) {
}
