/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

/** Fan-out accounting for a publish operation. */
public record PublishDetail(int snapshotRemoteTargetCount, int admittedRemoteTargetCount,
                            int droppedRemoteTargetCount, int unreachableRemoteTargetCount,
                            int snapshotLocalSpotCount, int admittedLocalSpotCount,
                            int droppedLocalSpotCount) {
}
