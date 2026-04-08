/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import dev.kairoscode.zlink.options.SocketOptions;

public final class DealerSocketOptions extends CommonSocketOptions {
    DealerSocketOptions(Socket socket) {
        super(socket);
    }

    public boolean probe() {
        return socket.getOption(SocketOptions.PROBE_ROUTER) != 0;
    }

    public void probe(boolean enabled) {
        socket.setOption(SocketOptions.PROBE_ROUTER, enabled ? 1 : 0);
    }
}
