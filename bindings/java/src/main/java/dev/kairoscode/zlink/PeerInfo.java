/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import dev.kairoscode.zlink.internal.NativeHelpers;
import dev.kairoscode.zlink.internal.NativeLayouts;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;

public record PeerInfo(
  byte[] routingId,
  String remoteAddr,
  long connectedTime,
  long msgsSent,
  long msgsReceived,
  long sndPendingMsgs,
  long rcvPendingMsgs
) {
    static PeerInfo fromNative(MemorySegment segment) {
        int ridSize = segment.get(ValueLayout.JAVA_BYTE,
          NativeLayouts.PEER_ROUTING_OFFSET) & 0xFF;
        byte[] rid = new byte[ridSize];
        if (ridSize > 0) {
            MemorySegment.copy(segment, NativeLayouts.PEER_ROUTING_OFFSET + 1,
              MemorySegment.ofArray(rid), 0, ridSize);
        }

        String remote = NativeHelpers.fromCString(segment.asSlice(
          NativeLayouts.PEER_REMOTE_OFFSET, 256), 256);
        long connected = segment.get(ValueLayout.JAVA_LONG,
          NativeLayouts.PEER_CONNECTED_OFFSET);
        long sent = segment.get(ValueLayout.JAVA_LONG,
          NativeLayouts.PEER_MSGS_SENT_OFFSET);
        long received = segment.get(ValueLayout.JAVA_LONG,
          NativeLayouts.PEER_MSGS_RECV_OFFSET);
        long sndPending = segment.get(ValueLayout.JAVA_LONG,
          NativeLayouts.PEER_SND_PENDING_OFFSET);
        long rcvPending = segment.get(ValueLayout.JAVA_LONG,
          NativeLayouts.PEER_RCV_PENDING_OFFSET);
        return new PeerInfo(rid, remote, connected, sent, received, sndPending,
          rcvPending);
    }
}
