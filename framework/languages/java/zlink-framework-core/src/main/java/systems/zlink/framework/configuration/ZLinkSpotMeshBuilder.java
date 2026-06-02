package systems.zlink.framework.configuration;

import java.util.function.Consumer;

public interface ZLinkSpotMeshBuilder {
    void useDiscovery(Consumer<RegistryBuilder> configure);

    void addNode(String spotNodeName, Consumer<ZLinkSpotNodeBuilder> configure);
}
