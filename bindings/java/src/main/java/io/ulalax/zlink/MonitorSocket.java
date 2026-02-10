/* SPDX-License-Identifier: MPL-2.0 */

package io.ulalax.zlink;

import io.ulalax.zlink.internal.Native;
import java.lang.foreign.MemorySegment;

public final class MonitorSocket implements AutoCloseable {
    private final Socket socket;

    MonitorSocket(Socket socket) {
        this.socket = socket;
    }

    public MonitorEvent recv(int flags) {
        return Native.monitorRecv(socket.handle(), flags);
    }

    @Override
    public void close() {
        socket.close();
    }
}
