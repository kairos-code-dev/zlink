/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

/**
 * Carries the result of a completed actor entry spot join.
 * @param result the join outcome
 */
public record ActorJoinEntrySpotCompletion(
        ActorJoinEntrySpotResult result) {
}
