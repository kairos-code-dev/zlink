/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import dev.kairoscode.zlink.internal.Native;
import dev.kairoscode.zlink.options.SocketOptionKey;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.util.Optional;

public final class MonitorSocket implements AutoCloseable {
    private MemorySegment handle;
    private final boolean own;

    MonitorSocket(MemorySegment handle, boolean own) {
        this.handle = handle;
        this.own = own;
    }

    public MonitorEvent recv() {
        ensureOpen();
        return Native.monitorRecv(handle, ReceiveFlag.NONE.getValue());
    }

    public Optional<MonitorEvent> tryRecv() {
        ensureOpen();
        MonitorEvent event = Native.monitorRecv(handle,
            ReceiveFlag.DONTWAIT.getValue());
        if (event != null)
            return Optional.of(event);
        int errno = Native.errno();
        if (errno == Socket.ERRNO_EAGAIN || errno == Socket.ERRNO_EWOULDBLOCK_WIN)
            return Optional.empty();
        throw ZlinkException.fromLastError("zlink_monitor_recv");
    }

    void setOption(SocketOptionKey<Integer> option, int value) {
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
