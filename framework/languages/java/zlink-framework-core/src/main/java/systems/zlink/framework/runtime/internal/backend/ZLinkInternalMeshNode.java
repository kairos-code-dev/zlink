package systems.zlink.framework.runtime.internal.backend;

import java.util.List;
import java.util.function.Consumer;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.service.spot.MeshPeerEntry;
import systems.zlink.contracts.service.spot.MeshNodeStatus;
import systems.zlink.framework.runtime.backend.ZLinkBackendObject;

public interface ZLinkInternalMeshNode extends ZLinkBackendObject {
    void setBind(String endpoint);

    void addChannel(String channelName);

    void setChannelWeight(String channelName, int weight);

    void setRoutingId(RoutingId routingId);

    void start();

    long connectPeer(String endpoint);

    long connectPeer(String endpoint, RoutingId expectedRoutingId);

    MeshNodeStatus status();

    List<MeshPeerEntry> peers();

    List<Long> connectionIntentIds();

    void startDispatch(Consumer<ZLinkMeshDispatchRecord> receiver);
}
