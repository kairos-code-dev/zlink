/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.sockets;

import systems.zlink.internal.ContractAccess;


public final class StreamSocketOptions extends CommonSocketOptions {
    public StreamSocketOptions(Socket socket) {
        super(socket);
    }

    public boolean notifyEnabled() {
        return ContractAccess.socketGetOption(socket, SocketOptions.STREAM_NOTIFY) != 0;
    }

    public void notify(boolean enabled) {
        ContractAccess.socketSetOption(socket, SocketOptions.STREAM_NOTIFY, enabled ? 1 : 0);
    }
}
