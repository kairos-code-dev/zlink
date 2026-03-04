/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import dev.kairoscode.zlink.internal.Native;
import dev.kairoscode.zlink.internal.NativeHelpers;
import dev.kairoscode.zlink.internal.NativeLayouts;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;

public final class SpotNode implements AutoCloseable {
    private MemorySegment handle;

    public SpotNode(Context ctx) {
        this.handle = Native.spotNodeNew(ctx.handle());
        if (handle == null || handle.address() == 0)
            throw new RuntimeException("zlink_spot_node_new failed");
    }

    MemorySegment handle() {
        return handle;
    }

    public void bind(String endpoint) {
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.spotNodeBind(handle, NativeHelpers.toCString(arena, endpoint));
            if (rc != 0)
                throw new RuntimeException("zlink_spot_node_bind failed");
        }
    }

    public void connectRegistry(String registryEndpoint) {
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.spotNodeConnectRegistry(handle, NativeHelpers.toCString(arena, registryEndpoint));
            if (rc != 0)
                throw new RuntimeException("zlink_spot_node_connect_registry failed");
        }
    }

    public void connectPeerPub(String peerPubEndpoint) {
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.spotNodeConnectPeer(handle, NativeHelpers.toCString(arena, peerPubEndpoint));
            if (rc != 0)
                throw new RuntimeException("zlink_spot_node_connect_peer_pub failed");
        }
    }

    public void disconnectPeerPub(String peerPubEndpoint) {
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.spotNodeDisconnectPeer(handle, NativeHelpers.toCString(arena, peerPubEndpoint));
            if (rc != 0)
                throw new RuntimeException("zlink_spot_node_disconnect_peer_pub failed");
        }
    }

    public void register(String serviceName, String advertiseEndpoint) {
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.spotNodeRegister(handle,
                NativeHelpers.toCString(arena, serviceName),
                NativeHelpers.toCString(arena, advertiseEndpoint));
            if (rc != 0)
                throw new RuntimeException("zlink_spot_node_register failed");
        }
    }

    public void unregister(String serviceName) {
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.spotNodeUnregister(handle, NativeHelpers.toCString(arena, serviceName));
            if (rc != 0)
                throw new RuntimeException("zlink_spot_node_unregister failed");
        }
    }

    public void setDiscovery(Discovery discovery, String serviceName) {
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.spotNodeSetDiscovery(handle, discovery.handle(),
                NativeHelpers.toCString(arena, serviceName));
            if (rc != 0)
                throw new RuntimeException("zlink_spot_node_set_discovery failed");
        }
    }

    public void setTlsServer(String cert, String key) {
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.spotNodeSetTlsServer(handle,
                NativeHelpers.toCString(arena, cert),
                NativeHelpers.toCString(arena, key));
            if (rc != 0)
                throw new RuntimeException("zlink_spot_node_set_tls_server failed");
        }
    }

    public void setTlsClient(String caCert, String hostname, int trustSystem) {
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.spotNodeSetTlsClient(handle,
                NativeHelpers.toCString(arena, caCert),
                NativeHelpers.toCString(arena, hostname), trustSystem);
            if (rc != 0)
                throw new RuntimeException("zlink_spot_node_set_tls_client failed");
        }
    }

    public void setSockOpt(SpotNodeSocketRole role, SocketOption option, byte[] value) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment buf = arena.allocate(value.length);
            MemorySegment.copy(MemorySegment.ofArray(value), 0, buf, 0, value.length);
            int rc = Native.spotNodeSetSockOpt(handle, role.getValue(),
                option.getValue(), buf, value.length);
            if (rc != 0)
                throw new RuntimeException("zlink_spot_node_setsockopt failed");
        }
    }

    public void setSockOpt(SpotNodeSocketRole role, SocketOption option, int value) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment buf = arena.allocate(ValueLayout.JAVA_INT);
            buf.set(ValueLayout.JAVA_INT, 0, value);
            int rc = Native.spotNodeSetSockOpt(handle, role.getValue(),
                option.getValue(), buf, ValueLayout.JAVA_INT.byteSize());
            if (rc != 0)
                throw new RuntimeException("zlink_spot_node_setsockopt failed");
        }
    }

    public void setSockOpt(SpotNodeOption option, int value) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment buf = arena.allocate(ValueLayout.JAVA_INT);
            buf.set(ValueLayout.JAVA_INT, 0, value);
            int rc = Native.spotNodeSetSockOpt(handle, SpotNodeSocketRole.NODE.getValue(),
                option.getValue(), buf, ValueLayout.JAVA_INT.byteSize());
            if (rc != 0)
                throw new RuntimeException("zlink_spot_node_setsockopt failed");
        }
    }

    public Socket pubSocket() {
        MemorySegment sock = Native.spotNodePubSocket(handle);
        if (sock == null || sock.address() == 0)
            throw new RuntimeException("zlink_spot_node_pub_socket_unsafe failed");
        return Socket.adopt(sock, false);
    }

    public Socket subSocket() {
        MemorySegment sock = Native.spotNodeSubSocket(handle);
        if (sock == null || sock.address() == 0)
            throw new RuntimeException("zlink_spot_node_sub_socket_unsafe failed");
        return Socket.adopt(sock, false);
    }

    public PeerInfo[] pubPeers() {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment count = arena.allocate(ValueLayout.JAVA_LONG);
            count.set(ValueLayout.JAVA_LONG, 0, 0L);
            int rc = Native.spotNodePubPeers(handle, MemorySegment.NULL, count);
            if (rc != 0)
                throw new RuntimeException("zlink_spot_node_pub_peers failed");
            long available = count.get(ValueLayout.JAVA_LONG, 0);
            if (available <= 0)
                return new PeerInfo[0];

            MemorySegment peersMem = arena.allocate(NativeLayouts.PEER_INFO_LAYOUT,
              available);
            count.set(ValueLayout.JAVA_LONG, 0, available);
            rc = Native.spotNodePubPeers(handle, peersMem, count);
            if (rc != 0)
                throw new RuntimeException("zlink_spot_node_pub_peers failed");

            long actualLong = count.get(ValueLayout.JAVA_LONG, 0);
            if (actualLong < 0)
                actualLong = 0;
            if (actualLong > available)
                actualLong = available;
            int actual = (int) actualLong;
            long stride = NativeLayouts.PEER_INFO_LAYOUT.byteSize();
            PeerInfo[] out = new PeerInfo[actual];
            for (int i = 0; i < actual; i++) {
                out[i] = PeerInfo.fromNative(peersMem.asSlice((long) i * stride,
                  stride));
            }
            return out;
        }
    }

    public PeerInfo[] subPeers() {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment count = arena.allocate(ValueLayout.JAVA_LONG);
            count.set(ValueLayout.JAVA_LONG, 0, 0L);
            int rc = Native.spotNodeSubPeers(handle, MemorySegment.NULL, count);
            if (rc != 0)
                throw new RuntimeException("zlink_spot_node_sub_peers failed");
            long available = count.get(ValueLayout.JAVA_LONG, 0);
            if (available <= 0)
                return new PeerInfo[0];

            MemorySegment peersMem = arena.allocate(NativeLayouts.PEER_INFO_LAYOUT,
              available);
            count.set(ValueLayout.JAVA_LONG, 0, available);
            rc = Native.spotNodeSubPeers(handle, peersMem, count);
            if (rc != 0)
                throw new RuntimeException("zlink_spot_node_sub_peers failed");

            long actualLong = count.get(ValueLayout.JAVA_LONG, 0);
            if (actualLong < 0)
                actualLong = 0;
            if (actualLong > available)
                actualLong = available;
            int actual = (int) actualLong;
            long stride = NativeLayouts.PEER_INFO_LAYOUT.byteSize();
            PeerInfo[] out = new PeerInfo[actual];
            for (int i = 0; i < actual; i++) {
                out[i] = PeerInfo.fromNative(peersMem.asSlice((long) i * stride,
                  stride));
            }
            return out;
        }
    }

    @Override
    public void close() {
        if (handle == null || handle.address() == 0)
            return;
        Native.spotNodeDestroy(handle);
        handle = MemorySegment.NULL;
    }
}
