/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import dev.kairoscode.zlink.options.SocketOptions;

public final class RouterSocketOptions extends CommonSocketOptions {
    RouterSocketOptions(Socket socket) {
        super(socket);
    }

    public boolean mandatory() {
        return socket.getOption(SocketOptions.ROUTER_MANDATORY) != 0;
    }

    public void mandatory(boolean enabled) {
        socket.setOption(SocketOptions.ROUTER_MANDATORY, enabled ? 1 : 0);
    }

    public boolean handover() {
        return socket.getOption(SocketOptions.ROUTER_HANDOVER) != 0;
    }

    public void handover(boolean enabled) {
        socket.setOption(SocketOptions.ROUTER_HANDOVER, enabled ? 1 : 0);
    }

    public boolean probeRouter() {
        return socket.getOption(SocketOptions.PROBE_ROUTER) != 0;
    }

    public void probeRouter(boolean enabled) {
        socket.setOption(SocketOptions.PROBE_ROUTER, enabled ? 1 : 0);
    }

    public RoutingId connectRoutingId() {
        byte[] value = socket.getOption(SocketOptions.CONNECT_ROUTING_ID_BYTES);
        return value.length == 0 ? null : RoutingId.copyOf(value);
    }

    public void connectRoutingId(RoutingId routingId) {
        socket.setOption(SocketOptions.CONNECT_ROUTING_ID_BYTES,
            routingId == null ? new byte[0] : routingId.toByteArray());
    }
}
