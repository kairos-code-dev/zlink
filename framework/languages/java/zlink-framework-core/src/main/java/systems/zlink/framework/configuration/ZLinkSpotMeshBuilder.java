package systems.zlink.framework.configuration;

import java.util.function.Consumer;

public interface ZLinkSpotMeshBuilder {
    void useDiscovery(Consumer<ZLinkDiscoveryBuilder> configure);

    void addRegistryEndpoint(String endpoint);

    void addNode(String spotNodeName, Consumer<ZLinkSpotNodeBuilder> configure);
}
