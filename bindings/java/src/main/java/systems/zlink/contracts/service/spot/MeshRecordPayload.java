/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

/** Marker for a typed payload carried in a receive record's kind data. */
public sealed interface MeshRecordPayload
    permits ActorControlRecord, ActorJoinCompletion, ActorTransferControl,
    MeshSendReadyData {
}
