package dev.kairoscode.zlink.internal;

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
                    ValueLayout.JAVA_LONG_UNALIGNED.withName("rcv_pending_msgs"));
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

    public static final MemoryLayout SERVICE_MONITOR_OPEN_OPTIONS_LAYOUT =
            MemoryLayout.structLayout(
                    ValueLayout.JAVA_INT.withName("events"));
    public static final long SERVICE_MONITOR_OPEN_EVENTS_OFFSET =
            SERVICE_MONITOR_OPEN_OPTIONS_LAYOUT.byteOffset(
                    PathElement.groupElement("events"));

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
            MemoryLayout.sequenceLayout(256, ValueLayout.JAVA_BYTE).withName("service_name"),
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
    public static final long SERVICE_EVENT_SERVICE_NAME_OFFSET =
            SERVICE_EVENT_LAYOUT.byteOffset(
                    PathElement.groupElement("service_name"));
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

    public static final MemoryLayout SPOT_NODE_STATUS_LAYOUT =
            MemoryLayout.structLayout(
                    MemoryLayout.sequenceLayout(256, ValueLayout.JAVA_BYTE).withName("service_name"),
                    MemoryLayout.sequenceLayout(256, ValueLayout.JAVA_BYTE).withName("local_endpoint"),
                    ROUTING_ID_LAYOUT.withName("node_routing_id"),
                    ValueLayout.JAVA_INT.withName("state"),
                    ValueLayout.JAVA_INT.withName("configured_peer_count"),
                    ValueLayout.JAVA_INT.withName("active_peer_count"),
                    ValueLayout.JAVA_INT.withName("connected_peer_count"),
                    ValueLayout.JAVA_INT.withName("subject_count"),
                    ValueLayout.JAVA_INT.withName("ready_subject_count"),
                    ValueLayout.JAVA_INT.withName("last_error"),
                    MemoryLayout.paddingLayout(4),
                    ValueLayout.JAVA_LONG_UNALIGNED.withName("last_changed_ms"));

    public static final MemoryLayout SPOT_NODE_PEER_ENTRY_LAYOUT =
            MemoryLayout.structLayout(
                    MemoryLayout.sequenceLayout(256, ValueLayout.JAVA_BYTE).withName("service_name"),
                    MemoryLayout.sequenceLayout(256, ValueLayout.JAVA_BYTE).withName("local_endpoint"),
                    MemoryLayout.sequenceLayout(256, ValueLayout.JAVA_BYTE).withName("peer_endpoint"),
                    ValueLayout.JAVA_INT.withName("source"),
                    ValueLayout.JAVA_INT.withName("state"),
                    ValueLayout.JAVA_INT.withName("admission_state"),
                    MemoryLayout.paddingLayout(4),
                    ValueLayout.JAVA_LONG_UNALIGNED.withName("connected_since_ms"),
                    ValueLayout.JAVA_LONG_UNALIGNED.withName("last_changed_ms"));

    public static final MemoryLayout SPOT_SERVICE_ATTACHMENT_STATS_LAYOUT =
            MemoryLayout.structLayout(
                    MemoryLayout.sequenceLayout(256, ValueLayout.JAVA_BYTE).withName("service_name"),
                    ValueLayout.JAVA_INT.withName("router_count"),
                    ValueLayout.JAVA_INT.withName("pub_count"),
                    ValueLayout.JAVA_INT.withName("sub_count"),
                    ValueLayout.JAVA_INT.withName("auto_router_count"),
                    ValueLayout.JAVA_INT.withName("auto_pub_count"),
                    ValueLayout.JAVA_INT.withName("auto_sub_count"));

    public static final MemoryLayout SPOT_SERVICE_MONITOR_EVENT_LAYOUT =
            MemoryLayout.structLayout(
                    MemoryLayout.sequenceLayout(256, ValueLayout.JAVA_BYTE).withName("service_name"),
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
                    ValueLayout.JAVA_INT.withName("service_kind"),
                    ValueLayout.JAVA_INT.withName("service_role"),
                    MemoryLayout.sequenceLayout(256, ValueLayout.JAVA_BYTE).withName("service_name"),
                    ValueLayout.JAVA_INT.withName("total_count"),
                    ValueLayout.JAVA_INT.withName("connecting_count"),
                    ValueLayout.JAVA_INT.withName("ready_count"),
                    ValueLayout.JAVA_INT.withName("error_count"),
                    ValueLayout.JAVA_INT.withName("stopped_count"),
                    MemoryLayout.paddingLayout(4),
                    ValueLayout.JAVA_LONG_UNALIGNED.withName("last_reported_ms"));

    public static final MemoryLayout REGISTRY_SERVICE_SUMMARY_FILTER_LAYOUT =
            MemoryLayout.structLayout(
                    ValueLayout.JAVA_INT.withName("service_kind"),
                    ValueLayout.JAVA_INT.withName("service_role"),
                    MemoryLayout.sequenceLayout(256, ValueLayout.JAVA_BYTE).withName("service_name"));

    public static final MemoryLayout MEMBER_PEER_ENTRY_LAYOUT =
            MemoryLayout.structLayout(
                    ValueLayout.JAVA_SHORT.withName("service_type"),
                    ValueLayout.JAVA_SHORT.withName("service_role"),
                    MemoryLayout.sequenceLayout(256, ValueLayout.JAVA_BYTE).withName("service_name"),
                    MemoryLayout.sequenceLayout(256, ValueLayout.JAVA_BYTE).withName("endpoint"),
                    ROUTING_ID_LAYOUT.withName("routing_id"),
                    ValueLayout.JAVA_INT.withName("admission_state"),
                    MemoryLayout.paddingLayout(8),
                    ValueLayout.JAVA_LONG_UNALIGNED.withName("value"));
    public static final long MEMBER_PEER_SERVICE_TYPE_OFFSET =
            MEMBER_PEER_ENTRY_LAYOUT.byteOffset(
                    PathElement.groupElement("service_type"));
    public static final long MEMBER_PEER_SERVICE_ROLE_OFFSET =
            MEMBER_PEER_ENTRY_LAYOUT.byteOffset(
                    PathElement.groupElement("service_role"));
    public static final long MEMBER_PEER_SERVICE_NAME_OFFSET =
            MEMBER_PEER_ENTRY_LAYOUT.byteOffset(
                    PathElement.groupElement("service_name"));
    public static final long MEMBER_PEER_ENDPOINT_OFFSET =
            MEMBER_PEER_ENTRY_LAYOUT.byteOffset(
                    PathElement.groupElement("endpoint"));
    public static final long MEMBER_PEER_ROUTING_ID_OFFSET =
            MEMBER_PEER_ENTRY_LAYOUT.byteOffset(
                    PathElement.groupElement("routing_id"));
    public static final long MEMBER_PEER_ADMISSION_STATE_OFFSET =
            MEMBER_PEER_ENTRY_LAYOUT.byteOffset(
                    PathElement.groupElement("admission_state"));
    public static final long MEMBER_PEER_VALUE_OFFSET =
            MEMBER_PEER_ENTRY_LAYOUT.byteOffset(
                    PathElement.groupElement("value"));

    public static final MemoryLayout REGISTRY_TOPOLOGY_ENTRY_LAYOUT =
            MemoryLayout.structLayout(
                    ROUTING_ID_LAYOUT.withName("routing_id"),
                    ValueLayout.JAVA_INT.withName("service_kind"),
                    ValueLayout.JAVA_INT.withName("service_role"),
                    MemoryLayout.sequenceLayout(256, ValueLayout.JAVA_BYTE).withName("service_name"),
                    MemoryLayout.sequenceLayout(256, ValueLayout.JAVA_BYTE).withName("endpoint"),
                    ValueLayout.JAVA_INT.withName("source"),
                    ValueLayout.JAVA_INT.withName("state"),
                    ValueLayout.JAVA_INT.withName("desired_count"),
                    ValueLayout.JAVA_INT.withName("ready_count"),
                    ValueLayout.JAVA_INT.withName("error_code"),
                    MemoryLayout.paddingLayout(4),
                    ValueLayout.JAVA_LONG_UNALIGNED.withName("last_reported_ms"));

    public static final MemoryLayout REGISTRY_TOPOLOGY_FILTER_LAYOUT =
            MemoryLayout.structLayout(
                    ValueLayout.JAVA_INT.withName("service_kind"),
                    ValueLayout.JAVA_INT.withName("service_role"),
                    MemoryLayout.sequenceLayout(256, ValueLayout.JAVA_BYTE).withName("service_name"),
                    ROUTING_ID_LAYOUT.withName("routing_id"),
                    ValueLayout.JAVA_INT.withName("state"),
                    ValueLayout.JAVA_INT.withName("source"));
}
