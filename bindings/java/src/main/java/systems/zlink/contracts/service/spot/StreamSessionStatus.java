/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

/** A point-in-time status snapshot of a STREAM session service. */
public record StreamSessionStatus(StreamSessionState state, long lifecycleGeneration,
                                  long sessionCount, long bindingCount,
                                  long pendingMessageCount, long pendingByteCount,
                                  int lastError) {
}
