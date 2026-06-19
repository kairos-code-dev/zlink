package systems.zlink.framework.configuration;

public interface ZLinkSpotMeshBuilder {
    ZLinkDiscoveryBuilder useDiscovery();

    ZLinkSpotMeshBuilder useRegistrySpotResolver();

    ZLinkSpotNodeBuilder addNode(String spotNodeName);
}
