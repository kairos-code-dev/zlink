/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.sockets;

import systems.zlink.contracts.internal.ContractAccess;

public final class SubSocketOptions extends CommonSocketOptions {
    public SubSocketOptions(Socket socket) {
        super(socket);
    }

    public int topicsCount() {
        return ContractAccess.socketGetOption(socket, SocketOptions.TOPICS_COUNT);
    }

}
