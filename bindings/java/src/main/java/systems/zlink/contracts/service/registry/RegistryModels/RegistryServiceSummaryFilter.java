/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.registry;

public record RegistryServiceSummaryFilter(AutoConnectType autoConnectType,
                                           ServiceRole serviceRole,
                                           String channelName) {
}
