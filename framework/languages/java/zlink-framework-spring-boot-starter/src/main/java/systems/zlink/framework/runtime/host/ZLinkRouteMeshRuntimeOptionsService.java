package systems.zlink.framework.runtime.host;

import java.util.Map;
import java.util.function.Supplier;
import systems.zlink.framework.channels.ZLinkMeshChannelRuntimeOptions;
import systems.zlink.framework.channels.ZLinkMeshNodeRuntimeOptions;
import systems.zlink.framework.channels.ZLinkRouteMeshRuntimeOptions;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.runtime.internal.backend.ZLinkInternalMeshNode;

public final class ZLinkRouteMeshRuntimeOptionsService
    implements ZLinkRouteMeshRuntimeOptions {
    private final Supplier<Map<String, ZLinkInternalMeshNode>> nodes;

    public ZLinkRouteMeshRuntimeOptionsService(ZLinkFrameworkLifecycle lifecycle) {
        this(lifecycle::monitoringSpotSources);
    }

    ZLinkRouteMeshRuntimeOptionsService(
        Supplier<Map<String, ZLinkInternalMeshNode>> nodes) {
        this.nodes = java.util.Objects.requireNonNull(nodes, "nodes");
    }

    @Override
    public ZLinkMeshNodeRuntimeOptions meshNode(String meshName) {
        ZLinkInternalMeshNode node = requireNode(meshName);
        return new ZLinkMeshNodeRuntimeOptions() {
            @Override
            public long maxMessageSize() {
                return node.maxMessageSize();
            }

            @Override
            public void maxMessageSize(long value) {
                if (value < 0) {
                    throw new ZLinkConfigurationException(
                        "maxMessageSize must not be negative; use 0 for no framework limit");
                }
                node.setMaxMessageSize(value);
            }
        };
    }

    @Override
    public ZLinkMeshChannelRuntimeOptions channel(
        String meshName,
        String channelName) {
        ZLinkInternalMeshNode node = requireNode(meshName);
        if (channelName == null || channelName.isBlank()) {
            throw new IllegalArgumentException("channelName is required");
        }
        if (!node.channelWeights().containsKey(channelName)) {
            throw new ZLinkConfigurationException(
                "RouteMesh channel is not configured: "
                    + meshName + "/" + channelName);
        }
        return new ZLinkMeshChannelRuntimeOptions() {
            @Override
            public int weight() {
                return requireWeight(node, meshName, channelName);
            }

            @Override
            public void weight(int value) {
                if (value < 0 || value > 100) {
                    throw new ZLinkConfigurationException(
                        "channel weight must be in range 0..100");
                }
                node.setChannelWeight(channelName, value);
            }
        };
    }

    private ZLinkInternalMeshNode requireNode(String meshName) {
        if (meshName == null || meshName.isBlank()) {
            throw new IllegalArgumentException("meshName is required");
        }
        ZLinkInternalMeshNode node = nodes.get().get(meshName);
        if (node == null) {
            throw new ZLinkConfigurationException(
                "RouteMesh is not configured: " + meshName);
        }
        return node;
    }

    private static int requireWeight(
        ZLinkInternalMeshNode node,
        String meshName,
        String channelName) {
        Integer weight = node.channelWeights().get(channelName);
        if (weight == null) {
            throw new ZLinkConfigurationException(
                "RouteMesh channel is not configured: "
                    + meshName + "/" + channelName);
        }
        return weight;
    }
}
