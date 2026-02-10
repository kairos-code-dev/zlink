/* SPDX-License-Identifier: MPL-2.0 */

package io.ulalax.zlink;

import io.ulalax.zlink.internal.Native;
import io.ulalax.zlink.internal.NativeHelpers;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;

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

    public Socket pubSocket() {
        MemorySegment sock = Native.spotNodePubSocket(handle);
        if (sock == null || sock.address() == 0)
            throw new RuntimeException("zlink_spot_node_pub_socket failed");
        return Socket.adopt(sock, false);
    }

    public Socket subSocket() {
        MemorySegment sock = Native.spotNodeSubSocket(handle);
        if (sock == null || sock.address() == 0)
            throw new RuntimeException("zlink_spot_node_sub_socket failed");
        return Socket.adopt(sock, false);
    }

    @Override
    public void close() {
        if (handle == null || handle.address() == 0)
            return;
        Native.spotNodeDestroy(handle);
        handle = MemorySegment.NULL;
    }
}
