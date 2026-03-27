/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import dev.kairoscode.zlink.internal.Native;
import dev.kairoscode.zlink.options.SocketOptionKey;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;

public final class MonitorSocket implements AutoCloseable {
    private MemorySegment handle;
    private final boolean own;

    MonitorSocket(MemorySegment handle, boolean own) {
        this.handle = handle;
        this.own = own;
    }

    public MonitorEvent recv() {
        return recv(ReceiveFlag.NONE);
    }

    public MonitorEvent recv(ReceiveFlag flag) {
        if (flag == null)
            throw new IllegalArgumentException("flag is null");
        ensureOpen();
        return Native.monitorRecv(handle, flag.getValue());
    }

    public void setOption(SocketOptionKey<Integer> option, int value) {
        ensureOpen();
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment valueBuf = arena.allocate(java.lang.foreign.ValueLayout.JAVA_INT);
            valueBuf.set(java.lang.foreign.ValueLayout.JAVA_INT, 0, value);
            int rc = Native.setSockOpt(handle, option.optionId(), valueBuf,
                java.lang.foreign.ValueLayout.JAVA_INT.byteSize());
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_setsockopt");
        }
    }

    public MonitorSnapshot snapshot() {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment out = arena.allocate(
              dev.kairoscode.zlink.internal.NativeLayouts.MONITOR_SNAPSHOT_LAYOUT);
            int rc = Native.monitorSnapshot(handle, out);
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_monitor_snapshot");
            return MonitorSnapshot.fromNative(out);
        }
    }

    MemorySegment handle() {
        return handle;
    }

    @Override
    public void close() {
        if (handle == null || handle.address() == 0)
            return;
        if (own)
            Native.close(handle);
        handle = MemorySegment.NULL;
    }

    private void ensureOpen() {
        if (handle == null || handle.address() == 0)
            throw new IllegalStateException("monitor socket is closed");
    }
}
