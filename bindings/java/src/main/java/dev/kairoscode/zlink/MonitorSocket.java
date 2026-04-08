/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import dev.kairoscode.zlink.internal.Native;
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
        try {
            return Optional.of(Native.monitorRecv(handle,
                ReceiveFlag.DONTWAIT.getValue()));
        } catch (ZlinkException ex) {
            int errno = ex.errno();
            if (errno == Socket.ERRNO_EAGAIN || errno == Socket.ERRNO_EWOULDBLOCK_WIN)
                return Optional.empty();
            throw ex;
        }
    }

    public MonitorSnapshot snapshot() {
        try (java.lang.foreign.Arena arena = java.lang.foreign.Arena.ofConfined()) {
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
