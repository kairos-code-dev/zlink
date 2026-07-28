package systems.zlink.framework.runtime.mesh;

import java.time.Duration;
import systems.zlink.framework.runtime.internal.backend.ZLinkBackendContext;
import systems.zlink.framework.runtime.internal.backend.ZLinkBackendObject;
import systems.zlink.framework.runtime.internal.backend.ZLinkMeshBackendAdapter;
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
            node.setObjectRole(
                registration.objectServer()
                    ? systems.zlink.framework.locations.ZLinkMeshNodeObjectRole.SERVER
                    : registration.objectRoleEnabled()
                        ? systems.zlink.framework.locations.ZLinkMeshNodeObjectRole.CLIENT
                        : systems.zlink.framework.locations.ZLinkMeshNodeObjectRole.NONE);
            node.setPlacementWeight(registration.placementWeight());
            int routerSendHighWaterMark =
                registration.configureRouterSocket().sendHighWaterMark();
            node.setRouterHighWaterMark(routerSendHighWaterMark);
            node.setRouterPendingAdmissionCapacity(
                routerSendHighWaterMark > 0
                    ? routerSendHighWaterMark
                    : 4096);
            node.setRouterSendTimeout(
                registration.configureRouterSocket().sendTimeout()
                    .orElse(Duration.ofSeconds(1)));
            node.setMailboxMessageBudget(
                registration.configureRouterSocket().receiveHighWaterMark());
            registration.channelNames().forEach(node::addChannel);
            registration.channelWeights().forEach(node::setChannelWeight);
            if (!registration.spotFactories().isEmpty()
                || !registration.entrySpots().isEmpty()
                || !registration.actorFactories().isEmpty()
                || !registration.channelNames().isEmpty()) {
                node.spotNode();
            }
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
