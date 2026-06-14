/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.sockets;

import systems.zlink.runtime.sockets.SocketOptions;

import systems.zlink.runtime.nativeapi.ContractAccess;

/** The typed facade over SUB/XSUB-specific socket options. */
public final class SubSocketOptions extends CommonSocketOptions {
    SubSocketOptions(Socket socket) {
        super(socket);
    }

    public int topicsCount() {
        return ContractAccess.socketGetOption(socket, SocketOptions.TOPICS_COUNT);
    }

}
