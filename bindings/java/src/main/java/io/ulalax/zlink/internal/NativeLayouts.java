package io.ulalax.zlink.internal;

import java.lang.foreign.MemoryLayout;
import java.lang.foreign.ValueLayout;

public final class NativeLayouts {
    private NativeLayouts() {}

    public static final MemoryLayout MSG_LAYOUT = MemoryLayout.sequenceLayout(64, ValueLayout.JAVA_BYTE);

    public static final MemoryLayout PROVIDER_INFO_LAYOUT = MemoryLayout.sequenceLayout(
            256 + 256 + 256 + 4 + 4 + 8, ValueLayout.JAVA_BYTE);
    public static final long PROVIDER_SERVICE_OFFSET = 0;
    public static final long PROVIDER_ENDPOINT_OFFSET = 256;
    public static final long PROVIDER_ROUTING_OFFSET = 512;
    public static final long PROVIDER_WEIGHT_OFFSET = 512 + 256;
    public static final long PROVIDER_REGISTERED_OFFSET = PROVIDER_WEIGHT_OFFSET + 8;

    public static final MemoryLayout MONITOR_EVENT_LAYOUT = MemoryLayout.sequenceLayout(
            8 + 8 + 256 + 256 + 256, ValueLayout.JAVA_BYTE);
    public static final long MONITOR_EVENT_OFFSET = 0;
    public static final long MONITOR_VALUE_OFFSET = 8;
    public static final long MONITOR_ROUTING_OFFSET = 16;
    public static final long MONITOR_LOCAL_OFFSET = 16 + 256;
    public static final long MONITOR_REMOTE_OFFSET = 16 + 512;
}
