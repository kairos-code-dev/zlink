/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

public record MonitorEvent(long event, long value, byte[] routingId,
                           String localAddress, String remoteAddress) {
    public MonitorEvent {
        routingId = routingId == null ? new byte[0] : routingId.clone();
    }

    @Override
    public byte[] routingId() {
        return routingId.clone();
    }
}
