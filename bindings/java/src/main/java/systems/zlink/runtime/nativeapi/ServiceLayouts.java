/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.nativeapi;

import java.lang.foreign.AddressLayout;
import java.lang.foreign.MemoryLayout;
import java.lang.foreign.MemoryLayout.PathElement;
import java.lang.foreign.ValueLayout;

/**
 * Struct layouts for the Core 10.0.0 service API. Field order and padding
 * mirror the C structs exactly; all offsets are derived via named path
 * elements. u64/size_t use JAVA_LONG_UNALIGNED, pointers use a byte-aligned
 * ADDRESS, and explicit padding reproduces the C compiler's natural alignment.
 */
public final class ServiceLayouts {
    private ServiceLayouts() {
    }

    static final int ABI_VERSION = 1;

    private static final ValueLayout.OfInt I32 = ValueLayout.JAVA_INT_UNALIGNED;
    private static final ValueLayout.OfLong U64 = ValueLayout.JAVA_LONG_UNALIGNED;
    private static final AddressLayout PTR = ValueLayout.ADDRESS.withByteAlignment(1);

    private static final MemoryLayout ROUTING_ID = NativeLayouts.ROUTING_ID_LAYOUT;
    private static final MemoryLayout ACTOR_REF = NativeLayouts.ACTOR_REF_LAYOUT;

    public static final MemoryLayout MESH_METADATA_VIEW = MemoryLayout.structLayout(
        PTR.withName("data"),
        U64.withName("size"));

    private static MemoryLayout seq(int n) {
        return MemoryLayout.sequenceLayout(n, ValueLayout.JAVA_BYTE);
    }

    private static MemoryLayout pad(int n) {
        return MemoryLayout.paddingLayout(n);
    }

    public static long off(MemoryLayout layout, String name) {
        return layout.byteOffset(PathElement.groupElement(name));
    }

    public static final MemoryLayout MESH_NODE_OPTIONS = MemoryLayout.structLayout(
        I32.withName("struct_size"),
        I32.withName("version"),
        PTR.withName("mesh_name"),
        U64.withName("mesh_name_size"),
        PTR.withName("trust_profile"),
        U64.withName("trust_profile_size"));

    public static final MemoryLayout MESH_PEER_CONNECTION_OPTIONS = MemoryLayout.structLayout(
        I32.withName("struct_size"),
        I32.withName("version"),
        PTR.withName("endpoint"),
        U64.withName("endpoint_size"),
        I32.withName("has_expected_rid"),
        ROUTING_ID.withName("expected_rid"),
        pad(4));

    public static final MemoryLayout MESH_PUBLISH_DETAIL = MemoryLayout.structLayout(
        I32.withName("struct_size"),
        I32.withName("version"),
        I32.withName("snapshot_remote_target_count"),
        I32.withName("admitted_remote_target_count"),
        I32.withName("dropped_remote_target_count"),
        I32.withName("unreachable_remote_target_count"),
        I32.withName("snapshot_local_spot_count"),
        I32.withName("admitted_local_spot_count"),
        I32.withName("dropped_local_spot_count"));

    public static final MemoryLayout MESH_NODE_STATUS = MemoryLayout.structLayout(
        I32.withName("struct_size"),
        I32.withName("version"),
        I32.withName("state"),
        ROUTING_ID.withName("routing_id"),
        seq(256).withName("mesh_name"),
        seq(512).withName("local_endpoint"),
        pad(4),
        U64.withName("lifecycle_generation"),
        U64.withName("descriptor_revision"),
        I32.withName("channel_count"),
        I32.withName("configured_peer_count"),
        I32.withName("admitted_peer_count"),
        I32.withName("draining_peer_count"),
        U64.withName("pending_application_messages"),
        U64.withName("pending_infrastructure_messages"),
        U64.withName("pending_bytes"),
        U64.withName("multicast_submitted"),
        U64.withName("multicast_dropped_targets"),
        I32.withName("last_error"),
        pad(4),
        U64.withName("last_changed_ms"));

    public static final MemoryLayout MESH_PEER_ENTRY = MemoryLayout.structLayout(
        I32.withName("struct_size"),
        I32.withName("version"),
        U64.withName("connection_intent_id"),
        I32.withName("source"),
        I32.withName("state"),
        ROUTING_ID.withName("routing_id"),
        U64.withName("lifecycle_generation"),
        U64.withName("descriptor_revision"),
        seq(512).withName("endpoint"),
        I32.withName("channel_count"),
        I32.withName("last_error"),
        U64.withName("last_changed_ms"));

    public static final MemoryLayout MESH_MONITOR_OPEN_OPTIONS = MemoryLayout.structLayout(
        I32.withName("struct_size"),
        I32.withName("version"),
        U64.withName("events"));

    public static final MemoryLayout MESH_MONITOR_EVENT = MemoryLayout.structLayout(
        I32.withName("struct_size"),
        I32.withName("version"),
        I32.withName("kind"),
        pad(4),
        U64.withName("timestamp_ms"),
        U64.withName("mesh_lifecycle_generation"),
        U64.withName("mesh_descriptor_revision"),
        I32.withName("mesh_state"),
        ROUTING_ID.withName("peer_rid"),
        pad(4),
        U64.withName("peer_lifecycle_generation"),
        U64.withName("peer_descriptor_revision"),
        I32.withName("owner_kind"),
        ROUTING_ID.withName("spot_rid"),
        pad(4),
        ACTOR_REF.withName("actor"),
        seq(256).withName("channel_name"),
        U64.withName("operation_id_high"),
        U64.withName("operation_id_low"),
        I32.withName("snapshot_remote_target_count"),
        I32.withName("admitted_remote_target_count"),
        I32.withName("dropped_remote_target_count"),
        I32.withName("unreachable_remote_target_count"),
        I32.withName("snapshot_local_spot_count"),
        I32.withName("admitted_local_spot_count"),
        I32.withName("dropped_local_spot_count"),
        I32.withName("result_code"),
        I32.withName("failure_errno"),
        pad(4));

    public static final MemoryLayout MESH_MONITOR_STATUS = MemoryLayout.structLayout(
        I32.withName("struct_size"),
        I32.withName("version"),
        I32.withName("state"),
        pad(4),
        U64.withName("peer_admitted"),
        U64.withName("peer_rejected"),
        U64.withName("submitted_messages"),
        U64.withName("completed_operations"),
        U64.withName("backpressured_submits"),
        U64.withName("multicast_messages"),
        U64.withName("multicast_dropped_targets"),
        U64.withName("active_claims"),
        U64.withName("pending_application_messages"),
        U64.withName("pending_infrastructure_messages"),
        U64.withName("pending_bytes"));

    public static final MemoryLayout ACTOR_LOCATION = MemoryLayout.structLayout(
        I32.withName("struct_size"),
        I32.withName("version"),
        ACTOR_REF.withName("actor"),
        ROUTING_ID.withName("spot_rid"),
        U64.withName("spot_generation"),
        U64.withName("membership_epoch"));

    public static final MemoryLayout ACTOR_CONTROL_RECORD = MemoryLayout.structLayout(
        I32.withName("struct_size"),
        I32.withName("version"),
        I32.withName("kind"),
        pad(4),
        ACTOR_REF.withName("previous_actor"),
        ACTOR_REF.withName("current_actor"),
        ROUTING_ID.withName("previous_spot_rid"),
        ROUTING_ID.withName("current_spot_rid"),
        U64.withName("previous_spot_generation"),
        U64.withName("current_spot_generation"),
        U64.withName("previous_membership_epoch"),
        U64.withName("current_membership_epoch"),
        I32.withName("result_code"),
        pad(4));

    public static final MemoryLayout ACTOR_JOIN_COMPLETION = MemoryLayout.structLayout(
        I32.withName("struct_size"),
        I32.withName("version"),
        I32.withName("join_result"),
        pad(4),
        ACTOR_REF.withName("actor"),
        ACTOR_LOCATION.withName("location"));

    public static final MemoryLayout ACTOR_TRANSFER_ID = MemoryLayout.structLayout(
        U64.withName("high"),
        U64.withName("low"));

    public static final MemoryLayout ACTOR_TRANSFER_TOKEN = MemoryLayout.structLayout(
        MemoryLayout.sequenceLayout(8, U64).withName("opaque"));

    public static final MemoryLayout ACTOR_TRANSFER_PREPARE = MemoryLayout.structLayout(
        I32.withName("struct_size"),
        I32.withName("version"),
        I32.withName("role"),
        pad(4),
        ACTOR_TRANSFER_ID.withName("transfer_id"),
        ACTOR_REF.withName("actor"),
        U64.withName("expected_membership_epoch"),
        ROUTING_ID.withName("peer_node_rid"),
        U64.withName("final_sequence"),
        U64.withName("reserve_message_count"),
        U64.withName("reserve_byte_count"));

    public static final MemoryLayout ACTOR_TRANSFER_PREPARE_RESULT = MemoryLayout.structLayout(
        I32.withName("struct_size"),
        I32.withName("version"),
        I32.withName("role"),
        pad(4),
        ACTOR_TRANSFER_ID.withName("transfer_id"),
        ACTOR_REF.withName("actor"),
        U64.withName("final_sequence"),
        U64.withName("reserve_message_count"),
        U64.withName("reserve_byte_count"));

    public static final MemoryLayout ACTOR_TRANSFER_CONTROL = MemoryLayout.structLayout(
        I32.withName("struct_size"),
        I32.withName("version"),
        I32.withName("phase"),
        I32.withName("role"),
        ACTOR_TRANSFER_ID.withName("transfer_id"),
        ACTOR_REF.withName("actor"),
        U64.withName("membership_epoch"),
        U64.withName("final_sequence"),
        I32.withName("result_code"),
        I32.withName("failure_errno"));

    public static final MemoryLayout SPOT_STATUS = MemoryLayout.structLayout(
        I32.withName("struct_size"),
        I32.withName("version"),
        ROUTING_ID.withName("spot_rid"),
        I32.withName("spot_kind"),
        pad(4),
        U64.withName("lifecycle_generation"),
        U64.withName("pending_application_messages"),
        U64.withName("pending_infrastructure_messages"),
        U64.withName("pending_bytes"),
        I32.withName("active_actor_count"),
        I32.withName("draining"),
        I32.withName("last_error"),
        pad(4),
        U64.withName("last_changed_ms"));

    public static final MemoryLayout OPERATION_ID = MemoryLayout.structLayout(
        U64.withName("high"),
        U64.withName("low"));

    public static final MemoryLayout REPLY_TOKEN = MemoryLayout.structLayout(
        MemoryLayout.sequenceLayout(4, U64).withName("opaque"));

    public static final MemoryLayout CLAIM = MemoryLayout.structLayout(
        MemoryLayout.sequenceLayout(4, U64).withName("opaque"));

    public static final MemoryLayout READY_RECORD = MemoryLayout.structLayout(
        I32.withName("struct_size"),
        I32.withName("version"),
        I32.withName("owner_kind"),
        I32.withName("domain"),
        ROUTING_ID.withName("spot_rid"),
        ACTOR_REF.withName("actor"));

    public static final MemoryLayout RECEIVE_REQUIREMENTS = MemoryLayout.structLayout(
        I32.withName("struct_size"),
        I32.withName("version"),
        U64.withName("message_count"),
        U64.withName("part_count"),
        U64.withName("byte_count"));

    public static final MemoryLayout RECEIVE_RECORD = MemoryLayout.structLayout(
        I32.withName("struct_size"),
        I32.withName("version"),
        I32.withName("kind"),
        I32.withName("domain"),
        ROUTING_ID.withName("source_node_rid"),
        ROUTING_ID.withName("source_spot_rid"),
        U64.withName("source_binding_generation"),
        ACTOR_REF.withName("source_actor"),
        U64.withName("operation_id_high"),
        U64.withName("operation_id_low"),
        I32.withName("operation_kind"),
        pad(4),
        MemoryLayout.sequenceLayout(4, U64).withName("reply_token"),
        PTR.withName("channel_name"),
        U64.withName("channel_name_size"),
        PTR.withName("topic"),
        U64.withName("topic_size"),
        PTR.withName("application_metadata"),
        U64.withName("application_metadata_size"),
        PTR.withName("kind_data"),
        U64.withName("kind_data_size"),
        U64.withName("part_offset"),
        U64.withName("part_count"),
        I32.withName("terminal_result"),
        I32.withName("failure_errno"));

    public static final MemoryLayout MESH_SEND_READY_DATA = MemoryLayout.structLayout(
        I32.withName("struct_size"),
        I32.withName("version"),
        I32.withName("destination_kind"),
        ROUTING_ID.withName("target_node_rid"),
        ROUTING_ID.withName("target_spot_rid"),
        pad(4),
        ACTOR_REF.withName("target_actor"),
        PTR.withName("channel_name"),
        U64.withName("channel_name_size"));

    public static final MemoryLayout STREAM_SESSION_BINDING = MemoryLayout.structLayout(
        I32.withName("struct_size"),
        I32.withName("version"),
        ROUTING_ID.withName("session_rid"),
        ACTOR_REF.withName("actor"),
        U64.withName("binding_generation"),
        U64.withName("membership_epoch"));

    public static final MemoryLayout STREAM_SESSION_STATUS = MemoryLayout.structLayout(
        I32.withName("struct_size"),
        I32.withName("version"),
        I32.withName("state"),
        pad(4),
        U64.withName("lifecycle_generation"),
        U64.withName("session_count"),
        U64.withName("binding_count"),
        U64.withName("pending_message_count"),
        U64.withName("pending_byte_count"),
        I32.withName("last_error"),
        pad(4));
}
