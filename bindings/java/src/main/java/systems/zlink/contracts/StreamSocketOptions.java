/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts;

import systems.zlink.contracts.SocketOptions;

public final class StreamSocketOptions extends CommonSocketOptions {
    StreamSocketOptions(Socket socket) {
        super(socket);
    }

    public boolean notifyEnabled() {
        return socket.getOption(SocketOptions.STREAM_NOTIFY) != 0;
    }

    public void notify(boolean enabled) {
        socket.setOption(SocketOptions.STREAM_NOTIFY, enabled ? 1 : 0);
    }
}
