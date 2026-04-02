/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import dev.kairoscode.zlink.options.SocketOptions;

public final class SubSocketOptions extends CommonSocketOptions {
    SubSocketOptions(Socket socket) {
        super(socket);
    }

    public int topicsCount() {
        return socket.getOption(SocketOptions.TOPICS_COUNT);
    }
}
