/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.registry;

/**
 * Filters a service-summary query; null fields match anything.
 * @param autoConnectType restrict to this topology type, or null for any
 * @param serviceRole restrict to this service role, or null for any
 * @param channelName restrict to this channel name, or null for any
 */
public record RegistryServiceSummaryFilter(AutoConnectType autoConnectType,
                                           ServiceRole serviceRole,
                                           String channelName) {
}
