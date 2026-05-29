/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.eventing;

import systems.zlink.contracts.sockets.RecvFlags;

public interface SocketMonitor extends AutoCloseable {
    SocketMonitorHandler IGNORE_HANDLER = event -> {
    };

    void onEvent(SocketMonitorHandler handler);

    MonitorEvent recv();

    MonitorEvent recv(RecvFlags flags);

    MonitorStatus status();

    @Override
    void close();
}
