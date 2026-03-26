/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import dev.kairoscode.zlink.internal.Native;
import dev.kairoscode.zlink.options.SocketOptionKey;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;

public final class MonitorSocket implements AutoCloseable {
    private final Socket socket;

    MonitorSocket(Socket socket) {
        this.socket = socket;
    }

    public MonitorEvent recv() {
        return recv(ReceiveFlag.NONE);
    }

    public MonitorEvent recv(ReceiveFlag flag) {
        if (flag == null)
            throw new IllegalArgumentException("flag is null");
        return Native.monitorRecv(socket.handle(), flag.getValue());
    }

    public void setOption(SocketOptionKey<Integer> option, int value) {
        socket.setOption(option, value);
    }

    public MonitorSnapshot snapshot() {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment out = arena.allocate(
              dev.kairoscode.zlink.internal.NativeLayouts.MONITOR_SNAPSHOT_LAYOUT);
            int rc = Native.monitorSnapshot(socket.handle(), out);
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_monitor_snapshot");
            return MonitorSnapshot.fromNative(out);
        }
    }

    public Socket socket() {
        return socket;
    }

    @Override
    public void close() {
        socket.close();
    }
}
