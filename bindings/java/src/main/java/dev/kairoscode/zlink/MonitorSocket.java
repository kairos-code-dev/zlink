/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import dev.kairoscode.zlink.internal.Native;
import java.lang.foreign.MemorySegment;

public final class MonitorSocket implements AutoCloseable {
    private final Socket socket;

    MonitorSocket(Socket socket) {
        this.socket = socket;
    }

    public MonitorEvent recv(ReceiveFlag flag) {
        if (flag == null)
            throw new IllegalArgumentException("flag is null");
        return Native.monitorRecv(socket.handle(), flag.getValue());
    }

    @Override
    public void close() {
        socket.close();
    }
}
