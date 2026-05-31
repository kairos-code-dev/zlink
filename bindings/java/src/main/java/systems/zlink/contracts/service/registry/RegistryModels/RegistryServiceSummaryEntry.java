/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.registry;

/**
 * A per-service rollup of registered endpoints grouped by state.
 * @param autoConnectType the auto-connect topology type
 * @param serviceRole the service role
 * @param channelName the logical channel name
 * @param totalCount total endpoint count
 * @param connectingCount endpoints in connecting state
 * @param readyCount endpoints in ready state
 * @param errorCount endpoints in error state
 * @param stoppedCount endpoints in stopped state
 * @param lastReportedMs when this summary was last reported, in milliseconds
 */
public record RegistryServiceSummaryEntry(AutoConnectType autoConnectType,
                                          ServiceRole serviceRole,
                                          String channelName, int totalCount,
                                          int connectingCount, int readyCount,
                                          int errorCount, int stoppedCount,
                                          long lastReportedMs) {
}
