/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.eventing;

import systems.zlink.contracts.sockets.RecvFlags;

public interface MonitorSocket extends AutoCloseable {
    public static final SocketMonitorHandler IGNORE_HANDLER = event -> {
    };

    public abstract void onEvent(SocketMonitorHandler handler);

    public abstract MonitorEvent recv();

    public abstract MonitorEvent recv(RecvFlags flags);

    public abstract MonitorStatus status();

    @Override
    public abstract void close();
}
