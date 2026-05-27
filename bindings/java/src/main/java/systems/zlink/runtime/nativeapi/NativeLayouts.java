package systems.zlink.runtime.nativeapi;

import java.lang.foreign.MemoryLayout;
import java.lang.foreign.MemoryLayout.PathElement;
import java.lang.foreign.ValueLayout;

public final class NativeLayouts {
    private NativeLayouts() {}

    public static final MemoryLayout MSG_LAYOUT = MemoryLayout
            .sequenceLayout(64, ValueLayout.JAVA_BYTE)
            .withByteAlignment(ValueLayout.ADDRESS.byteAlignment());

    public static final MemoryLayout ROUTING_ID_LAYOUT = MemoryLayout.structLayout(
            ValueLayout.JAVA_BYTE.withName("size"),
            MemoryLayout.sequenceLayout(255, ValueLayout.JAVA_BYTE).withName("data"));
    public static final long ROUTING_ID_SIZE_OFFSET = ROUTING_ID_LAYOUT.byteOffset(
            PathElement.groupElement("size"));
    public static final long ROUTING_ID_DATA_OFFSET = ROUTING_ID_LAYOUT.byteOffset(
            PathElement.groupElement("data"));

    public static final MemoryLayout SOCKET_MONITOR_OPEN_OPTIONS_LAYOUT =
            MemoryLayout.structLayout(
                    ValueLayout.JAVA_INT.withName("events"));
    public static final long SOCKET_MONITOR_OPEN_EVENTS_OFFSET =
            SOCKET_MONITOR_OPEN_OPTIONS_LAYOUT.byteOffset(
                    PathElement.groupElement("events"));

    public static final MemoryLayout MONITOR_SNAPSHOT_LAYOUT =
            MemoryLayout.structLayout(
                    ValueLayout.JAVA_INT.withName("source_kind"),
                    ValueLayout.JAVA_INT.withName("state_flags"),
                    ValueLayout.JAVA_INT.withName("detail_flags"),
                    MemoryLayout.paddingLayout(4),
                    ValueLayout.JAVA_LONG_UNALIGNED.withName("snd_pending_msgs"),
                    ValueLayout.JAVA_LONG_UNALIGNED.withName("rcv_pending_msgs"),
                    ValueLayout.JAVA_INT.withName("auto_hwm_enabled"),
                    ValueLayout.JAVA_INT.withName("auto_hwm_profile"),
                    ValueLayout.JAVA_INT.withName("auto_hwm_role"),
                    ValueLayout.JAVA_INT.withName("auto_hwm_policy_class"),
                    ValueLayout.JAVA_LONG_UNALIGNED.withName("auto_hwm_unit_budget_bytes"),
                    ValueLayout.JAVA_INT.withName("auto_hwm_size_cap"),
                    MemoryLayout.paddingLayout(4),
                    ValueLayout.JAVA_LONG_UNALIGNED.withName("auto_hwm_socket_message_slots"),
                    ValueLayout.JAVA_LONG_UNALIGNED.withName("auto_hwm_effective_message_bytes"),
                    ValueLayout.JAVA_INT.withName("auto_hwm_applied_sndhwm"),
                    ValueLayout.JAVA_INT.withName("auto_hwm_applied_rcvhwm"),
                    ValueLayout.JAVA_INT.withName("auto_hwm_effective_sndbuf"),
                    ValueLayout.JAVA_INT.withName("auto_hwm_effective_rcvbuf"),
                    ValueLayout.JAVA_LONG_UNALIGNED.withName("auto_hwm_last_recalc_ms"),
                    ValueLayout.JAVA_INT.withName("auto_hwm_last_recalc_reason"),
                    ValueLayout.JAVA_INT.withName("auto_hwm_send_blocked_ratio_ppm"),
                    ValueLayout.JAVA_INT.withName("auto_hwm_deferred_sndhwm"),
                    ValueLayout.JAVA_INT.withName("auto_hwm_deferred_rcvhwm"));
    public static final long MONITOR_SNAPSHOT_SOURCE_KIND_OFFSET =
            MONITOR_SNAPSHOT_LAYOUT.byteOffset(
                    PathElement.groupElement("source_kind"));
    public static final long MONITOR_SNAPSHOT_STATE_FLAGS_OFFSET =
            MONITOR_SNAPSHOT_LAYOUT.byteOffset(
                    PathElement.groupElement("state_flags"));
    public static final long MONITOR_SNAPSHOT_DETAIL_FLAGS_OFFSET =
            MONITOR_SNAPSHOT_LAYOUT.byteOffset(
                    PathElement.groupElement("detail_flags"));
    public static final long MONITOR_SNAPSHOT_SND_PENDING_MSGS_OFFSET =
            MONITOR_SNAPSHOT_LAYOUT.byteOffset(
                    PathElement.groupElement("snd_pending_msgs"));
    public static final long MONITOR_SNAPSHOT_RCV_PENDING_MSGS_OFFSET =
            MONITOR_SNAPSHOT_LAYOUT.byteOffset(
                    PathElement.groupElement("rcv_pending_msgs"));
    public static final long MONITOR_SNAPSHOT_AUTO_HWM_ENABLED_OFFSET =
            MONITOR_SNAPSHOT_LAYOUT.byteOffset(
                    PathElement.groupElement("auto_hwm_enabled"));
    public static final long MONITOR_SNAPSHOT_AUTO_HWM_PROFILE_OFFSET =
            MONITOR_SNAPSHOT_LAYOUT.byteOffset(
                    PathElement.groupElement("auto_hwm_profile"));
    public static final long MONITOR_SNAPSHOT_AUTO_HWM_ROLE_OFFSET =
            MONITOR_SNAPSHOT_LAYOUT.byteOffset(
                    PathElement.groupElement("auto_hwm_role"));
    public static final long MONITOR_SNAPSHOT_AUTO_HWM_POLICY_CLASS_OFFSET =
            MONITOR_SNAPSHOT_LAYOUT.byteOffset(
                    PathElement.groupElement("auto_hwm_policy_class"));
    public static final long MONITOR_SNAPSHOT_AUTO_HWM_UNIT_BUDGET_BYTES_OFFSET =
            MONITOR_SNAPSHOT_LAYOUT.byteOffset(
                    PathElement.groupElement("auto_hwm_unit_budget_bytes"));
    public static final long MONITOR_SNAPSHOT_AUTO_HWM_SIZE_CAP_OFFSET =
            MONITOR_SNAPSHOT_LAYOUT.byteOffset(
                    PathElement.groupElement("auto_hwm_size_cap"));
    public static final long MONITOR_SNAPSHOT_AUTO_HWM_SOCKET_MESSAGE_SLOTS_OFFSET =
            MONITOR_SNAPSHOT_LAYOUT.byteOffset(
                    PathElement.groupElement("auto_hwm_socket_message_slots"));
    public static final long MONITOR_SNAPSHOT_AUTO_HWM_EFFECTIVE_MESSAGE_BYTES_OFFSET =
            MONITOR_SNAPSHOT_LAYOUT.byteOffset(
                    PathElement.groupElement("auto_hwm_effective_message_bytes"));
    public static final long MONITOR_SNAPSHOT_AUTO_HWM_APPLIED_SNDHWM_OFFSET =
            MONITOR_SNAPSHOT_LAYOUT.byteOffset(
                    PathElement.groupElement("auto_hwm_applied_sndhwm"));
    public static final long MONITOR_SNAPSHOT_AUTO_HWM_APPLIED_RCVHWM_OFFSET =
            MONITOR_SNAPSHOT_LAYOUT.byteOffset(
                    PathElement.groupElement("auto_hwm_applied_rcvhwm"));
    public static final long MONITOR_SNAPSHOT_AUTO_HWM_EFFECTIVE_SNDBUF_OFFSET =
            MONITOR_SNAPSHOT_LAYOUT.byteOffset(
                    PathElement.groupElement("auto_hwm_effective_sndbuf"));
    public static final long MONITOR_SNAPSHOT_AUTO_HWM_EFFECTIVE_RCVBUF_OFFSET =
            MONITOR_SNAPSHOT_LAYOUT.byteOffset(
                    PathElement.groupElement("auto_hwm_effective_rcvbuf"));
    public static final long MONITOR_SNAPSHOT_AUTO_HWM_LAST_RECALC_MS_OFFSET =
            MONITOR_SNAPSHOT_LAYOUT.byteOffset(
                    PathElement.groupElement("auto_hwm_last_recalc_ms"));
    public static final long MONITOR_SNAPSHOT_AUTO_HWM_LAST_RECALC_REASON_OFFSET =
            MONITOR_SNAPSHOT_LAYOUT.byteOffset(
                    PathElement.groupElement("auto_hwm_last_recalc_reason"));
    public static final long MONITOR_SNAPSHOT_AUTO_HWM_SEND_BLOCKED_RATIO_PPM_OFFSET =
            MONITOR_SNAPSHOT_LAYOUT.byteOffset(
                    PathElement.groupElement("auto_hwm_send_blocked_ratio_ppm"));
    public static final long MONITOR_SNAPSHOT_AUTO_HWM_DEFERRED_SNDHWM_OFFSET =
            MONITOR_SNAPSHOT_LAYOUT.byteOffset(
                    PathElement.groupElement("auto_hwm_deferred_sndhwm"));
    public static final long MONITOR_SNAPSHOT_AUTO_HWM_DEFERRED_RCVHWM_OFFSET =
            MONITOR_SNAPSHOT_LAYOUT.byteOffset(
                    PathElement.groupElement("auto_hwm_deferred_rcvhwm"));

    public static final MemoryLayout MONITOR_EVENT_LAYOUT = MemoryLayout.structLayout(
            ValueLayout.JAVA_LONG_UNALIGNED.withName("event"),
            ValueLayout.JAVA_LONG_UNALIGNED.withName("value"),
            ROUTING_ID_LAYOUT.withName("routing_id"),
            MemoryLayout.sequenceLayout(256, ValueLayout.JAVA_BYTE).withName("local_addr"),
            MemoryLayout.sequenceLayout(256, ValueLayout.JAVA_BYTE).withName("remote_addr"));
    public static final long MONITOR_EVENT_OFFSET = MONITOR_EVENT_LAYOUT.byteOffset(
            PathElement.groupElement("event"));
    public static final long MONITOR_VALUE_OFFSET = MONITOR_EVENT_LAYOUT.byteOffset(
            PathElement.groupElement("value"));
    public static final long MONITOR_ROUTING_OFFSET = MONITOR_EVENT_LAYOUT.byteOffset(
            PathElement.groupElement("routing_id"));
    public static final long MONITOR_LOCAL_OFFSET = MONITOR_EVENT_LAYOUT.byteOffset(
            PathElement.groupElement("local_addr"));
    public static final long MONITOR_REMOTE_OFFSET = MONITOR_EVENT_LAYOUT.byteOffset(
            PathElement.groupElement("remote_addr"));

    public static final MemoryLayout SERVICE_EVENT_LAYOUT = MemoryLayout.structLayout(
            ValueLayout.JAVA_INT.withName("service_kind"),
            ValueLayout.JAVA_INT.withName("event_type"),
            ValueLayout.JAVA_INT.withName("status"),
            ValueLayout.JAVA_INT.withName("error_code"),
            ValueLayout.JAVA_INT.withName("value"),
            ValueLayout.JAVA_INT.withName("detail_flags"),
            MemoryLayout.sequenceLayout(256, ValueLayout.JAVA_BYTE).withName("channel_name"),
            MemoryLayout.sequenceLayout(256, ValueLayout.JAVA_BYTE).withName("endpoint"),
            ROUTING_ID_LAYOUT.withName("routing_id"),
            MemoryLayout.sequenceLayout(256, ValueLayout.JAVA_BYTE).withName("subject"),
            ValueLayout.JAVA_INT.withName("subject_kind"));
    public static final long SERVICE_EVENT_SERVICE_KIND_OFFSET =
            SERVICE_EVENT_LAYOUT.byteOffset(
                    PathElement.groupElement("service_kind"));
    public static final long SERVICE_EVENT_EVENT_TYPE_OFFSET =
            SERVICE_EVENT_LAYOUT.byteOffset(
                    PathElement.groupElement("event_type"));
    public static final long SERVICE_EVENT_STATUS_OFFSET =
            SERVICE_EVENT_LAYOUT.byteOffset(
                    PathElement.groupElement("status"));
    public static final long SERVICE_EVENT_ERROR_CODE_OFFSET =
            SERVICE_EVENT_LAYOUT.byteOffset(
                    PathElement.groupElement("error_code"));
    public static final long SERVICE_EVENT_VALUE_OFFSET =
            SERVICE_EVENT_LAYOUT.byteOffset(
                    PathElement.groupElement("value"));
    public static final long SERVICE_EVENT_DETAIL_FLAGS_OFFSET =
            SERVICE_EVENT_LAYOUT.byteOffset(
                    PathElement.groupElement("detail_flags"));
    public static final long SERVICE_EVENT_CHANNEL_NAME_OFFSET =
            SERVICE_EVENT_LAYOUT.byteOffset(
                    PathElement.groupElement("channel_name"));
    public static final long SERVICE_EVENT_ENDPOINT_OFFSET =
            SERVICE_EVENT_LAYOUT.byteOffset(
                    PathElement.groupElement("endpoint"));
    public static final long SERVICE_EVENT_ROUTING_ID_OFFSET =
            SERVICE_EVENT_LAYOUT.byteOffset(
                    PathElement.groupElement("routing_id"));
    public static final long SERVICE_EVENT_SUBJECT_OFFSET =
            SERVICE_EVENT_LAYOUT.byteOffset(
                    PathElement.groupElement("subject"));
    public static final long SERVICE_EVENT_SUBJECT_KIND_OFFSET =
            SERVICE_EVENT_LAYOUT.byteOffset(
                    PathElement.groupElement("subject_kind"));

    public static final MemoryLayout SPOT_DISPATCH_INFO_LAYOUT =
            MemoryLayout.structLayout(
                    ValueLayout.JAVA_INT.withName("event"),
                    ValueLayout.JAVA_INT.withName("subject_kind"),
                    ValueLayout.ADDRESS.withName("subject"));
    public static final long SPOT_DISPATCH_INFO_EVENT_OFFSET =
            SPOT_DISPATCH_INFO_LAYOUT.byteOffset(
                    PathElement.groupElement("event"));
    public static final long SPOT_DISPATCH_INFO_SUBJECT_KIND_OFFSET =
            SPOT_DISPATCH_INFO_LAYOUT.byteOffset(
                    PathElement.groupElement("subject_kind"));
    public static final long SPOT_DISPATCH_INFO_SUBJECT_OFFSET =
            SPOT_DISPATCH_INFO_LAYOUT.byteOffset(
                    PathElement.groupElement("subject"));

    public static final int ACTOR_ID_MAX = 256;

    public static final MemoryLayout ACTOR_REF_LAYOUT =
            MemoryLayout.structLayout(
                    ROUTING_ID_LAYOUT.withName("node_rid"),
                    MemoryLayout.sequenceLayout(ACTOR_ID_MAX,
                            ValueLayout.JAVA_BYTE).withName("actor_id"),
                    ValueLayout.JAVA_LONG_UNALIGNED.withName("generation"));
    public static final long ACTOR_REF_NODE_RID_OFFSET =
            ACTOR_REF_LAYOUT.byteOffset(PathElement.groupElement("node_rid"));
    public static final long ACTOR_REF_ACTOR_ID_OFFSET =
            ACTOR_REF_LAYOUT.byteOffset(PathElement.groupElement("actor_id"));
    public static final long ACTOR_REF_GENERATION_OFFSET =
            ACTOR_REF_LAYOUT.byteOffset(PathElement.groupElement("generation"));

    public static final MemoryLayout ACTOR_RECV_INFO_LAYOUT =
            MemoryLayout.structLayout(
                    ACTOR_REF_LAYOUT.withName("actor"),
                    ROUTING_ID_LAYOUT.withName("source_node_rid"),
                    ROUTING_ID_LAYOUT.withName("source_session_rid"),
                    ValueLayout.JAVA_INT.withName("flags"),
                    MemoryLayout.paddingLayout(4));
    public static final long ACTOR_RECV_INFO_ACTOR_OFFSET =
            ACTOR_RECV_INFO_LAYOUT.byteOffset(PathElement.groupElement("actor"));
    public static final long ACTOR_RECV_INFO_SOURCE_NODE_RID_OFFSET =
            ACTOR_RECV_INFO_LAYOUT.byteOffset(
                    PathElement.groupElement("source_node_rid"));
    public static final long ACTOR_RECV_INFO_SOURCE_SESSION_RID_OFFSET =
            ACTOR_RECV_INFO_LAYOUT.byteOffset(
                    PathElement.groupElement("source_session_rid"));
    public static final long ACTOR_RECV_INFO_FLAGS_OFFSET =
            ACTOR_RECV_INFO_LAYOUT.byteOffset(PathElement.groupElement("flags"));

    public static final MemoryLayout ACTOR_JOIN_INFO_LAYOUT =
            MemoryLayout.structLayout(
                    ACTOR_REF_LAYOUT.withName("source_actor"),
                    ACTOR_REF_LAYOUT.withName("target_actor"),
                    ROUTING_ID_LAYOUT.withName("source_node_rid"),
                    ROUTING_ID_LAYOUT.withName("source_spot_rid"),
                    ROUTING_ID_LAYOUT.withName("target_node_rid"),
                    ROUTING_ID_LAYOUT.withName("target_spot_rid"),
                    ValueLayout.JAVA_LONG_UNALIGNED.withName("join_epoch"),
                    ValueLayout.ADDRESS.withName("request"),
                    ValueLayout.JAVA_INT.withName("flags"),
                    MemoryLayout.paddingLayout(4));
    public static final long ACTOR_JOIN_INFO_SOURCE_ACTOR_OFFSET =
            ACTOR_JOIN_INFO_LAYOUT.byteOffset(
                    PathElement.groupElement("source_actor"));
    public static final long ACTOR_JOIN_INFO_TARGET_ACTOR_OFFSET =
            ACTOR_JOIN_INFO_LAYOUT.byteOffset(
                    PathElement.groupElement("target_actor"));
    public static final long ACTOR_JOIN_INFO_SOURCE_NODE_RID_OFFSET =
            ACTOR_JOIN_INFO_LAYOUT.byteOffset(
                    PathElement.groupElement("source_node_rid"));
    public static final long ACTOR_JOIN_INFO_SOURCE_SPOT_RID_OFFSET =
            ACTOR_JOIN_INFO_LAYOUT.byteOffset(
                    PathElement.groupElement("source_spot_rid"));
    public static final long ACTOR_JOIN_INFO_TARGET_NODE_RID_OFFSET =
            ACTOR_JOIN_INFO_LAYOUT.byteOffset(
                    PathElement.groupElement("target_node_rid"));
    public static final long ACTOR_JOIN_INFO_TARGET_SPOT_RID_OFFSET =
            ACTOR_JOIN_INFO_LAYOUT.byteOffset(
                    PathElement.groupElement("target_spot_rid"));
    public static final long ACTOR_JOIN_INFO_JOIN_EPOCH_OFFSET =
            ACTOR_JOIN_INFO_LAYOUT.byteOffset(
                    PathElement.groupElement("join_epoch"));
    public static final long ACTOR_JOIN_INFO_REQUEST_OFFSET =
            ACTOR_JOIN_INFO_LAYOUT.byteOffset(PathElement.groupElement("request"));
    public static final long ACTOR_JOIN_INFO_FLAGS_OFFSET =
            ACTOR_JOIN_INFO_LAYOUT.byteOffset(PathElement.groupElement("flags"));

    public static final MemoryLayout ACTOR_CREATE_RESULT_LAYOUT =
            MemoryLayout.structLayout(
                    ValueLayout.JAVA_INT.withName("status"),
                    MemoryLayout.paddingLayout(4),
                    ACTOR_REF_LAYOUT.withName("actor"));
    public static final long ACTOR_CREATE_RESULT_STATUS_OFFSET =
            ACTOR_CREATE_RESULT_LAYOUT.byteOffset(
                    PathElement.groupElement("status"));
    public static final long ACTOR_CREATE_RESULT_ACTOR_OFFSET =
            ACTOR_CREATE_RESULT_LAYOUT.byteOffset(
                    PathElement.groupElement("actor"));

    public static final MemoryLayout ACTOR_ROUTE_LAYOUT =
            MemoryLayout.structLayout(
                    ACTOR_REF_LAYOUT.withName("actor"),
                    ROUTING_ID_LAYOUT.withName("current_spot_rid"),
                    ValueLayout.JAVA_INT.withName("current_spot_kind"),
                    MemoryLayout.paddingLayout(4));
    public static final long ACTOR_ROUTE_ACTOR_OFFSET =
            ACTOR_ROUTE_LAYOUT.byteOffset(PathElement.groupElement("actor"));
    public static final long ACTOR_ROUTE_CURRENT_SPOT_RID_OFFSET =
            ACTOR_ROUTE_LAYOUT.byteOffset(
                    PathElement.groupElement("current_spot_rid"));
    public static final long ACTOR_ROUTE_CURRENT_SPOT_KIND_OFFSET =
            ACTOR_ROUTE_LAYOUT.byteOffset(
                    PathElement.groupElement("current_spot_kind"));

    public static final MemoryLayout SPOT_ROUTE_LAYOUT =
            MemoryLayout.structLayout(
                    ROUTING_ID_LAYOUT.withName("spot_rid"),
                    ROUTING_ID_LAYOUT.withName("owner_node_rid"),
                    ValueLayout.JAVA_INT.withName("spot_kind"),
                    MemoryLayout.paddingLayout(4));
    public static final long SPOT_ROUTE_SPOT_RID_OFFSET =
            SPOT_ROUTE_LAYOUT.byteOffset(PathElement.groupElement("spot_rid"));
    public static final long SPOT_ROUTE_OWNER_NODE_RID_OFFSET =
            SPOT_ROUTE_LAYOUT.byteOffset(
                    PathElement.groupElement("owner_node_rid"));
    public static final long SPOT_ROUTE_SPOT_KIND_OFFSET =
            SPOT_ROUTE_LAYOUT.byteOffset(PathElement.groupElement("spot_kind"));

    public static final MemoryLayout ACTOR_JOIN_RESULT_LAYOUT =
            MemoryLayout.structLayout(
                    ValueLayout.JAVA_INT.withName("result"),
                    ValueLayout.JAVA_INT.withName("join_result_code"),
                    ACTOR_REF_LAYOUT.withName("actor"),
                    ROUTING_ID_LAYOUT.withName("joined_spot_rid"),
                    ValueLayout.JAVA_LONG_UNALIGNED.withName("join_epoch"),
                    ValueLayout.JAVA_INT.withName("flags"),
                    MemoryLayout.paddingLayout(4));
    public static final long ACTOR_JOIN_RESULT_RESULT_OFFSET =
            ACTOR_JOIN_RESULT_LAYOUT.byteOffset(
                    PathElement.groupElement("result"));
    public static final long ACTOR_JOIN_RESULT_JOIN_RESULT_CODE_OFFSET =
            ACTOR_JOIN_RESULT_LAYOUT.byteOffset(
                    PathElement.groupElement("join_result_code"));
    public static final long ACTOR_JOIN_RESULT_ACTOR_OFFSET =
            ACTOR_JOIN_RESULT_LAYOUT.byteOffset(
                    PathElement.groupElement("actor"));
    public static final long ACTOR_JOIN_RESULT_JOINED_SPOT_RID_OFFSET =
            ACTOR_JOIN_RESULT_LAYOUT.byteOffset(
                    PathElement.groupElement("joined_spot_rid"));
    public static final long ACTOR_JOIN_RESULT_JOIN_EPOCH_OFFSET =
            ACTOR_JOIN_RESULT_LAYOUT.byteOffset(
                    PathElement.groupElement("join_epoch"));
    public static final long ACTOR_JOIN_RESULT_FLAGS_OFFSET =
            ACTOR_JOIN_RESULT_LAYOUT.byteOffset(
                    PathElement.groupElement("flags"));

    public static final MemoryLayout ACTOR_JOIN_ENTRY_SPOT_RESULT_LAYOUT =
            MemoryLayout.structLayout(
                    ValueLayout.JAVA_INT.withName("result"),
                    MemoryLayout.paddingLayout(4),
                    ACTOR_REF_LAYOUT.withName("actor"),
                    ROUTING_ID_LAYOUT.withName("target_node_rid"),
                    ValueLayout.JAVA_LONG_UNALIGNED.withName("join_epoch"),
                    ValueLayout.JAVA_INT.withName("flags"),
                    MemoryLayout.paddingLayout(4));
    public static final long ACTOR_JOIN_ENTRY_SPOT_RESULT_RESULT_OFFSET =
            ACTOR_JOIN_ENTRY_SPOT_RESULT_LAYOUT.byteOffset(
                    PathElement.groupElement("result"));
    public static final long ACTOR_JOIN_ENTRY_SPOT_RESULT_ACTOR_OFFSET =
            ACTOR_JOIN_ENTRY_SPOT_RESULT_LAYOUT.byteOffset(
                    PathElement.groupElement("actor"));
    public static final long ACTOR_JOIN_ENTRY_SPOT_RESULT_TARGET_NODE_RID_OFFSET =
            ACTOR_JOIN_ENTRY_SPOT_RESULT_LAYOUT.byteOffset(
                    PathElement.groupElement("target_node_rid"));
    public static final long ACTOR_JOIN_ENTRY_SPOT_RESULT_JOIN_EPOCH_OFFSET =
            ACTOR_JOIN_ENTRY_SPOT_RESULT_LAYOUT.byteOffset(
                    PathElement.groupElement("join_epoch"));
    public static final long ACTOR_JOIN_ENTRY_SPOT_RESULT_FLAGS_OFFSET =
            ACTOR_JOIN_ENTRY_SPOT_RESULT_LAYOUT.byteOffset(
                    PathElement.groupElement("flags"));

    public static final MemoryLayout ACTOR_LOOKUP_RESULT_LAYOUT =
            MemoryLayout.structLayout(
                    ValueLayout.JAVA_INT.withName("result"),
                    MemoryLayout.paddingLayout(4),
                    ACTOR_REF_LAYOUT.withName("actor"),
                    ValueLayout.JAVA_INT.withName("flags"),
                    MemoryLayout.paddingLayout(4));
    public static final long ACTOR_LOOKUP_RESULT_RESULT_OFFSET =
            ACTOR_LOOKUP_RESULT_LAYOUT.byteOffset(
                    PathElement.groupElement("result"));
    public static final long ACTOR_LOOKUP_RESULT_ACTOR_OFFSET =
            ACTOR_LOOKUP_RESULT_LAYOUT.byteOffset(
                    PathElement.groupElement("actor"));
    public static final long ACTOR_LOOKUP_RESULT_FLAGS_OFFSET =
            ACTOR_LOOKUP_RESULT_LAYOUT.byteOffset(
                    PathElement.groupElement("flags"));

    public static final MemoryLayout SPOT_ACTOR_LIFECYCLE_INFO_LAYOUT =
            MemoryLayout.structLayout(
                    ACTOR_REF_LAYOUT.withName("previous_actor"),
                    ACTOR_REF_LAYOUT.withName("current_actor"),
                    ROUTING_ID_LAYOUT.withName("previous_spot_rid"),
                    ROUTING_ID_LAYOUT.withName("current_spot_rid"),
                    ValueLayout.JAVA_LONG_UNALIGNED.withName("join_epoch"),
                    ValueLayout.JAVA_INT.withName("flags"),
                    MemoryLayout.paddingLayout(4));
    public static final long SPOT_ACTOR_LIFECYCLE_INFO_PREVIOUS_ACTOR_OFFSET =
            SPOT_ACTOR_LIFECYCLE_INFO_LAYOUT.byteOffset(
                    PathElement.groupElement("previous_actor"));
    public static final long SPOT_ACTOR_LIFECYCLE_INFO_CURRENT_ACTOR_OFFSET =
            SPOT_ACTOR_LIFECYCLE_INFO_LAYOUT.byteOffset(
                    PathElement.groupElement("current_actor"));
    public static final long SPOT_ACTOR_LIFECYCLE_INFO_PREVIOUS_SPOT_RID_OFFSET =
            SPOT_ACTOR_LIFECYCLE_INFO_LAYOUT.byteOffset(
                    PathElement.groupElement("previous_spot_rid"));
    public static final long SPOT_ACTOR_LIFECYCLE_INFO_CURRENT_SPOT_RID_OFFSET =
            SPOT_ACTOR_LIFECYCLE_INFO_LAYOUT.byteOffset(
                    PathElement.groupElement("current_spot_rid"));
    public static final long SPOT_ACTOR_LIFECYCLE_INFO_JOIN_EPOCH_OFFSET =
            SPOT_ACTOR_LIFECYCLE_INFO_LAYOUT.byteOffset(
                    PathElement.groupElement("join_epoch"));
    public static final long SPOT_ACTOR_LIFECYCLE_INFO_FLAGS_OFFSET =
            SPOT_ACTOR_LIFECYCLE_INFO_LAYOUT.byteOffset(
                    PathElement.groupElement("flags"));

    public static final MemoryLayout SPOT_NODE_STATUS_LAYOUT =
            MemoryLayout.structLayout(
                    MemoryLayout.sequenceLayout(256, ValueLayout.JAVA_BYTE).withName("channel_name"),
                    MemoryLayout.sequenceLayout(256, ValueLayout.JAVA_BYTE).withName("local_endpoint"),
                    ROUTING_ID_LAYOUT.withName("node_routing_id"),
                    ValueLayout.JAVA_INT.withName("state"),
                    ValueLayout.JAVA_INT.withName("configured_peer_count"),
                    ValueLayout.JAVA_INT.withName("active_peer_count"),
                    ValueLayout.JAVA_INT.withName("connected_peer_count"),
                    ValueLayout.JAVA_INT.withName("subject_count"),
                    ValueLayout.JAVA_INT.withName("ready_subject_count"),
                    ValueLayout.JAVA_INT.withName("disconnected_sub_target_count"),
                    ValueLayout.JAVA_INT.withName("disconnected_routed_target_count"),
                    ValueLayout.JAVA_INT.withName("last_error"),
                    MemoryLayout.paddingLayout(4),
                    ValueLayout.JAVA_LONG_UNALIGNED.withName("last_changed_ms"));

    public static final MemoryLayout SPOT_NODE_PEER_ENTRY_LAYOUT =
            MemoryLayout.structLayout(
                    MemoryLayout.sequenceLayout(256, ValueLayout.JAVA_BYTE).withName("channel_name"),
                    MemoryLayout.sequenceLayout(256, ValueLayout.JAVA_BYTE).withName("local_endpoint"),
                    MemoryLayout.sequenceLayout(256, ValueLayout.JAVA_BYTE).withName("peer_endpoint"),
                    ValueLayout.JAVA_INT.withName("source"),
                    ValueLayout.JAVA_INT.withName("state"),
                    ValueLayout.JAVA_INT.withName("weight"),
                    MemoryLayout.paddingLayout(4),
                    ValueLayout.JAVA_LONG_UNALIGNED.withName("connected_since_ms"),
                    ValueLayout.JAVA_LONG_UNALIGNED.withName("last_changed_ms"));

    public static final MemoryLayout SPOT_SERVICE_ATTACHMENT_STATS_LAYOUT =
            MemoryLayout.structLayout(
                    MemoryLayout.sequenceLayout(256, ValueLayout.JAVA_BYTE).withName("channel_name"),
                    ValueLayout.JAVA_INT.withName("router_count"),
                    ValueLayout.JAVA_INT.withName("pub_count"),
                    ValueLayout.JAVA_INT.withName("sub_count"),
                    ValueLayout.JAVA_INT.withName("auto_router_count"),
                    ValueLayout.JAVA_INT.withName("auto_pub_count"),
                    ValueLayout.JAVA_INT.withName("auto_sub_count"));

    public static final MemoryLayout SPOT_SERVICE_MONITOR_EVENT_LAYOUT =
            MemoryLayout.structLayout(
                    MemoryLayout.sequenceLayout(256, ValueLayout.JAVA_BYTE).withName("channel_name"),
                    ValueLayout.JAVA_INT.withName("role"),
                    MONITOR_EVENT_LAYOUT.withName("event"));

    public static final MemoryLayout SPOT_NODE_PEER_FILTER_LAYOUT =
            MemoryLayout.structLayout(
                    MemoryLayout.sequenceLayout(256, ValueLayout.JAVA_BYTE).withName("peer_endpoint"),
                    ValueLayout.JAVA_INT.withName("source"),
                    ValueLayout.JAVA_INT.withName("state"));

    public static final MemoryLayout SPOT_NODE_SUBJECT_ENTRY_LAYOUT =
            MemoryLayout.structLayout(
                    ValueLayout.JAVA_INT.withName("role"),
                    MemoryLayout.sequenceLayout(256, ValueLayout.JAVA_BYTE).withName("subject"),
                    ValueLayout.JAVA_INT.withName("subject_kind"),
                    ValueLayout.JAVA_INT.withName("ready_peer_count"),
                    ValueLayout.JAVA_INT.withName("active_peer_count"),
                    MemoryLayout.paddingLayout(8),
                    ValueLayout.JAVA_LONG_UNALIGNED.withName("last_changed_ms"));

    public static final MemoryLayout SPOT_NODE_SUBJECT_FILTER_LAYOUT =
            MemoryLayout.structLayout(
                    ValueLayout.JAVA_INT.withName("role"),
                    MemoryLayout.sequenceLayout(256, ValueLayout.JAVA_BYTE).withName("subject"),
                    ValueLayout.JAVA_INT.withName("subject_kind"));

    public static final MemoryLayout SPOT_NODE_OPTIONS_LAYOUT =
            MemoryLayout.structLayout(ValueLayout.JAVA_INT.withName("mode"));

    public static final MemoryLayout SPOT_NODE_SOCKET_SNAPSHOT_FILTER_LAYOUT =
            MemoryLayout.structLayout(
                    ValueLayout.JAVA_INT.withName("owner"),
                    ValueLayout.JAVA_INT.withName("socket_type"),
                    MemoryLayout.sequenceLayout(64, ValueLayout.JAVA_BYTE).withName("socket_name"));

    public static final MemoryLayout SPOT_NODE_SOCKET_SNAPSHOT_ENTRY_LAYOUT =
            MemoryLayout.structLayout(
                    ValueLayout.JAVA_INT.withName("owner"),
                    MemoryLayout.paddingLayout(4),
                    ValueLayout.JAVA_LONG_UNALIGNED.withName("owner_id"),
                    MemoryLayout.sequenceLayout(64, ValueLayout.JAVA_BYTE).withName("owner_name"),
                    MemoryLayout.sequenceLayout(64, ValueLayout.JAVA_BYTE).withName("socket_name"),
                    ValueLayout.JAVA_INT.withName("socket_type"),
                    ValueLayout.JAVA_INT.withName("auto_hwm_visible"),
                    MONITOR_SNAPSHOT_LAYOUT.withName("snapshot"));

    public static final MemoryLayout SPOT_NODE_SPOT_ENTRY_LAYOUT =
            MemoryLayout.structLayout(
                    ROUTING_ID_LAYOUT.withName("spot_rid"),
                    ValueLayout.JAVA_INT.withName("spot_kind"),
                    ValueLayout.JAVA_INT.withName("dispatch_handler_attached"),
                    ValueLayout.JAVA_INT.withName("joined_actor_count"),
                    ValueLayout.JAVA_INT.withName("pending_actor_join_count"),
                    ValueLayout.JAVA_INT.withName("route_synced"),
                    MemoryLayout.paddingLayout(4),
                    ValueLayout.JAVA_LONG_UNALIGNED.withName("last_changed_ms"));
    public static final long SPOT_NODE_SPOT_ENTRY_SPOT_RID_OFFSET =
            SPOT_NODE_SPOT_ENTRY_LAYOUT.byteOffset(
                    PathElement.groupElement("spot_rid"));
    public static final long SPOT_NODE_SPOT_ENTRY_DISPATCH_HANDLER_ATTACHED_OFFSET =
            SPOT_NODE_SPOT_ENTRY_LAYOUT.byteOffset(
                    PathElement.groupElement("dispatch_handler_attached"));
    public static final long SPOT_NODE_SPOT_ENTRY_SPOT_KIND_OFFSET =
            SPOT_NODE_SPOT_ENTRY_LAYOUT.byteOffset(
                    PathElement.groupElement("spot_kind"));
    public static final long SPOT_NODE_SPOT_ENTRY_JOINED_ACTOR_COUNT_OFFSET =
            SPOT_NODE_SPOT_ENTRY_LAYOUT.byteOffset(
                    PathElement.groupElement("joined_actor_count"));
    public static final long SPOT_NODE_SPOT_ENTRY_PENDING_ACTOR_JOIN_COUNT_OFFSET =
            SPOT_NODE_SPOT_ENTRY_LAYOUT.byteOffset(
                    PathElement.groupElement("pending_actor_join_count"));
    public static final long SPOT_NODE_SPOT_ENTRY_ROUTE_SYNCED_OFFSET =
            SPOT_NODE_SPOT_ENTRY_LAYOUT.byteOffset(
                    PathElement.groupElement("route_synced"));
    public static final long SPOT_NODE_SPOT_ENTRY_LAST_CHANGED_MS_OFFSET =
            SPOT_NODE_SPOT_ENTRY_LAYOUT.byteOffset(
                    PathElement.groupElement("last_changed_ms"));

    public static final MemoryLayout SPOT_NODE_ACTOR_ENTRY_LAYOUT =
            MemoryLayout.structLayout(
                    ACTOR_REF_LAYOUT.withName("actor"),
                    ROUTING_ID_LAYOUT.withName("current_spot_rid"),
                    ValueLayout.JAVA_INT.withName("current_spot_kind"),
                    ValueLayout.JAVA_INT.withName("route_synced"),
                    ValueLayout.JAVA_INT.withName("pending_message_count"),
                    MemoryLayout.paddingLayout(4),
                    ValueLayout.JAVA_LONG_UNALIGNED.withName("last_changed_ms"));
    public static final long SPOT_NODE_ACTOR_ENTRY_ACTOR_OFFSET =
            SPOT_NODE_ACTOR_ENTRY_LAYOUT.byteOffset(
                    PathElement.groupElement("actor"));
    public static final long SPOT_NODE_ACTOR_ENTRY_CURRENT_SPOT_RID_OFFSET =
            SPOT_NODE_ACTOR_ENTRY_LAYOUT.byteOffset(
                    PathElement.groupElement("current_spot_rid"));
    public static final long SPOT_NODE_ACTOR_ENTRY_CURRENT_SPOT_KIND_OFFSET =
            SPOT_NODE_ACTOR_ENTRY_LAYOUT.byteOffset(
                    PathElement.groupElement("current_spot_kind"));
    public static final long SPOT_NODE_ACTOR_ENTRY_ROUTE_SYNCED_OFFSET =
            SPOT_NODE_ACTOR_ENTRY_LAYOUT.byteOffset(
                    PathElement.groupElement("route_synced"));
    public static final long SPOT_NODE_ACTOR_ENTRY_PENDING_MESSAGE_COUNT_OFFSET =
            SPOT_NODE_ACTOR_ENTRY_LAYOUT.byteOffset(
                    PathElement.groupElement("pending_message_count"));
    public static final long SPOT_NODE_ACTOR_ENTRY_LAST_CHANGED_MS_OFFSET =
            SPOT_NODE_ACTOR_ENTRY_LAYOUT.byteOffset(
                    PathElement.groupElement("last_changed_ms"));

    public static final MemoryLayout REGISTRY_STATUS_LAYOUT =
            MemoryLayout.structLayout(
                    ValueLayout.JAVA_INT.withName("registry_id"),
                    MemoryLayout.sequenceLayout(256, ValueLayout.JAVA_BYTE).withName("bind_endpoint"),
                    ValueLayout.JAVA_INT.withName("state"),
                    ValueLayout.JAVA_INT.withName("topology_entry_count"),
                    ValueLayout.JAVA_INT.withName("peer_registry_count"),
                    ValueLayout.JAVA_INT.withName("connected_peer_registry_count"),
                    MemoryLayout.paddingLayout(4),
                    ValueLayout.JAVA_LONG_UNALIGNED.withName("list_seq"),
                    ValueLayout.JAVA_INT.withName("last_error"),
                    MemoryLayout.paddingLayout(4),
                    ValueLayout.JAVA_LONG_UNALIGNED.withName("last_changed_ms"));

    public static final MemoryLayout REGISTRY_SERVICE_SUMMARY_ENTRY_LAYOUT =
            MemoryLayout.structLayout(
                    ValueLayout.JAVA_INT.withName("auto_connect_type"),
                    ValueLayout.JAVA_INT.withName("service_role"),
                    MemoryLayout.sequenceLayout(256, ValueLayout.JAVA_BYTE).withName("channel_name"),
                    ValueLayout.JAVA_INT.withName("total_count"),
                    ValueLayout.JAVA_INT.withName("connecting_count"),
                    ValueLayout.JAVA_INT.withName("ready_count"),
                    ValueLayout.JAVA_INT.withName("error_count"),
                    ValueLayout.JAVA_INT.withName("stopped_count"),
                    MemoryLayout.paddingLayout(4),
                    ValueLayout.JAVA_LONG_UNALIGNED.withName("last_reported_ms"));

    public static final MemoryLayout REGISTRY_SERVICE_SUMMARY_FILTER_LAYOUT =
            MemoryLayout.structLayout(
                    ValueLayout.JAVA_INT.withName("auto_connect_type"),
                    ValueLayout.JAVA_INT.withName("service_role"),
                    MemoryLayout.sequenceLayout(256, ValueLayout.JAVA_BYTE).withName("channel_name"));

    public static final MemoryLayout MEMBER_PEER_ENTRY_LAYOUT =
            MemoryLayout.structLayout(
                    ValueLayout.JAVA_INT.withName("auto_connect_type"),
                    ValueLayout.JAVA_INT.withName("service_role"),
                    MemoryLayout.sequenceLayout(256, ValueLayout.JAVA_BYTE).withName("channel_name"),
                    MemoryLayout.sequenceLayout(256, ValueLayout.JAVA_BYTE).withName("endpoint"),
                    ValueLayout.JAVA_INT.withName("weight"),
                    ROUTING_ID_LAYOUT.withName("routing_id"),
                    MemoryLayout.paddingLayout(4),
                    ValueLayout.JAVA_LONG_UNALIGNED.withName("value"));
    public static final long MEMBER_PEER_AUTO_CONNECT_TYPE_OFFSET =
            MEMBER_PEER_ENTRY_LAYOUT.byteOffset(
                    PathElement.groupElement("auto_connect_type"));
    public static final long MEMBER_PEER_SERVICE_ROLE_OFFSET =
            MEMBER_PEER_ENTRY_LAYOUT.byteOffset(
                    PathElement.groupElement("service_role"));
    public static final long MEMBER_PEER_CHANNEL_NAME_OFFSET =
            MEMBER_PEER_ENTRY_LAYOUT.byteOffset(
                    PathElement.groupElement("channel_name"));
    public static final long MEMBER_PEER_ENDPOINT_OFFSET =
            MEMBER_PEER_ENTRY_LAYOUT.byteOffset(
                    PathElement.groupElement("endpoint"));
    public static final long MEMBER_PEER_ROUTING_ID_OFFSET =
            MEMBER_PEER_ENTRY_LAYOUT.byteOffset(
                    PathElement.groupElement("routing_id"));
    public static final long MEMBER_PEER_WEIGHT_OFFSET =
            MEMBER_PEER_ENTRY_LAYOUT.byteOffset(
                    PathElement.groupElement("weight"));
    public static final long MEMBER_PEER_VALUE_OFFSET =
            MEMBER_PEER_ENTRY_LAYOUT.byteOffset(
                    PathElement.groupElement("value"));

    public static final MemoryLayout REGISTRY_TOPOLOGY_ENTRY_LAYOUT =
            MemoryLayout.structLayout(
                    ValueLayout.JAVA_INT.withName("auto_connect_type"),
                    ROUTING_ID_LAYOUT.withName("routing_id"),
                    ValueLayout.JAVA_INT.withName("service_kind"),
                    ValueLayout.JAVA_INT.withName("service_role"),
                    MemoryLayout.sequenceLayout(256, ValueLayout.JAVA_BYTE).withName("channel_name"),
                    MemoryLayout.sequenceLayout(256, ValueLayout.JAVA_BYTE).withName("endpoint"),
                    ValueLayout.JAVA_INT.withName("source"),
                    ValueLayout.JAVA_INT.withName("state"),
                    ValueLayout.JAVA_INT.withName("desired_count"),
                    ValueLayout.JAVA_INT.withName("ready_count"),
                    ValueLayout.JAVA_INT.withName("error_code"),
                    ValueLayout.JAVA_LONG_UNALIGNED.withName("last_reported_ms"),
                    ValueLayout.JAVA_INT.withName("spot_kind"),
                    MemoryLayout.paddingLayout(4));

    public static final MemoryLayout REGISTRY_TOPOLOGY_FILTER_LAYOUT =
            MemoryLayout.structLayout(
                    ValueLayout.JAVA_INT.withName("auto_connect_type"),
                    ValueLayout.JAVA_INT.withName("service_kind"),
                    ValueLayout.JAVA_INT.withName("service_role"),
                    MemoryLayout.sequenceLayout(256, ValueLayout.JAVA_BYTE).withName("channel_name"),
                    ROUTING_ID_LAYOUT.withName("routing_id"),
                    ValueLayout.JAVA_INT.withName("state"),
                    ValueLayout.JAVA_INT.withName("source"));
}
