/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

/** Event selection bits accepted by {@link MeshNode#openMonitor(long)}. */
public final class MeshMonitorEventMask {
    public static final long STATE_CHANGED = 1L << 0;
    public static final long PEER_CONNECTING = 1L << 1;
    public static final long PEER_ADMITTED = 1L << 2;
    public static final long PEER_DRAINING = 1L << 3;
    public static final long PEER_CLOSED = 1L << 4;
    public static final long PEER_REJECTED = 1L << 5;
    public static final long CHANNEL_CHANGED = 1L << 6;
    public static final long MESSAGE_SUBMITTED = 1L << 7;
    public static final long MULTICAST_COMMITTED = 1L << 8;
    public static final long MULTICAST_DROPPED = 1L << 9;
    public static final long BACKPRESSURED = 1L << 10;
    public static final long OPERATION_COMPLETED = 1L << 11;
    public static final long PROTOCOL_ERROR = 1L << 12;
    public static final long CLAIM_REVOKED = 1L << 13;
    public static final long ALL = (1L << 14) - 1;

    private MeshMonitorEventMask() {
    }
}
