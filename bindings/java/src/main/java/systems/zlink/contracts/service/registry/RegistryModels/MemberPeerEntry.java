/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.registry;

import systems.zlink.contracts.core.RoutingId;

public record MemberPeerEntry(AutoConnectType autoConnectType,
                              ServiceRole serviceRole,
                              String channelName, String endpoint,
                              RoutingId routingId, long value,
                              int weight) {
}
