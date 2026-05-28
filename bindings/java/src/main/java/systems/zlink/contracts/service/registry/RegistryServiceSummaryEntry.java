/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.registry;

public record RegistryServiceSummaryEntry(AutoConnectType autoConnectType,
                                          ServiceRole serviceRole,
                                          String channelName, int totalCount,
                                          int connectingCount, int readyCount,
                                          int errorCount, int stoppedCount,
                                          long lastReportedMs) {
}
