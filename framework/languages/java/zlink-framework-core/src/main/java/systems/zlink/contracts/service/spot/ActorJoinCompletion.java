/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

/** The typed payload of an actor join completion record. */
public record ActorJoinCompletion(
    ActorJoinDecision joinResult,
    ActorRef actor,
    ActorLocation location) implements MeshRecordPayload {
}
