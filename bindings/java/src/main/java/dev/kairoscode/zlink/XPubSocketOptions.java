/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import java.util.Objects;
import dev.kairoscode.zlink.options.SocketOptions;

public final class XPubSocketOptions extends CommonSocketOptions {
    XPubSocketOptions(Socket socket) {
        super(socket);
    }

    public void subscriptionMode(XPubSubscriptionMode mode) {
        Objects.requireNonNull(mode, "mode");
        switch (mode) {
            case DEFAULT -> {
                socket.setOption(SocketOptions.XPUB_MANUAL, 0);
                socket.setOption(SocketOptions.XPUB_MANUAL_LAST_VALUE, 0);
            }
            case MANUAL -> {
                socket.setOption(SocketOptions.XPUB_MANUAL_LAST_VALUE, 0);
                socket.setOption(SocketOptions.XPUB_MANUAL, 1);
            }
            case MANUAL_LAST_VALUE -> {
                socket.setOption(SocketOptions.XPUB_MANUAL, 1);
                socket.setOption(SocketOptions.XPUB_MANUAL_LAST_VALUE, 1);
            }
        }
    }
}
