package systems.zlink.framework.runtime.mesh;

import systems.zlink.framework.runtime.backend.ZLinkBackendContext;
import systems.zlink.framework.runtime.backend.ZLinkMeshBackendAdapter;
import systems.zlink.framework.runtime.internal.backend.ZLinkInternalMeshNode;

public final class ZLinkMeshNodeRuntime implements AutoCloseable {
    private final ZLinkInternalMeshNode node;

    private ZLinkMeshNodeRuntime(ZLinkInternalMeshNode node) {
        this.node = node;
    }

    public static ZLinkMeshNodeRuntime start(
        MeshNodeRegistration registration,
        ZLinkMeshBackendAdapter adapter,
        ZLinkBackendContext context) {
        ZLinkInternalMeshNode node =
            adapter.createMeshNode(context, registration.meshName());
        boolean started = false;
        try {
            if (registration.routingId() != null) {
                node.setRoutingId(registration.routingId());
            }
            node.setBind(registration.bindEndpoint());
            registration.channelNames().forEach(node::addChannel);
            registration.channelWeights().forEach(node::setChannelWeight);
            node.start();
            for (MeshNodeRegistration.Peer peer : registration.peers()) {
                if (peer.expectedRoutingId() == null) {
                    node.connectPeer(peer.endpoint());
                } else {
                    node.connectPeer(peer.endpoint(), peer.expectedRoutingId());
                }
            }
            started = true;
            return new ZLinkMeshNodeRuntime(node);
        } finally {
            if (!started) {
                node.close();
            }
        }
    }

    public ZLinkInternalMeshNode node() {
        return node;
    }

    @Override
    public void close() {
        node.close();
    }
}
