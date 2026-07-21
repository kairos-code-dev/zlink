/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.nativeapi;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.service.spot.ActorLocation;
import systems.zlink.contracts.service.spot.ActorControlRecord;
import systems.zlink.contracts.service.spot.ActorJoinCompletion;
import systems.zlink.contracts.service.spot.ActorJoinDecision;
import systems.zlink.contracts.service.spot.ActorLifecycleKind;
import systems.zlink.contracts.service.spot.ActorRef;
import systems.zlink.contracts.service.spot.ActorTransferControl;
import systems.zlink.contracts.service.spot.ActorTransferId;
import systems.zlink.contracts.service.spot.ActorTransferPhase;
import systems.zlink.contracts.service.spot.ActorTransferPrepare;
import systems.zlink.contracts.service.spot.ActorTransferPrepareResult;
import systems.zlink.contracts.service.spot.ActorTransferRole;
import systems.zlink.contracts.service.spot.ActorTransferToken;
import systems.zlink.contracts.service.spot.MeshNodeState;
import systems.zlink.contracts.service.spot.MeshNodeStatus;
import systems.zlink.contracts.service.spot.MeshDestinationKind;
import systems.zlink.contracts.service.spot.MeshPeerEntry;
import systems.zlink.contracts.service.spot.MeshPeerSource;
import systems.zlink.contracts.service.spot.MeshPeerState;
import systems.zlink.contracts.service.spot.MeshRecordPayload;
import systems.zlink.contracts.service.spot.MeshSendReadyData;
import systems.zlink.contracts.service.spot.OperationId;
import systems.zlink.contracts.service.spot.OperationKind;
import systems.zlink.contracts.service.spot.OwnerKind;
import systems.zlink.contracts.service.spot.PublishDetail;
import systems.zlink.contracts.service.spot.ReadyRecord;
import systems.zlink.contracts.service.spot.ReceiveRecord;
import systems.zlink.contracts.service.spot.ReceiveRequirements;
import systems.zlink.contracts.service.spot.RecordKind;
import systems.zlink.contracts.service.spot.ReplyToken;
import systems.zlink.contracts.service.spot.SpotStatus;
import systems.zlink.contracts.service.spot.StreamSessionBinding;
import systems.zlink.contracts.service.spot.StreamSessionState;
import systems.zlink.contracts.service.spot.StreamSessionStatus;
import java.lang.foreign.Arena;
import java.lang.foreign.MemoryLayout;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.nio.charset.StandardCharsets;

/** Marshalling between Core 10.0.0 service structs and contract records. */
public final class ServiceInterop {
    private static final int MESH_NAME_MAX = 256;
    private static final int ENDPOINT_MAX = 512;

    private ServiceInterop() {
    }

    // --- header stamping ---
    public static void stampHeader(MemorySegment out, MemoryLayout layout) {
        out.set(ValueLayout.JAVA_INT_UNALIGNED, 0, (int) layout.byteSize());
        out.set(ValueLayout.JAVA_INT_UNALIGNED, 4, ServiceLayouts.ABI_VERSION);
    }

    public static MemorySegment allocStamped(Arena arena, MemoryLayout layout) {
        MemorySegment out = arena.allocate(layout);
        stampHeader(out, layout);
        return out;
    }

    // --- routing id / actor ref ---
    public static MemorySegment routingIdToNative(Arena arena, RoutingId rid) {
        return NativeRoutingIds.allocate(arena, rid);
    }

    public static MemorySegment actorRefToNative(Arena arena, ActorRef ref) {
        MemorySegment out = arena.allocate(NativeLayouts.ACTOR_REF_LAYOUT);
        writeActorRef(out, ref);
        return out;
    }

    public static void writeActorRef(MemorySegment out, ActorRef ref) {
        NativeRoutingIds.write(out.asSlice(NativeLayouts.ACTOR_REF_NODE_RID_OFFSET,
            NativeLayouts.ROUTING_ID_LAYOUT.byteSize()), ref.nodeRid());
        writeCString(out.asSlice(NativeLayouts.ACTOR_REF_ACTOR_ID_OFFSET,
            NativeLayouts.ACTOR_ID_MAX), ref.actorId(), NativeLayouts.ACTOR_ID_MAX);
        out.set(ValueLayout.JAVA_LONG_UNALIGNED, NativeLayouts.ACTOR_REF_GENERATION_OFFSET,
            ref.generation());
    }

    public static ActorRef actorRefFromNative(MemorySegment segment) {
        MemorySegment view = segment.byteSize() >= NativeLayouts.ACTOR_REF_LAYOUT.byteSize()
            ? segment : segment.reinterpret(NativeLayouts.ACTOR_REF_LAYOUT.byteSize());
        RoutingId nodeRid = NativeRoutingIds.readAllowEmptyValue(view.asSlice(
            NativeLayouts.ACTOR_REF_NODE_RID_OFFSET,
            NativeLayouts.ROUTING_ID_LAYOUT.byteSize()));
        String actorId = NativeHelpers.fromCString(view.asSlice(
            NativeLayouts.ACTOR_REF_ACTOR_ID_OFFSET, NativeLayouts.ACTOR_ID_MAX),
            NativeLayouts.ACTOR_ID_MAX);
        long generation = view.get(ValueLayout.JAVA_LONG_UNALIGNED,
            NativeLayouts.ACTOR_REF_GENERATION_OFFSET);
        return new ActorRef(nodeRid, actorId, generation);
    }

    public static MemorySegment actorTransferPrepareToNative(
        Arena arena, ActorTransferPrepare prepare) {
        MemoryLayout layout = ServiceLayouts.ACTOR_TRANSFER_PREPARE;
        MemorySegment out = allocStamped(arena, layout);
        out.set(ValueLayout.JAVA_INT_UNALIGNED, off(layout, "role"),
            prepare.role().value());
        writeTransferId(slice(out, layout, "transfer_id",
            ServiceLayouts.ACTOR_TRANSFER_ID.byteSize()), prepare.transferId());
        writeActorRef(slice(out, layout, "actor",
            NativeLayouts.ACTOR_REF_LAYOUT.byteSize()), prepare.actor());
        out.set(ValueLayout.JAVA_LONG_UNALIGNED,
            off(layout, "expected_membership_epoch"),
            prepare.expectedMembershipEpoch());
        NativeRoutingIds.write(slice(out, layout, "peer_node_rid",
            NativeLayouts.ROUTING_ID_LAYOUT.byteSize()), prepare.peerNodeRid());
        out.set(ValueLayout.JAVA_LONG_UNALIGNED, off(layout, "final_sequence"),
            prepare.finalSequence());
        out.set(ValueLayout.JAVA_LONG_UNALIGNED,
            off(layout, "reserve_message_count"), prepare.reserveMessageCount());
        out.set(ValueLayout.JAVA_LONG_UNALIGNED,
            off(layout, "reserve_byte_count"), prepare.reserveByteCount());
        return out;
    }

    public static ActorTransferPrepareResult actorTransferPrepareResultFromNative(
        MemorySegment value) {
        MemoryLayout layout = ServiceLayouts.ACTOR_TRANSFER_PREPARE_RESULT;
        return new ActorTransferPrepareResult(
            ActorTransferRole.fromValue(i32(value, layout, "role")),
            transferIdFromNative(slice(value, layout, "transfer_id",
                ServiceLayouts.ACTOR_TRANSFER_ID.byteSize())),
            actorRefFromNative(slice(value, layout, "actor",
                NativeLayouts.ACTOR_REF_LAYOUT.byteSize())),
            u64(value, layout, "final_sequence"),
            u64(value, layout, "reserve_message_count"),
            u64(value, layout, "reserve_byte_count"));
    }

    public static ActorTransferToken actorTransferTokenFromNative(MemorySegment value) {
        byte[] opaque = new byte[(int) ServiceLayouts.ACTOR_TRANSFER_TOKEN.byteSize()];
        MemorySegment.copy(value, 0, MemorySegment.ofArray(opaque), 0, opaque.length);
        return ContractAccess.actorTransferTokenCreate(opaque);
    }

    public static MemorySegment actorTransferTokenToNative(
        Arena arena, ActorTransferToken token) {
        MemorySegment out = arena.allocate(ServiceLayouts.ACTOR_TRANSFER_TOKEN);
        byte[] opaque = ContractAccess.actorTransferTokenOpaque(token);
        MemorySegment.copy(MemorySegment.ofArray(opaque), 0, out, 0,
            Math.min(opaque.length, out.byteSize()));
        return out;
    }

    private static void writeTransferId(MemorySegment out, ActorTransferId id) {
        out.set(ValueLayout.JAVA_LONG_UNALIGNED,
            ServiceLayouts.off(ServiceLayouts.ACTOR_TRANSFER_ID, "high"), id.high());
        out.set(ValueLayout.JAVA_LONG_UNALIGNED,
            ServiceLayouts.off(ServiceLayouts.ACTOR_TRANSFER_ID, "low"), id.low());
    }

    private static ActorTransferId transferIdFromNative(MemorySegment value) {
        return new ActorTransferId(
            u64(value, ServiceLayouts.ACTOR_TRANSFER_ID, "high"),
            u64(value, ServiceLayouts.ACTOR_TRANSFER_ID, "low"));
    }

    // --- operation id ---
    public static OperationId operationIdFromNative(MemorySegment seg) {
        long high = seg.get(ValueLayout.JAVA_LONG_UNALIGNED,
            ServiceLayouts.off(ServiceLayouts.OPERATION_ID, "high"));
        long low = seg.get(ValueLayout.JAVA_LONG_UNALIGNED,
            ServiceLayouts.off(ServiceLayouts.OPERATION_ID, "low"));
        return new OperationId(high, low);
    }

    // --- mesh node status ---
    public static MeshNodeStatus meshNodeStatusFromNative(MemorySegment s) {
        MemoryLayout l = ServiceLayouts.MESH_NODE_STATUS;
        return new MeshNodeStatus(
            MeshNodeState.fromValue(i32(s, l, "state")),
            NativeRoutingIds.readAllowEmptyValue(slice(s, l, "routing_id",
                NativeLayouts.ROUTING_ID_LAYOUT.byteSize())),
            cstr(s, l, "mesh_name", MESH_NAME_MAX),
            cstr(s, l, "local_endpoint", ENDPOINT_MAX),
            u64(s, l, "lifecycle_generation"),
            u64(s, l, "descriptor_revision"),
            i32(s, l, "channel_count"),
            i32(s, l, "configured_peer_count"),
            i32(s, l, "admitted_peer_count"),
            i32(s, l, "draining_peer_count"),
            u64(s, l, "pending_application_messages"),
            u64(s, l, "pending_infrastructure_messages"),
            u64(s, l, "pending_bytes"),
            u64(s, l, "multicast_submitted"),
            u64(s, l, "multicast_dropped_targets"),
            i32(s, l, "last_error"),
            u64(s, l, "last_changed_ms"));
    }

    public static MeshPeerEntry meshPeerEntryFromNative(MemorySegment s) {
        MemoryLayout l = ServiceLayouts.MESH_PEER_ENTRY;
        return new MeshPeerEntry(
            NativeRoutingIds.readAllowEmptyValue(slice(s, l, "routing_id",
                NativeLayouts.ROUTING_ID_LAYOUT.byteSize())),
            cstr(s, l, "endpoint", ENDPOINT_MAX),
            u64(s, l, "connection_intent_id"),
            MeshPeerSource.fromValue(i32(s, l, "source")),
            MeshPeerState.fromValue(i32(s, l, "state")),
            u64(s, l, "lifecycle_generation"),
            u64(s, l, "descriptor_revision"),
            i32(s, l, "channel_count"),
            i32(s, l, "last_error"),
            u64(s, l, "last_changed_ms"));
    }

    public static SpotStatus spotStatusFromNative(MemorySegment s) {
        MemoryLayout l = ServiceLayouts.SPOT_STATUS;
        return new SpotStatus(
            NativeRoutingIds.readAllowEmptyValue(slice(s, l, "spot_rid",
                NativeLayouts.ROUTING_ID_LAYOUT.byteSize())),
            EnumCodecs.spotKindFromValue(i32(s, l, "spot_kind")),
            u64(s, l, "lifecycle_generation"),
            u64(s, l, "pending_application_messages"),
            u64(s, l, "pending_infrastructure_messages"),
            u64(s, l, "pending_bytes"),
            i32(s, l, "active_actor_count"),
            i32(s, l, "draining") != 0,
            i32(s, l, "last_error"),
            u64(s, l, "last_changed_ms"));
    }

    public static ActorLocation actorLocationFromNative(MemorySegment s) {
        MemoryLayout l = ServiceLayouts.ACTOR_LOCATION;
        ActorRef actor = actorRefFromNative(slice(s, l, "actor",
            NativeLayouts.ACTOR_REF_LAYOUT.byteSize()));
        RoutingId spotRid = NativeRoutingIds.readAllowEmptyValue(slice(s, l, "spot_rid",
            NativeLayouts.ROUTING_ID_LAYOUT.byteSize()));
        return new ActorLocation(actor, spotRid, u64(s, l, "spot_generation"),
            u64(s, l, "membership_epoch"));
    }

    public static PublishDetail publishDetailFromNative(MemorySegment s) {
        MemoryLayout l = ServiceLayouts.MESH_PUBLISH_DETAIL;
        return new PublishDetail(
            i32(s, l, "snapshot_remote_target_count"),
            i32(s, l, "admitted_remote_target_count"),
            i32(s, l, "dropped_remote_target_count"),
            i32(s, l, "unreachable_remote_target_count"),
            i32(s, l, "snapshot_local_spot_count"),
            i32(s, l, "admitted_local_spot_count"),
            i32(s, l, "dropped_local_spot_count"));
    }

    public static StreamSessionStatus streamSessionStatusFromNative(MemorySegment s) {
        MemoryLayout l = ServiceLayouts.STREAM_SESSION_STATUS;
        return new StreamSessionStatus(
            StreamSessionState.fromValue(i32(s, l, "state")),
            u64(s, l, "lifecycle_generation"),
            u64(s, l, "session_count"),
            u64(s, l, "binding_count"),
            u64(s, l, "pending_message_count"),
            u64(s, l, "pending_byte_count"),
            i32(s, l, "last_error"));
    }

    public static StreamSessionBinding streamSessionBindingFromNative(MemorySegment s) {
        MemoryLayout l = ServiceLayouts.STREAM_SESSION_BINDING;
        RoutingId sessionRid = NativeRoutingIds.readAllowEmptyValue(slice(s, l, "session_rid",
            NativeLayouts.ROUTING_ID_LAYOUT.byteSize()));
        ActorRef actor = actorRefFromNative(slice(s, l, "actor",
            NativeLayouts.ACTOR_REF_LAYOUT.byteSize()));
        return new StreamSessionBinding(sessionRid, actor, u64(s, l, "binding_generation"),
            u64(s, l, "membership_epoch"));
    }

    // --- dispatch ---
    public static ReadyRecord readyRecordFromNative(MemorySegment s) {
        MemoryLayout l = ServiceLayouts.READY_RECORD;
        return new ReadyRecord(
            OwnerKind.fromValue(i32(s, l, "owner_kind")),
            i32(s, l, "domain"),
            NativeRoutingIds.readAllowEmptyValue(slice(s, l, "spot_rid",
                NativeLayouts.ROUTING_ID_LAYOUT.byteSize())),
            actorRefFromNative(slice(s, l, "actor",
                NativeLayouts.ACTOR_REF_LAYOUT.byteSize())));
    }

    public static ReceiveRequirements receiveRequirementsFromNative(MemorySegment s) {
        MemoryLayout l = ServiceLayouts.RECEIVE_REQUIREMENTS;
        return new ReceiveRequirements(u64(s, l, "message_count"), u64(s, l, "part_count"),
            u64(s, l, "byte_count"));
    }

    public static ReceiveRecord receiveRecordFromNative(MemorySegment s) {
        MemoryLayout l = ServiceLayouts.RECEIVE_RECORD;
        long high = u64(s, l, "operation_id_high");
        long low = u64(s, l, "operation_id_low");
        OperationId opId = new OperationId(high, low);
        byte[] tokenBytes = new byte[32];
        MemorySegment.copy(s, ServiceLayouts.off(l, "reply_token"),
            MemorySegment.ofArray(tokenBytes), 0, 32);
        ReplyToken replyToken = ContractAccess.replyTokenCreate(tokenBytes);
        String channelName = readStr(s, l, "channel_name", "channel_name_size");
        String topic = readStr(s, l, "topic", "topic_size");
        byte[] appMeta = readBytes(s, l, "application_metadata", "application_metadata_size");
        RecordKind kind = RecordKind.fromValue(i32(s, l, "kind"));
        OperationKind operationKind =
            OperationKind.fromValue(i32(s, l, "operation_kind"));
        MeshRecordPayload kindData = meshRecordPayload(
            kind,
            operationKind,
            s.get(ValueLayout.ADDRESS, ServiceLayouts.off(l, "kind_data")),
            u64(s, l, "kind_data_size"));
        return new ReceiveRecord(
            kind,
            i32(s, l, "domain"),
            NativeRoutingIds.readAllowEmptyValue(slice(s, l, "source_node_rid",
                NativeLayouts.ROUTING_ID_LAYOUT.byteSize())),
            NativeRoutingIds.readAllowEmptyValue(slice(s, l, "source_spot_rid",
                NativeLayouts.ROUTING_ID_LAYOUT.byteSize())),
            u64(s, l, "source_binding_generation"),
            actorRefFromNative(slice(s, l, "source_actor",
                NativeLayouts.ACTOR_REF_LAYOUT.byteSize())),
            opId,
            operationKind,
            replyToken,
            channelName,
            topic,
            appMeta,
            kindData,
            i32(s, l, "terminal_result"),
            i32(s, l, "failure_errno"),
            (int) u64(s, l, "part_offset"),
            (int) u64(s, l, "part_count"));
    }

    private static MeshRecordPayload meshRecordPayload(
        RecordKind kind,
        OperationKind operationKind,
        MemorySegment data,
        long size) {
        if (data == null || data.address() == 0 || size == 0) {
            return null;
        }
        if (kind == RecordKind.SEND_READY
            && size >= ServiceLayouts.MESH_SEND_READY_DATA.byteSize()) {
            MemorySegment value = data.reinterpret(
                ServiceLayouts.MESH_SEND_READY_DATA.byteSize());
            MemoryLayout layout = ServiceLayouts.MESH_SEND_READY_DATA;
            return new MeshSendReadyData(
                MeshDestinationKind.fromValue(i32(value, layout, "destination_kind")),
                NativeRoutingIds.readAllowEmptyValue(slice(value, layout, "target_node_rid",
                    NativeLayouts.ROUTING_ID_LAYOUT.byteSize())),
                NativeRoutingIds.readAllowEmptyValue(slice(value, layout, "target_spot_rid",
                    NativeLayouts.ROUTING_ID_LAYOUT.byteSize())),
                actorRefFromNative(slice(value, layout, "target_actor",
                    NativeLayouts.ACTOR_REF_LAYOUT.byteSize())),
                readStr(value, layout, "channel_name", "channel_name_size"));
        }
        if (kind == RecordKind.SPOT_CONTROL
            && size >= ServiceLayouts.ACTOR_CONTROL_RECORD.byteSize()) {
            MemorySegment value = data.reinterpret(ServiceLayouts.ACTOR_CONTROL_RECORD.byteSize());
            MemoryLayout layout = ServiceLayouts.ACTOR_CONTROL_RECORD;
            return new ActorControlRecord(
                ActorLifecycleKind.fromValue(i32(value, layout, "kind")),
                actorRefFromNative(slice(value, layout, "previous_actor",
                    NativeLayouts.ACTOR_REF_LAYOUT.byteSize())),
                actorRefFromNative(slice(value, layout, "current_actor",
                    NativeLayouts.ACTOR_REF_LAYOUT.byteSize())),
                NativeRoutingIds.readAllowEmptyValue(slice(value, layout, "previous_spot_rid",
                    NativeLayouts.ROUTING_ID_LAYOUT.byteSize())),
                NativeRoutingIds.readAllowEmptyValue(slice(value, layout, "current_spot_rid",
                    NativeLayouts.ROUTING_ID_LAYOUT.byteSize())),
                u64(value, layout, "previous_spot_generation"),
                u64(value, layout, "current_spot_generation"),
                u64(value, layout, "previous_membership_epoch"),
                u64(value, layout, "current_membership_epoch"),
                i32(value, layout, "result_code"));
        }
        if (kind == RecordKind.COMPLETION
            && operationKind == OperationKind.ACTOR_JOIN
            && size >= ServiceLayouts.ACTOR_JOIN_COMPLETION.byteSize()) {
            MemorySegment value = data.reinterpret(ServiceLayouts.ACTOR_JOIN_COMPLETION.byteSize());
            MemoryLayout layout = ServiceLayouts.ACTOR_JOIN_COMPLETION;
            MemorySegment location = slice(value, layout, "location",
                ServiceLayouts.ACTOR_LOCATION.byteSize());
            return new ActorJoinCompletion(
                ActorJoinDecision.fromValue(i32(value, layout, "join_result")),
                actorRefFromNative(slice(value, layout, "actor",
                    NativeLayouts.ACTOR_REF_LAYOUT.byteSize())),
                actorLocationFromNative(location));
        }
        if (kind == RecordKind.TRANSFER_CONTROL
            && size >= ServiceLayouts.ACTOR_TRANSFER_CONTROL.byteSize()) {
            MemorySegment value = data.reinterpret(
                ServiceLayouts.ACTOR_TRANSFER_CONTROL.byteSize());
            MemoryLayout layout = ServiceLayouts.ACTOR_TRANSFER_CONTROL;
            return new ActorTransferControl(
                ActorTransferPhase.fromValue(i32(value, layout, "phase")),
                ActorTransferRole.fromValue(i32(value, layout, "role")),
                transferIdFromNative(slice(value, layout, "transfer_id",
                    ServiceLayouts.ACTOR_TRANSFER_ID.byteSize())),
                actorRefFromNative(slice(value, layout, "actor",
                    NativeLayouts.ACTOR_REF_LAYOUT.byteSize())),
                u64(value, layout, "membership_epoch"),
                u64(value, layout, "final_sequence"),
                i32(value, layout, "result_code"),
                i32(value, layout, "failure_errno"));
        }
        return null;
    }

    public static byte[] replyTokenToBytes(ReplyToken token) {
        return ContractAccess.replyTokenOpaque(token);
    }

    public static void writeReplyToken(MemorySegment out, byte[] tokenBytes) {
        MemorySegment.copy(MemorySegment.ofArray(tokenBytes), 0, out, 0,
            Math.min(tokenBytes.length, 32));
    }

    // --- field helpers ---
    private static long off(MemoryLayout l, String name) {
        return ServiceLayouts.off(l, name);
    }

    private static int i32(MemorySegment s, MemoryLayout l, String name) {
        return s.get(ValueLayout.JAVA_INT_UNALIGNED, off(l, name));
    }

    private static long u64(MemorySegment s, MemoryLayout l, String name) {
        return s.get(ValueLayout.JAVA_LONG_UNALIGNED, off(l, name));
    }

    private static MemorySegment slice(MemorySegment s, MemoryLayout l, String name, long size) {
        return s.asSlice(off(l, name), size);
    }

    private static String cstr(MemorySegment s, MemoryLayout l, String name, int max) {
        return NativeHelpers.fromCString(s.asSlice(off(l, name), max), max);
    }

    private static String readStr(MemorySegment s, MemoryLayout l, String ptrName, String sizeName) {
        MemorySegment ptr = s.get(ValueLayout.ADDRESS, off(l, ptrName));
        long size = u64(s, l, sizeName);
        if (ptr == null || ptr.address() == 0 || size <= 0) {
            return null;
        }
        byte[] bytes = new byte[(int) size];
        MemorySegment.copy(ptr.reinterpret(size), 0, MemorySegment.ofArray(bytes), 0, (int) size);
        return new String(bytes, StandardCharsets.UTF_8);
    }

    private static byte[] readBytes(MemorySegment s, MemoryLayout l, String ptrName, String sizeName) {
        MemorySegment ptr = s.get(ValueLayout.ADDRESS, off(l, ptrName));
        long size = u64(s, l, sizeName);
        if (ptr == null || ptr.address() == 0 || size <= 0) {
            return null;
        }
        byte[] bytes = new byte[(int) size];
        MemorySegment.copy(ptr.reinterpret(size), 0, MemorySegment.ofArray(bytes), 0, (int) size);
        return bytes;
    }

    private static void writeCString(MemorySegment out, String value, int maxLen) {
        byte[] bytes = value.getBytes(StandardCharsets.UTF_8);
        if (bytes.length >= maxLen) {
            throw new IllegalArgumentException("value exceeds fixed buffer size " + (maxLen - 1));
        }
        if (bytes.length > 0) {
            MemorySegment.copy(MemorySegment.ofArray(bytes), 0, out, 0, bytes.length);
        }
        out.set(ValueLayout.JAVA_BYTE, bytes.length, (byte) 0);
    }
}
