/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import dev.kairoscode.zlink.options.SocketOptions;
public class PubSocketOptions extends CommonSocketOptions {
    PubSocketOptions(Socket socket) {
        super(socket);
    }

    public boolean verbose() {
        return socket.getOption(SocketOptions.XPUB_VERBOSE) != 0;
    }

    public void verbose(boolean enabled) {
        socket.setOption(SocketOptions.XPUB_VERBOSE, enabled ? 1 : 0);
    }

    public boolean verboser() {
        return socket.getOption(SocketOptions.XPUB_VERBOSER) != 0;
    }

    public void verboser(boolean enabled) {
        socket.setOption(SocketOptions.XPUB_VERBOSER, enabled ? 1 : 0);
    }

    public boolean noDrop() {
        return socket.getOption(SocketOptions.XPUB_NODROP) != 0;
    }

    public void noDrop(boolean enabled) {
        socket.setOption(SocketOptions.XPUB_NODROP, enabled ? 1 : 0);
    }

    public boolean manual() {
        return socket.getOption(SocketOptions.XPUB_MANUAL) != 0;
    }

    public void manual(boolean enabled) {
        socket.setOption(SocketOptions.XPUB_MANUAL, enabled ? 1 : 0);
    }
}
