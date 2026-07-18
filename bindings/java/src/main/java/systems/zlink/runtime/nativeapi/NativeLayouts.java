package systems.zlink.runtime.nativeapi;

import java.lang.foreign.MemoryLayout;
import java.lang.foreign.MemoryLayout.PathElement;
import java.lang.foreign.ValueLayout;

public final class NativeLayouts {
    private NativeLayouts() {}

    public static final MemoryLayout MESSAGE_LAYOUT = MemoryLayout
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
                    ValueLayout.JAVA_INT.withName("auto_hwm_connection_bucket_enabled"),
                    ValueLayout.JAVA_INT.withName("auto_hwm_connection_bucket_count"),
                    ValueLayout.JAVA_INT.withName("auto_hwm_connection_bucket_index"),
                    ValueLayout.JAVA_INT.withName("auto_hwm_connection_bucket_hwm_4k"),
                    ValueLayout.JAVA_INT.withName("auto_hwm_connection_bucket_hysteresis_retained"),
                    MemoryLayout.paddingLayout(4),
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
    public static final long MONITOR_SNAPSHOT_AUTO_HWM_CONNECTION_BUCKET_ENABLED_OFFSET =
            MONITOR_SNAPSHOT_LAYOUT.byteOffset(
                    PathElement.groupElement("auto_hwm_connection_bucket_enabled"));
    public static final long MONITOR_SNAPSHOT_AUTO_HWM_CONNECTION_BUCKET_COUNT_OFFSET =
            MONITOR_SNAPSHOT_LAYOUT.byteOffset(
                    PathElement.groupElement("auto_hwm_connection_bucket_count"));
    public static final long MONITOR_SNAPSHOT_AUTO_HWM_CONNECTION_BUCKET_INDEX_OFFSET =
            MONITOR_SNAPSHOT_LAYOUT.byteOffset(
                    PathElement.groupElement("auto_hwm_connection_bucket_index"));
    public static final long MONITOR_SNAPSHOT_AUTO_HWM_CONNECTION_BUCKET_HWM_4K_OFFSET =
            MONITOR_SNAPSHOT_LAYOUT.byteOffset(
                    PathElement.groupElement("auto_hwm_connection_bucket_hwm_4k"));
    public static final long MONITOR_SNAPSHOT_AUTO_HWM_CONNECTION_BUCKET_HYSTERESIS_RETAINED_OFFSET =
            MONITOR_SNAPSHOT_LAYOUT.byteOffset(
                    PathElement.groupElement("auto_hwm_connection_bucket_hysteresis_retained"));
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

}
