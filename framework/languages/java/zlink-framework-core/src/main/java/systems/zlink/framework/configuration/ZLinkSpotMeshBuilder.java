package systems.zlink.framework.configuration;

public interface ZLinkSpotMeshBuilder {
    ZLinkDiscoveryBuilder useDiscovery();

    ZLinkSpotNodeBuilder addNode(String spotNodeName);
}
